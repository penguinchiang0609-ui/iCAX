# Database

`Database` 是 framework 中的对象模型工程，负责实体、组件、领域、仓库、元数据注册、组件属性访问、批量变更、事务、撤销还原和快速保存操作日志。

## 目录结构

当前目录直接放置 `Database.vcxproj` 及其源码文件。

- `ChangeSet.*`：批量变更模型与合并逻辑。
- `ChangeLog.*`：ChangeSet 过滤、反向变更、序列化和追加式操作日志。
- `RepositoryHistory.*`：外挂式撤销还原记录器和历史栈；`Domain`、`Entity`、`Component` 不保存历史状态。
