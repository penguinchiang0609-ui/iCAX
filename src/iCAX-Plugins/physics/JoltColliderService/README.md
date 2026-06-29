# JoltColliderService

`JoltColliderService` 是 `IColliderService` 的 Jolt 实现。

它按 `ProjectID` 保存独立 `CJoltPhysicsSceneCatalog`，每个 catalog 下可以创建多个 scene。一个 scene 对应一套独立 Jolt `PhysicsSystem`，主视口、预览视口和仿真视口不会共享 body/collider/raycast 状态。

目录结构：

- `JoltColliderService.h/.cpp`：Jolt 碰撞服务实现。
- `JoltColliderServiceExport.h`：DLL 导出宏。
