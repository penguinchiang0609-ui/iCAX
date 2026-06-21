# Database

`Database` 是 framework 中的对象模型工程，负责 Repository、Entity、Component、元数据注册、组件属性访问、批量变更、事务、撤销还原和快速保存操作日志。

## 目录结构

当前目录直接放置 `Database.vcxproj` 及其源码文件。

- `ChangeSet.*`：批量变更模型与合并逻辑。
- `OperationLog.*`：有序操作批次、事务回滚、撤销还原和快速保存日志；操作顺序是日志事实来源。
- `RepositoryHistory.*`：外挂式撤销还原记录器和历史栈；`Repository` 之外的 EC 对象不保存历史状态。

`ChangeSet` 只表达提交后的净变更摘要，用于 `kBatchChanged` 事件、版本和派生字段失效；它会合并字段修改，不作为撤销、重做或快速保存的回放依据。
