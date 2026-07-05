# laser-3d-cam webpage

这里是三维线条切割 CAM 的 H5 产品页面。

目录结构：

- `entry.mjs`：产品页面入口，导出 `mountProduct` 和 `mountProject`。

页面只通过前端 SDK 的 ProductProxy/ProjectProxy/SceneProxy 与 backend 通信，不直接访问 Database 或插件内部状态。

页面职责：

- `mountProduct`：显示产品入口，并通过 ApplicationShell 创建项目 catalog。
- `mountProject`：绑定当前 ProjectProxy 和 SceneProxy，调用 `Cam.GetScene` 获取项目场景状态。
- 视口只绘制 backend 返回的 `faces/loops/edges/toolpaths`，不内置产品拓扑 mock。
- 模型导入调用 `Cam.ImportModel`，当前只接受 STEP/STP、IGS/IGES 工件路径。
- 当前 backend 使用 OCCT 导入 STEP/IGES，并返回可拾取拓扑的二维投影；页面只负责显示和拾取，不承担 CAD 解析。
- 如果未来 backend 返回 `topology.importMode=fallback-preview`，页面仍会显示后端诊断提示，但当前主路径应为 `opencascade`。
