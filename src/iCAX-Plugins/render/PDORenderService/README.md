# PDORenderService

`PDORenderService` 是 `IRenderService` 的 PDO 实现。

它按 `ProjectID + RenderSceneID` 保存中立 `RenderData`，并提供把 mesh、polyline、toolpath、instance list 和 view state 写入 RenderPDO slot 的方法。服务不长期保存 `PDOHub` 或 `IPDOSlot` 指针，项目关闭时不会留下悬挂引用。

目录结构：

- `PDORenderService.h/.cpp`：PDO 渲染服务实现。
- `PDORenderServiceExport.h`：DLL 导出宏。
