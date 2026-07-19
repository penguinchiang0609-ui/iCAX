# PDORenderService

`PDORenderService` 是 `IRenderService` 的 PDO 实现。

它按 `ProjectID + RenderSceneID` 保存中立 `RenderData`，并提供把 mesh、polyline、toolpath、instance list、transform 和 camera 写入 RenderPDO slot 的方法。服务不长期保存 `PDOHub` 或 `IPDOSlot` 指针，Scene 关闭时不会留下悬挂引用。

Transform 是独立 PDO 组件，只表达 `transformId + localToWorld`。Render object 使用 `objectId` 作为 transform ID，camera 使用 `cameraId` 作为 transform ID。前端按相同 ID 把 instance/camera 与 transform 拼装到同一个逻辑物体上。

相机采用单向 PDO slot：

- `Camera`：`kDirection2External`，由后端写给前端。
- `Transform`：`kDirection2External`，由后端写给前端，表达 object/camera 等组件的位姿。

前端不控制相机，不向 `render.camera` 或 `render.transform` 写入相机位姿。用户输入后续走 InputPDO 或 Facade，由后端业务更新 Camera/Transform，再由 `PDORenderService` 发布。

目录结构：

- `PDORenderService.h/.cpp`：PDO 渲染服务实现。
- `PDORenderServiceExport.h`：DLL 导出宏。
