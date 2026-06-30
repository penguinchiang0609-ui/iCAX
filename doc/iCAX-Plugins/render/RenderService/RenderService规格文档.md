# RenderService 规格文档

## 定位

`IRenderService` 管理 project/scene 级渲染现场。它只面向 `RenderData`，不绑定 PDO 或具体 renderer。

## 核心接口

- `CreateScene(ProjectID, SceneID)`
- `DestroyScene(ProjectID, SceneID)`
- `DestroyProject(ProjectID)`
- `Update(ApplicationContext, ProductContext, ProjectContext, DeltaTime, TotalTime)`
- `UpsertMesh / UpsertPolyline / UpsertToolpath`
- `SetInstances`
- `SetViewStates`
- `GetSceneSnapshot`

`ProjectID` 不能为 nil，`SceneID` 不能为 0。

`Update` 是渲染服务的每帧入口，也可以理解为 RenderService 的 `OnTick`：

- PDO 实现：把当前 scene 的 RenderData 写入项目 PDOHub，必要时动态分配或释放 slot，随后由 Project 帧循环在 `PostSwapPDO` 阶段交换出去。
- 原生渲染实现：刷新屏幕、提交渲染命令或同步 renderer 内部状态。
- 调用位置：应在项目线程中调用，通常由 Behaviour、Project frame handler 或产品运行时调度。

## 使用样例

```cpp
auto render = project.Services().Resolve<iCAX::Render::IRenderService>();

render->CreateScene(project.GetProjectID(), 1);
render->UpsertMesh(project.GetProjectID(), 1, mesh);
render->SetInstances(project.GetProjectID(), 1, { instance }, {}, instanceVersion);

render->Update(applicationContext, productContext, project, deltaTime, totalTime);

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
