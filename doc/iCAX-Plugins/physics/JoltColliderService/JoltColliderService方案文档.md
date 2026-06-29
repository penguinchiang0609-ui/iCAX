# JoltColliderService 方案文档

## 内部结构

```text
CJoltColliderService
    ProjectID
        CJoltPhysicsSceneCatalog
            ColliderSceneID
                CJoltPhysicsScene
                    JPH::PhysicsSystem
```

每个 `CJoltPhysicsScene` 拥有独立 Jolt `PhysicsSystem`，因此主视口、预览视口和仿真视口之间不会共享 collider、body 或 raycast 状态。

## 映射规则

- `ColliderSceneID` 映射到 `PhysicsSceneID`。
- `ColliderID` 映射到 `PhysicsBodyID`。
- `ColliderObjectID` 透传为 Jolt body 的 owner object ID。

## 关闭清理

`DestroyScene` 只清理指定 scene。`DestroyProject` 清理一个 project 下所有 Jolt scene catalog。`OnUnload` 清理全部 project 数据。
