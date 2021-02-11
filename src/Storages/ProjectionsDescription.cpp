#include <Interpreters/ExpressionAnalyzer.h>
#include <Interpreters/TreeRewriter.h>
#include <Storages/ProjectionsDescription.h>

#include <Parsers/ASTProjectionDeclaration.h>
#include <Parsers/ParserCreateQuery.h>
#include <Parsers/parseQuery.h>
#include <Parsers/queryToString.h>

#include <Core/Defines.h>
#include <Interpreters/InterpreterSelectQuery.h>
#include <Parsers/ASTProjectionSelectQuery.h>
#include <Parsers/ASTSubquery.h>
#include <Processors/Pipe.h>
#include <Processors/Sources/SourceFromSingleChunk.h>


namespace DB
{
namespace ErrorCodes
{
    extern const int INCORRECT_QUERY;
    extern const int NO_SUCH_PROJECTION_IN_TABLE;
    extern const int ILLEGAL_PROJECTION;
    extern const int NOT_IMPLEMENTED;
};

ProjectionDescription::ProjectionDescription(const ProjectionDescription & other)
    : definition_ast(other.definition_ast ? other.definition_ast->clone() : nullptr)
    , query_ast(other.query_ast ? other.query_ast->clone() : nullptr)
    , name(other.name)
    , type(other.type)
    , required_columns(other.required_columns)
    , column_names(other.column_names)
    , data_types(other.data_types)
    , sample_block(other.sample_block)
    , metadata(other.metadata)
    , key_size(other.key_size)
{
}


bool ProjectionDescription::isPrimaryKeyColumnPossiblyWrappedInFunctions(const ASTPtr & node) const
{
    const String column_name = node->getColumnName();

    for (const auto & key_name : metadata->getPrimaryKeyColumns())
        if (column_name == key_name)
            return true;

    if (const auto * func = node->as<ASTFunction>())
        if (func->arguments->children.size() == 1)
            return isPrimaryKeyColumnPossiblyWrappedInFunctions(func->arguments->children.front());

    return false;
}


ProjectionDescription & ProjectionDescription::operator=(const ProjectionDescription & other)
{
    if (&other == this)
        return *this;

    if (other.definition_ast)
        definition_ast = other.definition_ast->clone();
    else
        definition_ast.reset();

    if (other.query_ast)
        query_ast = other.query_ast->clone();
    else
        query_ast.reset();

    name = other.name;
    type = other.type;
    required_columns = other.required_columns;
    column_names = other.column_names;
    data_types = other.data_types;
    sample_block = other.sample_block;
    metadata = other.metadata;
    key_size = other.key_size;
    return *this;
}

bool ProjectionDescription::operator==(const ProjectionDescription & other) const
{
    return name == other.name && queryToString(definition_ast) == queryToString(other.definition_ast);
}

ProjectionDescription
ProjectionDescription::getProjectionFromAST(const ASTPtr & definition_ast, const ColumnsDescription & columns, const Context & context)
{
    const auto * projection_definition = definition_ast->as<ASTProjectionDeclaration>();

    if (!projection_definition)
        throw Exception("Cannot create projection from non ASTProjectionDeclaration AST", ErrorCodes::INCORRECT_QUERY);

    if (projection_definition->name.empty())
        throw Exception("Projection must have name in definition.", ErrorCodes::INCORRECT_QUERY);

    if (projection_definition->type.empty())
        throw Exception("TYPE is required for projection", ErrorCodes::INCORRECT_QUERY);

    if (!projection_definition->query)
        throw Exception("QUERY is required for projection", ErrorCodes::INCORRECT_QUERY);

    if (projection_definition->type == "normal")
        throw Exception("Normal projections are not supported for now", ErrorCodes::NOT_IMPLEMENTED);

    ProjectionDescription result;
    result.definition_ast = projection_definition->clone();
    result.name = projection_definition->name;
    result.type = projection_definition->type;
    auto query = projection_definition->query->as<ASTProjectionSelectQuery &>();
    result.query_ast = query.cloneToASTSelect();

    auto external_storage_holder = std::make_shared<TemporaryTableHolder>(context, columns, ConstraintsDescription{});
    StoragePtr storage = external_storage_holder->getTable();
    InterpreterSelectQuery select(
        result.query_ast,
        context,
        storage,
        {},
        SelectQueryOptions{result.type == "normal" ? QueryProcessingStage::FetchColumns : QueryProcessingStage::WithMergeableState}
            .modify()
            .ignoreAlias());

    result.required_columns = select.getRequiredColumns();
    result.sample_block = select.getSampleBlock();

    for (size_t i = 0; i < result.sample_block.columns(); ++i)
    {
        const auto & column = result.sample_block.getByPosition(i);

        if (column.column && isColumnConst(*column.column))
            throw Exception(ErrorCodes::NOT_IMPLEMENTED, "Projections cannot contain constant columns: {}", column.name);

        result.column_names.emplace_back(column.name);
        result.data_types.emplace_back(column.type);
    }

    StorageInMemoryMetadata metadata;
    metadata.setColumns(ColumnsDescription(result.sample_block.getNamesAndTypesList()));
    metadata.partition_key = KeyDescription::getSortingKeyFromAST({}, metadata.columns, context, {});

    const auto & query_select = result.query_ast->as<const ASTSelectQuery &>();
    if (result.type == "aggregate" && !query_select.groupBy())
        throw Exception("When TYPE aggregate is specified, there should be a non-constant GROUP BY clause", ErrorCodes::ILLEGAL_PROJECTION);

    if (select.hasAggregation())
    {
        if (result.type == "normal")
            throw Exception(
                "When aggregation is used in projection, TYPE aggregate should be specified", ErrorCodes::ILLEGAL_PROJECTION);
        if (const auto & group_expression_list = query_select.groupBy())
        {
            ASTPtr order_expression;
            if (group_expression_list->children.size() == 1)
            {
                result.key_size = 1;
                order_expression = std::make_shared<ASTIdentifier>(group_expression_list->children.front()->getColumnName());
            }
            else
            {
                auto function_node = std::make_shared<ASTFunction>();
                function_node->name = "tuple";
                function_node->arguments = group_expression_list->clone();
                result.key_size = function_node->arguments->children.size();
                for (auto & child : function_node->arguments->children)
                    child = std::make_shared<ASTIdentifier>(child->getColumnName());
                function_node->children.push_back(function_node->arguments);
                order_expression = function_node;
            }
            metadata.sorting_key = KeyDescription::getSortingKeyFromAST(order_expression, metadata.columns, context, {});
            metadata.primary_key = KeyDescription::getKeyFromAST(order_expression, metadata.columns, context);
        }
        else
        {
            metadata.sorting_key = KeyDescription::getSortingKeyFromAST({}, metadata.columns, context, {});
            metadata.primary_key = KeyDescription::getKeyFromAST({}, metadata.columns, context);
        }
        if (query_select.orderBy())
            throw Exception(
                "When aggregation is used in projection, ORDER BY cannot be specified", ErrorCodes::ILLEGAL_PROJECTION);
    }
    else
    {
        metadata.sorting_key = KeyDescription::getSortingKeyFromAST(query_select.orderBy(), metadata.columns, context, {});
        metadata.primary_key = KeyDescription::getKeyFromAST(query_select.orderBy(), metadata.columns, context);
    }
    metadata.primary_key.definition_ast = nullptr;
    result.metadata = std::make_shared<StorageInMemoryMetadata>(metadata);
    return result;
}

void ProjectionDescription::recalculateWithNewColumns(const ColumnsDescription & new_columns, const Context & context)
{
    *this = getProjectionFromAST(definition_ast, new_columns, context);
}

String ProjectionsDescription::toString() const
{
    if (empty())
        return {};

    ASTExpressionList list;
    for (const auto & projection : projections)
        list.children.push_back(projection.definition_ast);

    return serializeAST(list, true);
}

ProjectionsDescription ProjectionsDescription::parse(const String & str, const ColumnsDescription & columns, const Context & context)
{
    ProjectionsDescription result;
    if (str.empty())
        return result;

    ParserProjectionDeclarationList parser;
    ASTPtr list = parseQuery(parser, str, 0, DBMS_DEFAULT_MAX_PARSER_DEPTH);

    for (const auto & projection_ast : list->children)
    {
        auto projection = ProjectionDescription::getProjectionFromAST(projection_ast, columns, context);
        result.add(std::move(projection));
    }

    return result;
}

bool ProjectionsDescription::has(const String & projection_name) const
{
    return projections.get<1>().find(projection_name) != projections.get<1>().end();
}

const ProjectionDescription & ProjectionsDescription::get(const String & projection_name) const
{
    auto it = projections.get<1>().find(projection_name);
    if (it == projections.get<1>().end())
        throw Exception("There is no projection " + projection_name + " in table", ErrorCodes::NO_SUCH_PROJECTION_IN_TABLE);

    return *it;
}

void ProjectionsDescription::add(ProjectionDescription && projection, const String & after_projection, bool first, bool if_not_exists)
{
    if (has(projection.name))
    {
        if (if_not_exists)
            return;
        throw Exception(
            "Cannot add projection " + projection.name + ": projection with this name already exists", ErrorCodes::ILLEGAL_PROJECTION);
    }

    auto insert_it = projections.cend();

    if (first)
        insert_it = projections.cbegin();
    else if (!after_projection.empty())
    {
        auto it = std::find_if(projections.cbegin(), projections.cend(), [&after_projection](const auto & projection_)
        {
            return projection_.name == after_projection;
        });
        if (it != projections.cend())
            ++it;
        insert_it = it;
    }

    projections.get<0>().insert(insert_it, std::move(projection));
}

void ProjectionsDescription::remove(const String & projection_name)
{
    auto it = projections.get<1>().find(projection_name);
    if (it == projections.get<1>().end())
        throw Exception("There is no projection " + projection_name + " in table.", ErrorCodes::NO_SUCH_PROJECTION_IN_TABLE);
    projections.get<1>().erase(it);
}

ExpressionActionsPtr
ProjectionsDescription::getSingleExpressionForProjections(const ColumnsDescription & columns, const Context & context) const
{
    ASTPtr combined_expr_list = std::make_shared<ASTExpressionList>();
    for (const auto & projection : projections)
        for (const auto & projection_expr : projection.query_ast->children)
            combined_expr_list->children.push_back(projection_expr->clone());

    auto syntax_result = TreeRewriter(context).analyze(combined_expr_list, columns.getAllPhysical());
    return ExpressionAnalyzer(combined_expr_list, syntax_result, context).getActions(false);
}

}