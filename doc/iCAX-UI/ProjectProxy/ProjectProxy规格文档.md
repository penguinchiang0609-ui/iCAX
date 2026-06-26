# ProjectProxy 规格文档

## 1. 定位

`ProjectProxy` 是前端项目代理层。它表达某个 backend project 的前端会话。

前端 project proxy 不拥有 repository、resource pool、ECS 或项目文件数据，只保存：

- project id。
- project channel id。
- project state 快照。
- `PDOClient`。

## 2. 对外接口

构造 `ProjectProxy` 时必须提供带 `projectChannelId` 的 project state。

方法：

- `updateState(projectState)`：更新项目状态快照，并按最新 PDO 描述重建 `PDOClient`。
- `getState()`：发送 `Project.GetState`，刷新并返回项目状态。
- `execute(command, payload, options)`：向 project channel 发送项目级命令。
- `undo(options)`：发送 `Project.Undo`。
- `redo(options)`：发送 `Project.Redo`。
- `getUndoRedoState(options)`：发送 `Project.GetUndoRedoState`。
- `subscribe(command, handler)`：订阅 project channel 的指定事件。
- `subscribeAll(handler)`：订阅 project channel 的所有事件。

属性：

- `pdo`：项目 PDO 访问入口。

## 3. PDO 边界

`ProjectProxy` 只提供 PDO client，不解释 payload。PDO 的 `typeName + instanceName`、版本和二进制布局由产品协议定义。

## 4. 验收要求

- 项目级命令必须返回 Promise。
- backend 主动项目事件可以被订阅。
- PDO 访问入口只来自项目状态，不创建全局单例。
- `ProjectProxy` 层必须至少提供状态、撤销、重做和撤销重做状态查询的标准入口。
- 项目文件保存依赖具体产品/文件模块，不作为 `ProjectProxy` 前端公共层的固定命令。
