# ProjectProxy

`ProjectProxy` 是前端项目代理模块，表达某个 backend project 的前端会话。

## 目录结构

- `ProjectProxy.mjs`：前端 project 代理对象。它持有 project channel、项目状态快照和 PDO client。

## 标准能力

- `getState()`：刷新项目状态快照。
- `execute(command, payload, options)`：执行项目级命令。
- `undo(options)`：发送 `Project.Undo`。
- `redo(options)`：发送 `Project.Redo`。
- `getUndoRedoState(options)`：发送 `Project.GetUndoRedoState`。
- `subscribe(command, handler)` / `subscribeAll(handler)`：订阅项目级事件。
- `pdo`：项目 PDO 高频数据入口。

## 边界

本模块不持有 repository、resource pool 或 ECS 数据。项目数据归 backend project 所有；前端通过 mailbox 执行项目级命令，通过 PDO 读取高频快照。
