# PDORenderService 规格文档

## 定位

`CPDORenderService` 实现 `IRenderService`，并把 `RenderData` 序列化到现有 `RenderPDO` payload。

## PDO 写入接口

- `WriteMeshToPDO(ProjectID, SceneID, GeometryID, Slot)`
- `WritePolylineToPDO(ProjectID, SceneID, GeometryID, Slot)`
- `WriteToolpathToPDO(ProjectID, SceneID, GeometryID, Slot)`
- `WriteInstanceListToPDO(ProjectID, SceneID, Slot)`
- `WriteViewStatesToPDO(ProjectID, SceneID, Slot)`

这些接口不长期保存 `IPDOSlot` 或 `PDOHub` 指针。调用方每次写入时显式传入目标 slot。

## 使用样例

```cpp
auto render = project.Services().Resolve<iCAX::Render::IRenderService>();
auto pdoRender = std::dynamic_pointer_cast<iCAX::PDORenderService::CPDORenderService>(render);

auto& slot = project.PDOHub().GetSlot(
    iCAX::PDO::MakePDOID("render.mesh", "main.mesh"));

pdoRender->WriteMeshToPDO(project.GetProjectID(), 1, geometryID, slot);
```

Polyline 和 Toolpath 的写法一致，只是目标 slot 的 PDO 类型分别为 `render.polyline` 和 `render.toolpath`。

## 版本行为

写入使用 `CPDOWriteLease::TryBeginIfNewer`。当 slot 内已有不低于当前 `RenderDataVersion` 的数据时，本次写入会跳过。

## 多项目与多场景

所有接口都显式接收 `ProjectID` 和 `SceneID`。同一个服务实例可以服务多个项目，也可以在每个项目内维护多个渲染场景；任一项目关闭时调用 `DestroyProject(ProjectID)` 清掉该项目下全部场景数据。
