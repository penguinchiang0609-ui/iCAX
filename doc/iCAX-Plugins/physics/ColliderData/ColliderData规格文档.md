# ColliderData 规格文档

## 定位

`ColliderData` 表达 iCAX 自己的碰撞语义，不依赖 Jolt 或其他物理引擎。

## 数据范围

- `SBoxColliderData`
- `SSphereColliderData`
- `SCapsuleColliderData`
- `STriangleMeshColliderData`
- `SConvexHullColliderData`
- `SColliderRaycastRequest`
- `SColliderRaycastHit`

## 约束

`ColliderSceneID` 和 `ColliderID` 的 0 值表示无效。`ColliderDataVersion` 使用 `uint64_t`，用于产品层判断 collider 是否需要重建或更新。

## 使用样例

```cpp
iCAX::Collider::SBoxColliderData box;
box.nOwnerObjectID = entity.ID();
box.Transform.Position = { 0.0f, 0.0f, 0.0f };
box.HalfExtent = { 0.5f, 0.5f, 0.5f };
```
