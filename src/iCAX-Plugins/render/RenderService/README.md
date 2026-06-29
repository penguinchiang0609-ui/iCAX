# RenderService

`RenderService` 是渲染插件族的服务接口项目。

它定义 project/scene 级渲染数据管理接口。接口显式携带 `ProjectID` 和 `RenderSceneID`，因此同一个服务实例也可以同时服务多个项目、多个视口、多个预览场景或仿真场景。

目录结构：

- `IRenderService.h`：渲染服务接口。
- `RenderSceneSnapshot.h`：场景快照数据。
- `RenderService.h`：项目统一入口头。
- `RenderServiceExport.h`：DLL 导出宏。
