# Database

`Database` 是 iCAX Engine Framework 层的 EC 数据模型项目。

它负责管理 `Repository`、`Entity`、`Component`、字段元数据、变更事件、组件版本和派生字段缓存。组件版本统一采用 `uint64_t`，可直接作为 PDO `dataVersion`。

## 目录结构

```text
Database/
  Database规格文档.md
    面向上层调用者，描述 Database 提供什么能力、如何使用、行为边界是什么。

  Database方案文档.md
    面向实现维护者，描述当前代码结构、事件链路、派生字段失效和测试方案。

  EntityWhere与EntityView规格文档.md
    描述 Where、一次性 Query、Entity 成员物化视图、参数、引用、增量维护和职责边界。

  Entity数据操作与语言规格文档.md
    描述结构化 Update/Delete、C++ Lambda、Lambda 字符串、EntitySQL 与工程依赖边界。

  README.md
    本目录说明。
```
