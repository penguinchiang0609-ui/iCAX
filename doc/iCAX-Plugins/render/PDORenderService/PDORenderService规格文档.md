# PDORenderService 规格文档

## 定位

`CPDORenderService` 实现 `IRenderService`，用于 H5 或其他外部前端路线。它把 `RenderData` 序列化为 `RenderPDO` payload，并写入当前项目的 `PDOHub`。

`PDORenderService` 不负责渲染屏幕。它只负责把后端渲染数据同步到项目 PDO，并通过项目 mailbox 告知前端 slot 的分配、释放和移动。

## 核心接口

- `Update(ApplicationContext, ProductContext, ProjectContext, DeltaTime, TotalTime)`
- `MakeGeometryPDOID(ProjectID, SceneID, GeometryKind, GeometryID)`
- `MakeObjectPDOID(ProjectID, SceneID, ObjectID)`
- `MakeViewStatePDOID(ProjectID, SceneID)`
- `NotifyPDODefragBegin(ProjectContext)`
- `NotifyPDOSlotMoved(ProjectContext, PDOID)`
- `NotifyPDODefragEnd(ProjectContext)`
- `WriteMeshToPDO(ProjectID, SceneID, GeometryID, Slot)`
- `WritePolylineToPDO(ProjectID, SceneID, GeometryID, Slot)`
- `WriteToolpathToPDO(ProjectID, SceneID, GeometryID, Slot)`
- `WriteInstanceListToPDO(ProjectID, SceneID, Slot)`
- `WriteObjectToPDO(ProjectID, SceneID, ObjectID, Slot)`
- `WriteViewStatesToPDO(ProjectID, SceneID, Slot)`

产品常规帧循环应调用 `Update`。`WriteXToPDO` 是低层白盒接口，主要用于测试、诊断或特殊手动输出。

## 分配规则

`Update` 每帧从 `ProjectContext.PDOHub()` 获取当前项目的动态 PDOHub，并按当前 scene 数据自动分配或释放 slot。

- 一个 mesh、polyline、toolpath 几何各自占用一个 geometry slot。
- 一个 render object 占用一个 object slot。该 slot 使用 `InstanceList` payload，但只包含这个对象自己的 instance 和 style。
- 一个 scene 最多占用一个 view-state slot。
- 默认不把多个对象打包进同一个 slot。只有未来显式提供 `BatchAddObject` 这类 API 时，才允许按调用语义合并。

前端必须使用 `PDOID` 作为 slot 身份，并在 shared memory arena 中按 `PDOID` 解析当前 offset。前端不得长期缓存 offset。

## 邮件事件

`PDORenderService` 通过项目后端邮局发送事件，前端从项目前端邮局接收。payload 是 UTF-8 JSON 文本。所有 64 位 ID 都以字符串形式写入，避免 JS `Number` 精度问题。

事件码：

- `kPDORenderSlotAllocatedEvent`：slot 已分配。
- `kPDORenderSlotFreedEvent`：slot 已释放。
- `kPDORenderSlotMovedEvent`：slot 位置或容量发生变化，`PDOID` 不变。
- `kPDORenderDefragBeginEvent`：PDO arena 开始整理碎片。
- `kPDORenderDefragEndEvent`：PDO arena 碎片整理结束。

典型事件 payload：

```json
{
  "event": "SlotAllocated",
  "projectId": "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx",
  "channelId": "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx",
  "sceneId": "1",
  "slotRole": "Object",
  "payloadKind": "render.instance_list",
  "pdoId": "123456789",
  "geometryId": "88",
  "objectId": "100",
  "payloadCapacity": "128"
}
```

碎片整理时，发送顺序应为：

```text
DefragBegin
SlotMoved...  // 可选，发生移动的 slot 才发送
DefragEnd
```

前端收到 `DefragBegin` 后应暂停直接使用旧 offset；收到 `DefragEnd` 后按 `PDOID` 重新解析 arena 中的 slot。

## 使用样例

```cpp
auto render = project.Services().Resolve<iCAX::Render::IRenderService>();

render->CreateScene(project.GetProjectID(), 1);
render->UpsertMesh(project.GetProjectID(), 1, mesh);

iCAX::Render::SRenderInstanceData instance;
instance.nObjectID = 100;
instance.nGeometryID = mesh.nGeometryID;
instance.eGeometryKind = iCAX::Render::ERenderGeometryKind::Mesh;
render->SetInstances(project.GetProjectID(), 1, { instance }, {}, instanceVersion);

render->Update(applicationContext, productContext, project, deltaTime, totalTime);
project.PDOHub().SwapOutSlot();
```

如果上层需要提前知道某个对象的 PDOID：

```cpp
auto objectPDOID =
    iCAX::PDORenderService::CPDORenderService::MakeObjectPDOID(
        project.GetProjectID(), 1, instance.nObjectID);
```

## 版本行为

写入使用 `CPDOWriteLease::TryBeginIfNewer`。当 slot 内已有不低于当前 `RenderDataVersion` 的数据时，本次写入会跳过。

几何数据使用自身 `nDataVersion`。对象 slot 当前使用 scene 的 `nInstanceDataVersion`。视图状态使用 scene 的 `nViewDataVersion`。
