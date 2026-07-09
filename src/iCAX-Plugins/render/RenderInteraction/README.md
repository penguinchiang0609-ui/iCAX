# RenderInteraction

`RenderInteraction` 放置通用渲染切面能力。它只表达渲染对象、位姿和相机本体，不表达工件、机床、刀路等业务身份，也不承载键鼠漫游策略。

`CCameraComponent` 只表达相机投影参数和当前 viewport 归属；相机位置、姿态和缩放由同 Entity 上的 `CRenderTransformComponent` 承载。`CameraBehaviour` 只负责把相机参数发布到 `IRenderService`，不读取输入，也不修改 Transform。场景漫游这类高频交互由更高层的 `CameraNavigation` 插件提供。

`CRenderInstanceComponent` 表达“当前 Entity 要显示哪个几何资源”。资源可以是已经生成好的 `SRenderMeshData`、`SRenderPolylineData`、`SRenderToolpathData`，也可以是中立 `GeometryData::BRepModel`；当资源是 BRep 时，RenderInstance Behaviour 从 BRep 的 triangulation 派生成 RenderMesh 后发布到 `IRenderService`。这属于 Render 切面的资源适配能力，不属于 CAM/Workpiece 业务组件。

## 目录结构

- `RenderInteractionComponents.h`：通用 RenderInstance、RenderTransform 与 Camera 组件。
- `RenderInstanceBehaviour.cpp`：RenderInstance 的通用发布行为；负责把 Entity 的几何显示切面同步到 `IRenderService`。
- `RenderTransformBehaviour.cpp`：RenderTransform 的通用发布行为；负责把 Entity 的位姿切面同步到 `IRenderService`。
- `CameraBehaviour.cpp`：基于 CameraComponent 发布 camera 参数，不处理键鼠输入。
- `RenderInteraction.h`：统一入口头。
- `RenderInteractionExport.h`：DLL 导出宏。
- `pch.*` / `framework.h` / `dllmain.cpp`：Windows DLL 工程基础文件。
