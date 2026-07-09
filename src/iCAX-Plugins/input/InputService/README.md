# InputService

`InputService` 是后端输入状态服务。它从 Scene 级 `input.state/default` PDO 读取前端键鼠快照，并按 `ProjectID + SceneID + ViewportID` 维护 `GetKey`、`GetKeyDown`、`GetKeyUp` 和鼠标状态。

## 目录结构

- `InputService.h`：服务接口和输入快照结构。
- `InputService.cpp`：默认服务实现和自动注册。
- `InputServiceExport.h`：DLL 导出宏。
- `pch.*` / `framework.h` / `dllmain.cpp`：Windows DLL 工程基础文件。

