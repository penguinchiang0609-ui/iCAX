# Tracker legacy

本目录保留旧的 `DomainUndoRedo.*` 独立撤销重做栈实现，仅供追溯历史设计。

当前有效实现已经移入 `src/icax/framework/Database`：

- 事务由 `Database::IRepository::BeginTransaction()` 管理。
- 撤销重做由 `Database::IRepository::BeginUndoCommand()` / `Undo()` / `Redo()` 管理。
- `src/icax/framework/Tracker` 只保留旧接口兼容层，不再维护独立撤销栈。
