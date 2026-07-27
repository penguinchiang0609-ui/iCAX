# DatabaseLanguage

`DatabaseLanguage` 是 Database 的可选文本语言扩展工程。

- 单向依赖 `Database`。
- 解析受控 Lambda 字符串。
- 解析和执行 EntitySQL。
- 输出 Database 原生的 `SEntityWhere` / `SEntityQuery` / `SEntityInsert` / `SEntityUpdate`，不维护第二套求值或事务逻辑。
- EntitySQL 支持 Entity 字段投影、`ORDER BY`、`GROUP BY`、聚合和原子 `INSERT ENTITY`。

仅使用 C++ Builder 和 C++ Lambda 的模块不需要依赖本工程。

完整语法和边界见：

`doc/iCAX-Engine/Framework/Database/Entity数据操作与语言规格文档.md`
