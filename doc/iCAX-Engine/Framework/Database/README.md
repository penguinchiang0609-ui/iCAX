# Database

`Database` 是 iCAX Engine Framework 层的 EC 数据模型项目。

它负责管理 `Repository`、`Domain`、`Entity`、`Component`、字段元数据、变更事件、组件版本和派生字段缓存。

## 目录结构

```text
Database/
  Database规格文档.md
    面向上层调用者，描述 Database 提供什么能力、如何使用、行为边界是什么。

  Database方案文档.md
    面向实现维护者，描述当前代码结构、事件链路、派生字段失效和测试方案。

  README.md
    本目录说明。
```
