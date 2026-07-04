# laser-3d-cam webpage

这里是三维线条切割 CAM 的 H5 产品页面。

目录结构：

- `entry.mjs`：产品页面入口，导出 `mountProduct` 和 `mountProject`。

页面只通过前端 SDK 的 ProductProxy/ProjectProxy 与 backend 通信，不直接访问 Database 或插件内部状态。

页面职责：

- `mountProduct`：显示产品入口，并通过 ApplicationShell 创建项目 catalog。
- `mountProject`：绑定当前 ProjectProxy，调用 `Cam.GetScene` 获取项目场景状态。
- 视口只绘制 backend 返回的 `faces/loops/edges/toolpaths`，不内置产品拓扑 mock。
- 模型导入调用 `Cam.ImportModel`，当前只登记路径或 URI；真实 STEP 解析由 backend 插件继续扩展。
