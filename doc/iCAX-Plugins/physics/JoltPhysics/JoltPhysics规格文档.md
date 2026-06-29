# JoltPhysics 规格文档

## 1. 定位

`JoltPhysics` 是后端物理插件，负责刚体、碰撞查询和拾取。它不作为独立进程，也不作为通过 PDO 对接主程序的外部端点。

```text
Project / Behaviour / Product Service
  -> JoltPhysicsSceneCatalog
    -> Jolt PhysicsSystem
```

PDO 只用于 UI 边界，例如前方写入视口状态，后端写出显示或拾取结果。

## 2. 场景模型

- `CJoltPhysicsSceneCatalog` 管理多个物理场景。
- `CJoltPhysicsScene` 对应一套独立 Jolt `PhysicsSystem`。
- 多个显示窗口、多个 viewport、导入预览和临时检查场景可以分别创建独立 scene。
- 不同 scene 的 body、collider、raycast 和 step 不共享状态。
- catalog 由产品或项目运行时持有，不是全局单例。

## 3. 当前能力

- 创建 box body。
- 创建 sphere body。
- 创建 triangle mesh body。
- 销毁 body。
- 设置和读取 body transform。
- 设置和读取 gravity。
- 推进物理 step。
- 执行 raycast，并返回命中的 body、业务对象 ID、命中点、表面法线和距离。

## 4. 使用示例

```cpp
iCAX::JoltPhysics::CJoltPhysicsSceneCatalog catalog;
auto& scene = catalog.CreateScene(1);

iCAX::JoltPhysics::SBoxBodyDesc box;
box.nOwnerObjectID = 1001;
box.HalfExtent = { 10.0f, 5.0f, 2.0f };
auto bodyID = scene.CreateBoxBody(box);

iCAX::JoltPhysics::SPhysicsRaycastRequest ray;
ray.Origin = { 0.0f, 0.0f, 100.0f };
ray.Direction = { 0.0f, 0.0f, -1.0f };
auto hit = scene.Raycast(ray);
if (hit.bHit)
{
    // hit.nOwnerObjectID 对应产品层对象、Entity 或显示对象。
}
```

三角网格 collider 示例：

```cpp
std::vector<iCAX::JoltPhysics::SPhysicsVec3> vertices = LoadPreviewMeshVertices();
std::vector<uint32_t> indices = LoadPreviewMeshIndices();

iCAX::JoltPhysics::STriangleMeshBodyDesc mesh;
mesh.nOwnerObjectID = modelObjectID;
mesh.pVertices = vertices.data();
mesh.nVertexCount = static_cast<uint32_t>(vertices.size());
mesh.pIndices = indices.data();
mesh.nIndexCount = static_cast<uint32_t>(indices.size());
scene.CreateTriangleMeshBody(mesh);
```

## 5. 与 PDO 的关系

物理插件内部不通过 PDO 和 backend 主体通信。推荐关系是：

- 前方通过 `render.view_state` 或后续 `interaction.pointer` 提供 viewport 和输入状态。
- 后端 Behaviour 或产品服务读取项目数据、资源和输入状态，更新 physics scene。
- 后端执行 raycast 或碰撞查询。
- 结果写回 Repository、命令响应，或通过后续 `interaction.hit_result` PDO 发给前方。

## 6. 边界

不属于 `JoltPhysics`：

- 渲染。
- shader/material。
- UI 事件分发。
- 文件格式解析。
- ResourcePool 管理。
- RenderPDO layout。
- 流体和柔体形变。

## 7. 验收要求

- 源码位于 `src/iCAX-Plugins/physics/JoltPhysics/`。
- `iCAX-Engine/framework` 不依赖 Jolt。
- 主解决方案中可见该项目，但缺少 Jolt SDK 时默认不参与整解构建。
- 准备 Jolt SDK 后，`JoltPhysics.vcxproj` 能独立构建。
- 准备 Jolt SDK 后，`JoltPhysicsTest.vcxproj` 能独立构建并通过。
