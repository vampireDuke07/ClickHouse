---
machine_translated: true
machine_translated_rev: 5decc73b5dc60054f19087d3690c4eb99446a6c3
---

# 系统。零件 {#system_tables-parts}

包含关于 [MergeTree](../../engines/table-engines/mergetree-family/mergetree.md) 表部分的信息

每行描述一个数据零件。

列:

-   `partition` (String) – 分区名称。要了解什么是分区,请参阅 [ALTER](../../sql-reference/statements/alter.md#query_language_queries_alter) 查询的描述。

    格式:

    -   `YYYYMM` 用于按月自动分区。
    -   `any_string` 用于手动指定分区。

-   `name` (`String`) – 数据零件的名称。
-   `part_type` (`String`) – 数据零件的储存格式。

    可能的值:
    
    -   `Wide` 每个列存储在文件系统的一个单独的文件中。
    -   `Compact` 所有列都存储在文件系统中的一个文件中。
    数据存储格式由MergeTree表的min_bytes_for_wide_part和min_rows_for_wide_part设置控制。

-   `active` (`UInt8`) – 表示数据部分是否活跃的标志。如果数据部分是活跃的，则说明表中使用它。否则，表示删除。未激活的数据零件在合并后仍然存在。

-   `marks` (`UInt64`) – 标记的数量。表示数据零件中的大致行数，multiply `marks` 通过索引粒度（通常为8192）（此提示不适用于自适应粒度）。

-   `rows` (`UInt64`) – 行数。

-   `bytes_on_disk` (`UInt64`) – 所有数据部分文件的总大小(以字节为单位)。

-   `data_compressed_bytes` (`UInt64`) – Total size of compressed data in the data part. All the auxiliary files (for example, files with marks) are not included.

-   `data_uncompressed_bytes` (`UInt64`) – Total size of uncompressed data in the data part. All the auxiliary files (for example, files with marks) are not included.

-   `marks_bytes` (`UInt64`) – The size of the file with marks.

-   `modification_time` (`DateTime`) – The time the directory with the data part was modified. This usually corresponds to the time of data part creation.\|

-   `remove_time` (`DateTime`) – The time when the data part became inactive.

-   `refcount` (`UInt32`) – The number of places where the data part is used. A value greater than 2 indicates that the data part is used in queries or merges.

-   `min_date` (`Date`) – The minimum value of the date key in the data part.

-   `max_date` (`Date`) – The maximum value of the date key in the data part.

-   `min_time` (`DateTime`) – The minimum value of the date and time key in the data part.

-   `max_time`(`DateTime`) – The maximum value of the date and time key in the data part.

-   `partition_id` (`String`) – ID of the partition.

-   `min_block_number` (`UInt64`) – The minimum number of data parts that make up the current part after merging.

-   `max_block_number` (`UInt64`) – The maximum number of data parts that make up the current part after merging.

-   `level` (`UInt32`) – Depth of the merge tree. Zero means that the current part was created by insert rather than by merging other parts.

-   `data_version` (`UInt64`) – Number that is used to determine which mutations should be applied to the data part (mutations with a version higher than `data_version`).

-   `primary_key_bytes_in_memory` (`UInt64`) – The amount of memory (in bytes) used by primary key values.

-   `primary_key_bytes_in_memory_allocated` (`UInt64`) – The amount of memory (in bytes) reserved for primary key values.

-   `is_frozen` (`UInt8`) – Flag that shows that a partition data backup exists. 1, the backup exists. 0, the backup doesn't exist. For more details, see [FREEZE PARTITION](../../sql-reference/statements/alter.md#alter_freeze-partition)

-   `database` (`String`) – Name of the database.

-   `table` (`String`) – Name of the table.

-   `engine` (`String`) – Name of the table engine without parameters.

-   `path` (`String`) – Absolute path to the folder with data part files.

-   `disk` (`String`) – Name of a disk that stores the data part.

-   `hash_of_all_files` (`String`) – [sipHash128](../../sql-reference/functions/hash-functions.md#hash_functions-siphash128) 的压缩文件。

-   `hash_of_uncompressed_files` (`String`) – [sipHash128](../../sql-reference/functions/hash-functions.md#hash_functions-siphash128) 未压缩的文件（带标记的文件，索引文件等。).

-   `uncompressed_hash_of_compressed_files` (`String`) – [sipHash128](../../sql-reference/functions/hash-functions.md#hash_functions-siphash128) 压缩文件中的数据，就好像它们是未压缩的。

-   `bytes` (`UInt64`) – Alias for `bytes_on_disk`.

-   `marks_size` (`UInt64`) – Alias for `marks_bytes`.
