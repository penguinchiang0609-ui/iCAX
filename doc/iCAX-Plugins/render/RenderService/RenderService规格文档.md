# RenderService 规格文档

## 定位

`IRenderService` 管理 project/scene 级渲染现场。它只面向 `RenderData`，不绑定 PDO 或具体 renderer。

## 核心接口

- `CreateScene(ProjectID, SceneID)`
- `DestroyScene(ProjectID, SceneID)`
- `DestroyProject(ProjectID)`
- `UpsertMesh / UpsertPolyline / UpsertToolpath`
- `SetInstances`
- `SetViewStates`
- `GetSceneSnapshot`

`ProjectID` 不能为 nil，`SceneID` 不能为 0。

## 使用样例

```cpp
auto render = project.Services().Resolve<iCAX::Render::IRenderService>();

render->CreateScene(project.GetProjectID(), 1);
render->UpsertMesh(project.GetProjectID(), 1, mesh);
render->SetInstances(project.GetProjectID(), 1, { instance }, {}, instanceVersion);

auto snapshot = render->GetSceneSnapshot(project.GetProjectID(), 1);
```

## 多场景约定

同一个 project 下可以有多个 scene：

```text
1: MainViewport
2: ImportPreview
3: Simulation
4: Measurement
```

同一个 `SceneID` 可以在不同 project 中重复使用，因为隔离键是 `ProjectID + SceneID`。
