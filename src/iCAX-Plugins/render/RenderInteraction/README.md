# RenderInteraction

`RenderInteraction` 放置通用显示资源适配能力。它不表达工件、机床、刀路等业务身份，也不维护后端渲染现场。

`CCameraComponent` 只是一种可选的领域数据；TubeOne 当前的取景、标准视图和交互相机由前端 viewport 本地维护。

`CRenderInstanceComponent` 表达“当前 Entity 要显示哪个几何资源、使用哪个材质资源”。`RenderResourceAdapter` 可把中立 `GeometryData::BRepModel`、三角网格和材质转换为资源池中的前端格式。EntityView 再把组件字段投影成 View 快照；前端只按快照中的 URL 取资源并渲染。

## 目录结构

- `RenderInteractionComponents.h`：通用 RenderInstance 与 Camera 组件。
- `RenderResourceAdapter.h/.cpp`：把后端几何和材质转换成资源池中的前端资源。
- `RenderInteractionComponents.h`：View 可以投影的显示引用组件。
- `RenderInteraction.h`：统一入口头。
- `RenderInteractionExport.h`：DLL 导出宏。
- `pch.*` / `framework.h` / `dllmain.cpp`：Windows DLL 工程基础文件。
