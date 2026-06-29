# physics

`physics` 存放后端物理和碰撞相关插件。

物理插件运行在 backend 内部，不作为独立进程或独立端点。它应直接消费 backend 的项目数据、组件数据和资源数据，并把结果写回 backend 或通过 PDO 暴露给前端。

## 目录结构

- `ColliderData/`：中立碰撞数据契约，不绑定具体物理引擎。
- `ColliderService/`：碰撞服务接口，按 ProjectID + SceneID 管理碰撞现场。
- `JoltPhysics/`：基于 Jolt Physics 的刚体、碰撞检测和拾取插件。
- `JoltColliderService/`：基于 JoltPhysics 的 ColliderService 实现。
