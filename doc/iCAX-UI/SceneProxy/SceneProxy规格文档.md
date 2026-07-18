# SceneProxy 规格文档

## 1. 定位

`SceneProxy` 是前端 Scene 代理层。Project 是项目容器，Scene 是运行现场；Scene 范围的 Facade 调用、事件订阅和 PDO 入口归 `SceneProxy`。

## 2. 对外接口

- `updateState(sceneState)`：更新 Scene 状态与 PDO 描述；
- `getState(options)`：调用 `Project.GetState`；
- `invoke(facadeMethod, payload, options)`：通过 Scene channel 调用 Facade 方法；
- `undo(options)`：调用 `Project.Undo`；
- `redo(options)`：调用 `Project.Redo`；
- `getUndoRedoState(options)`：调用 `Project.GetUndoRedoState`；
- `subscribe(facadeMember, handler)`：订阅指定事件；
- `subscribeAll(handler)`：订阅全部事件；
- `dispose()`：释放订阅并停止 channel。

`pdo` 属性提供 Scene PDO 访问入口。

## 3. 边界

SceneProxy 不解释参数和结果，不直接访问 Database、Resource、Component 或 Universe。实例 ID 始终放在参数中，例如：

```js
sceneProxy.invoke("Machine.Jog", { machineId, axis, delta })
```

SceneProxy 不生成 `MachineID#AxisNo#Jog` 一类实例地址。
