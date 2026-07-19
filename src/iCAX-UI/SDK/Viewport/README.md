# Viewport

`Viewport` 是 H5 前端 SDK 内置的三维视口层。

它负责把 backend 发布的 RenderPDO 数据解释成具体前端 renderer 可消费的对象。产品页面不直接解析 RenderPDO，也不直接持有 renderer 细节；产品页面只创建 SDK 暴露的 viewport 并绑定当前 `SceneProxy`。

相机由后端通过 `render.camera` PDO 发布，位姿由同 ID 的 `render.transform` PDO 发布。Three.js 视口只读取相机、Transform 和 scene object，并按 ID 拼装 renderer camera 或 scene object，不通过鼠标滚轮或拖拽自行改变相机，也不向后端写回相机数据。用户输入后续走 InputPDO 或 Facade，由后端决定如何更新相机和 Transform。

默认 H5 视口只负责采集键鼠状态并写入 `input.state/default`。后端可选的 `CameraNavigationBehaviour` 消费这些输入：`W/S/A/D` 或方向键用于前后左右移动，`Q/E` 或 `PageDown/PageUp` 用于下降/上升，按住 `Shift` 加速，按住鼠标右键拖拽旋转视角，滚轮沿当前视线方向移动。前端不直接修改相机。

## 边界

- `RenderPDO`：后端/前端共享的二进制渲染协议；几何、实例、Transform、相机分开传输。
- `ThreeRenderViewport`：H5 默认三维视口实现，使用 SDK 内置 Three.js。
- 产品 `webpage`：只负责布局、Facade 调用和把 viewport 放进页面。

`render.mesh` 不携带 bounds。Viewport 应从 position buffer 和对象 transform 计算自己的运行时包围盒/包围球，用于取景、裁剪和显示缓存；不要把 bounds 视为后端 RenderPDO 契约的一部分。

Viewport 中的 `objectId`、`cameraId` 和 `transformId` 来自后端 SceneObjectRegistry。前端可以把这些 ID 原样回传给后端用于拾取或交互反馈，但不负责解释它们对应哪个 Entity。

Three.js 是 H5 前端内置选择，不进入 Engine/Framework，也不进入具体产品业务插件。以后如果需要 WebGPU、Babylon、Qt 或 WPF，只要提供同样的 viewport 实现即可。

## 目录结构

- `renderPDO.mjs`：RenderPDO 常量、事件解析和二进制 payload 解析。
- `threeViewport.mjs`：基于 Three.js 的 RenderPDO viewport。
- `index.mjs`：Viewport 模块导出。
