# Database

`Database` 是 framework 中的对象模型工程，负责 Repository、Entity、Component、元数据注册、组件属性访问、批量变更、事务、撤销还原和快速保存操作日志。

## 目录结构

当前目录直接放置 `Database.vcxproj` 及其源码文件。

- `ChangeSet.*`：Repository 内部净变更摘要与合并逻辑，不作为公开事件契约。
- `ModifyFilter.*`：正式写入前的统一准入过滤，调用 checker，失败时返回错误信息且不落地。
- `OperationLog.*`：有序操作批次、事务回滚、撤销还原和快速保存日志；操作顺序是日志事实来源。
- `RepositoryUndoRedoHistory.*`：外挂式撤销重做记录器和历史栈；`Repository` 之外的 EC 对象不保存历史状态。

公开入口保持语义化：`BeginBatch` / `EndBatch` 用于普通批量命令，`BeginLoadBaseline` / `EndLoadBaseline` 用于加载项目基线，`BeginTransaction` / `CommitTransaction(transaction)` / `CancelTransaction(transaction)` 管理事务生命周期，事务对象只收集操作意图，`BeginUndoCommand` 定义撤销重做步骤边界。

组件 `Enable` / `Disable` 属于组件自身基础状态，参与事务、撤销还原和快速保存；它不属于 `PropertySet`，也不通过字段 meta 声明。

组件版本类型为 `uint64_t`。版本由 Repository 的版本表维护，脱离 Repository 的组件版本为 0；上层可以直接把 `Component::Version()` 或 `Repository::GetComponentVersion()` 的返回值作为 PDO `dataVersion`，用于跳过未变化数据的高频序列化。

项目文件 schema/version/migration 不属于 Database。它应由 File 模块负责，并在写入 Repository 基线数据之前完成。快速保存日志只回放当前程序按当前格式写出的操作批次，不负责文件升级或迁移。打开和回放日志时，调用方必须传入日志 `magic` 与 `version`；Database 只写入头部并严格校验，不内置产品级常量。

组件字段修改、组件添加和组件移除在事件触发前先经过 `ModifyFilter`。正式写入 API 提供 `bool + out string` 形式表达业务失败；失败不会触发事件、日志或撤销记录。

`ChangeSet` 只表达提交后的净变更摘要，用于 Repository 内部版本更新、派生字段失效和是否发布批量事件的判断；它会合并字段修改，不作为撤销、重做或快速保存的回放依据，也不会通过 `RepositoryEventArgs` 暴露给上层。
