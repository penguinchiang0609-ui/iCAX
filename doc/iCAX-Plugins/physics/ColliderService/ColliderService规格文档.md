# ColliderService 规格文档

## 定位

`IColliderService` 管理 project/scene 级碰撞现场，提供 collider 生命周期、raycast 和 step。

## 核心接口

- `CreateScene(ProjectID, SceneID)`
- `DestroyScene(ProjectID, SceneID)`
- `DestroyProject(ProjectID)`
- `CreateBoxCollider`
- `CreateSphereCollider`
- `CreateTriangleMeshCollider`
- `DestroyCollider`
- `SetColliderTransform`
- `Raycast`
- `Step`

## 使用样例

```cpp
auto collider = project.Services().Resolve<iCAX::Collider::IColliderService>();

collider->CreateScene(project.GetProjectID(), 1);
auto id = collider->CreateBoxCollider(project.GetProjectID(), 1, box);

iCAX::Collider::SColliderRaycastRequest ray;
ray.Origin = { 0.0f, 0.0f, 10.0f };
ray.Direction = { 0.0f, 0.0f, -1.0f };
auto hit = collider->Raycast(project.GetProjectID(), 1, ray);
```

## 多场景

同一个 project 可以创建主视口、预览、仿真、测量等多个 collider scene。不同 project 可以复用相同 scene id。
