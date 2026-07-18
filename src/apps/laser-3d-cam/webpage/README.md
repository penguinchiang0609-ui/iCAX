# laser-3d-cam webpage

这里是三维线条切割 CAM 的 H5 产品页面。

目录结构：

- `entry.mjs`：产品页面入口，负责产品挂载、项目挂载、主布局组装和命令协调；不承载各大区的面板细节。
- `layout/`：页面通用视图片段，如面板、进度遮罩、选择摘要。
- `ribbon/`：产品 ribbon 定义、tab 归一化、命令标题查询。
- `state/`：前端项目 view state 仓库和 scene payload selector。这里的函数只查询/整理前端状态，不发命令、不渲染 UI。
- `machine/`：机床定义大区，包括机床定义导入/实例化 action、机床定义列表、机床实例列表、机床场景树和机床参数摘要；不承载作业、刀路、工件逻辑。
- `workpiece/`：工件编辑大区，包括工件导入 action、工件列表、模型检查和 SVG 后备拓扑；不承载刀路逻辑。
- `machining/`：加工大区，包括刀路/作业 action、作业现场状态、作业机床选择、排序规划和输出入口。
- `toolpath/`：刀路视图片段，包括刀路列表和 SVG 后备刀路叠加。
- `view/`：视图大区的显示对象和视图参数面板。
- `automation/`：CDP/UI smoke 使用的 `window.__icaxLaser3DCAM` 自动化诊断入口。正常产品 UI 不直接依赖这里的具体实现。
- `styles/`：产品页面 CSS 和样式加载器。CSS 不再内嵌到 `entry.mjs` 的字符串中。
- `utils/`：HTML 转义、数字格式化等通用前端小工具。

页面只通过前端 SDK 的 ProductProxy/ProjectProxy/SceneProxy 与 backend 通信，不直接访问 Database 或插件内部状态。

页面职责：

- `mountProduct`：显示产品入口，并通过 ApplicationShell 创建项目 catalog。
- `mountProject`：绑定当前 ProjectProxy 和 SceneProxy；页面按需要分别调用 `MachineDefinition.List`、`Machine.List`、`Job.Get`、`Selection.Get` 等产品 Facade 方法查询状态，不使用大而杂的统一主场景入口。
- Ribbon 采用 `机床定义 / 工件编辑 / 加工 / 视图`。前两个大区是资源准备入口；`加工` 大区操作正式主 Scene。
- 如果当前 Scene 启用了 PDO，视口使用 `iCAX-UI/SDK` 内置的 `ThreeRenderViewport` 显示 RenderPDO 数据。
- 如果当前 Scene 未启用 PDO，视口使用 backend 返回的 `faces/loops/edges/toolpaths` 做 SVG 后备预览。
- 机床定义导入分两步：先在导入按钮处按需调用 `Cam.MachineDefinition.GetSupportedFormats` 读取当前产品支持的定义格式，再调用 `Cam.MachineDefinition.Import` 把源文件目录托管到产品数据区。实例化时调用 `Cam.Machine.Instantiate` 低频解析托管源文件，并展开为主 Scene 中的机床 Entity；导入本身不写项目资源库。
- 模型导入分两步：先调用 `Cam.WorkpieceModel.Import` 生成 STEP/STP、IGS/IGES 对应的 BRep/Topology 资源，再调用 `Cam.Workpiece.Instantiate` 创建工件 Entity；CEF 宿主提供 `openFileDialog` 时页面可以直接选择模型文件。
- 当前 backend 使用 OCCT 导入 STEP/IGES，并返回可拾取拓扑的二维投影；页面只负责显示和拾取，不承担 CAD 解析。
- Three 视口的点击命中会使用 RenderPDO mesh 的 `faceIndex` 和 CAM topology 中的 `triangleStart/triangleCount` 映射到 `face` 拾取，再通过 `Cam.Selection.PickTopology` 回写后端选择。当前 RenderPDO 尚未携带 edge/loop 拾取映射，因此 edge/loop 精确拾取仍走 SVG 后备视图或后续碰撞/拓扑拾取服务。
- 工件 CAD 修复、机床定义编辑后续应作为独立 EditScene/工具会话接入；主 Scene 只接收最终资源和组件引用更新。
- 如果未来 backend 返回 `topology.importMode=fallback-preview`，页面仍会显示后端诊断提示，但当前主路径应为 `opencascade`。
