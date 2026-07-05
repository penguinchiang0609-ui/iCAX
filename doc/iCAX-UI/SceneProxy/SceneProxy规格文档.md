# SceneProxy 规格文档

## 1. 定位

`SceneProxy` 是前端 Scene 代理层。Backend 中 Project 是项目容器，Scene 才是运行现场；因此前端业务命令、撤销重做、事件订阅和 PDO 高频数据入口都归 `SceneProxy`。

`SceneProxy` 不拥有 backend 数据，只保存：

- scene id。
- scene channel id。
- scene state 快照。
- `PDOClient`。
- 当前 Scene 的订阅取消函数。

## 2. 对外接口

构造 `SceneProxy` 时必须提供带 `sceneChannelId` 的 scene state。

方法：

- `updateState(sceneState)`：更新状态快照，并按最新 PDO 描述重建 `PDOClient`。
- `getState(options)`：通过 Scene channel 发送 `Project.GetState`，刷新并返回当前 Scene 状态。
- `execute(command, payload, options)`：向 Scene channel 发送业务命令。
- `undo(options)`：发送 `Project.Undo`。
- `redo(options)`：发送 `Project.Redo`。
- `getUndoRedoState(options)`：发送 `Project.GetUndoRedoState`。
- `subscribe(command, handler)`：订阅 Scene channel 的指定事件。
- `subscribeAll(handler)`：订阅 Scene channel 的所有事件。
- `dispose()`：释放本 Scene 的所有前端订阅并停止 mailbox client 对该 channel 的监听。

属性：

- `pdo`：Scene PDO 访问入口。

## 3. 边界

`SceneProxy` 不解释产品 payload，不直接访问 Database、Resource、Component 或 Universe。产品页面如果需要修改数据，必须发送命令到后端；如果需要读取高频状态，必须通过 PDO。

## 4. 验收要求

- Scene 命令必须返回 Promise。
- backend 主动 Scene 事件可以被订阅。
- PDO 访问入口只来自 Scene 状态，不创建全局单例。
- `SceneProxy` 层必须至少提供状态、撤销、重做和撤销重做状态查询入口。
