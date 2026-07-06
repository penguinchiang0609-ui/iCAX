# laser-3d-cam webpage

这里是三维线条切割 CAM 的 H5 产品页面。

目录结构：

- `entry.mjs`：产品页面入口，导出 `mountProduct` 和 `mountProject`。

页面只通过前端 SDK 的 ProductProxy/ProjectProxy/SceneProxy 与 backend 通信，不直接访问 Database 或插件内部状态。

页面职责：

- `mountProduct`：显示产品入口，并通过 ApplicationShell 创建项目 catalog。
- `mountProject`：绑定当前 ProjectProxy 和 SceneProxy，调用 `Cam.GetScene` 获取项目场景状态。
- 如果当前 Scene 启用了 PDO，视口使用 `iCAX-UI/SDK` 内置的 `ThreeRenderViewport` 显示 RenderPDO 数据。
- 如果当前 Scene 未启用 PDO，视口使用 backend 返回的 `faces/loops/edges/toolpaths` 做 SVG 后备预览。
- 模型导入调用 `Cam.ImportModel`，当前只接受 STEP/STP、IGS/IGES 工件路径；CEF 宿主提供 `openFileDialog` 时页面可以直接选择模型文件。
- 当前 backend 使用 OCCT 导入 STEP/IGES，并返回可拾取拓扑的二维投影；页面只负责显示和拾取，不承担 CAD 解析。
- Three 视口的点击命中会使用 RenderPDO mesh 的 `faceIndex` 和 CAM topology 中的 `triangleStart/triangleCount` 映射到 `face` 拾取，再通过 `Cam.PickTopology` 回写后端选择。当前 RenderPDO 尚未携带 edge/loop 拾取映射，因此 edge/loop 精确拾取仍走 SVG 后备视图或后续碰撞/拓扑拾取服务。
- 如果未来 backend 返回 `topology.importMode=fallback-preview`，页面仍会显示后端诊断提示，但当前主路径应为 `opencascade`。
