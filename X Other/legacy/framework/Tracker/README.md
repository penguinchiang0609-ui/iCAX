# Tracker

`Tracker` 是 framework 中的旧追踪接口兼容层。当前撤销重做、事务提交、事务回滚和快速保存日志都由 `Database` 统一维护，`Tracker` 只保留旧调用入口并转发到 `Database`。

## 目录结构

- `ITrackCoordinator.*`：创建旧追踪协调器的兼容入口。
- `IRepositoryUndoRedo.h` / `RepositoryUndoRedo.*`：旧撤销重做接口和转发实现，实际调用 `Database::IRepository`。
- `ITransaction.h` / `Transaction.*`：旧事务接口和适配实现，提交时使用 `Database::BeginTransaction()`。
- `SnapshotComponent.*`：旧快照组件定义。
- `Tracker.vcxproj` / `Tracker.vcxproj.filters`：兼容工程文件。

旧的 `DomainUndoRedo.*` 独立栈实现已移入 `X Other/legacy/modules/Tracker/`，不再参与当前工程编译。
