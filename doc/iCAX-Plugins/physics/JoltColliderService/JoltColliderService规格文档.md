# JoltColliderService 规格文档

## 定位

`CJoltColliderService` 实现 `IColliderService`，底层使用 `JoltPhysics` 插件。

## 当前支持

- box collider
- sphere collider
- triangle mesh collider
- transform 读写
- step
- raycast
- 多 project、多 scene 隔离

`ColliderData` 中预留的 capsule 和 convex hull 尚未暴露到 `IColliderService` 当前接口。

## 使用样例

```cpp
iCAX::JoltColliderService::CJoltColliderService service;
service.OnLoad();
service.CreateScene(projectID, 1);

auto colliderID = service.CreateBoxCollider(projectID, 1, box);
auto hit = service.Raycast(projectID, 1, ray);
```
