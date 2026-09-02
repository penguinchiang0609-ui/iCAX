# Viewport

`Viewport` 是 H5 前端 SDK 内置的三维视口层。

它负责消费 View 快照，并把快照引用的资源池数据转换成具体前端 renderer 对象。产品页面只负责选择 View、创建 viewport，并把 View 快照交给 viewport。

Three.js 视口在前端本地维护相机。取景、标准视图、鼠标旋转和平移不创建后端相机对象，也不改变 View；View 只决定场景内容。

键鼠和触控输入由 viewport 本地处理，不进入后端。碰撞 PDO 只保留给机床碰撞等高频计算结果，不能携带场景渲染数据，也不能绕过 View 决定对象可见性。

## 边界

- `View`：唯一的场景投影和可见性契约；一个 View 可以组合多个 EntityView Source，快照包含对象属性及资源 URL。
- `Resources`：几何与材质的版本化数据通道。
- `ThreeRenderViewport`：H5 默认三维视口实现，使用 SDK 内置 Three.js。
- 产品 `webpage`：只负责布局、SDO 调用和把 viewport 放进页面。

几何资源不携带运行时 bounds。Viewport 从 position buffer 和对象 transform 计算包围盒/包围球，用于取景、裁剪和显示缓存。

Viewport 中的对象 ID 直接来自 View 行的 Entity ID。前端可以把它原样回传用于拾取或交互反馈。

Three.js 是 H5 前端内置选择，不进入 Engine/Framework，也不进入具体产品业务插件。以后如果需要 WebGPU、Babylon、Qt 或 WPF，只要提供同样的 viewport 实现即可。

## 目录结构

- `renderResource.mjs`：资源池几何和材质格式的解析与加载。
- `threeViewport.mjs`：基于 Three.js 的 View viewport。
- `index.mjs`：Viewport 模块导出。
