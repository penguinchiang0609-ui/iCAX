# CameraNavigation

`CameraNavigation` 是可选的场景交互插件，负责把 `InputService` 中的高频键鼠输入转换为相机 Entity 上的 `CRenderTransformComponent` 修改。

它不发布 RenderPDO，也不解释业务对象。相机参数发布仍由 `RenderInteraction::CameraBehaviour` 负责，Transform 发布仍由 `RenderInteraction::RenderTransformBehaviour` 负责。

默认键位：

- `W/S/A/D` 或方向键：前后左右移动。
- `Q/E` 或 `PageDown/PageUp`：下降/上升。
- `Shift`：临时加速。
- 鼠标右键拖拽：旋转视角。
- 鼠标滚轮：沿当前视线方向移动。

## 目录结构

- `CameraNavigationComponents.h`：`CCameraNavigationComponent`，表达相机导航参数。
- `CameraNavigationBehaviour.cpp`：`CCameraNavigationBehaviour`，每帧读取 InputPDO 状态并修改 Transform。
- `CameraNavigation.h`：统一入口头。
- `CameraNavigationExport.h`：DLL 导出宏。
- `pch.*` / `framework.h` / `dllmain.cpp`：Windows DLL 工程基础文件。

