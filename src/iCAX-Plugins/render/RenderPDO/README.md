# RenderPDO

`RenderPDO` 是渲染插件层的渲染 PDO 协议项目。

它只定义后端/前端通过 PDO 交换的固定二进制 layout，包括 mesh、polyline、toolpath、实例列表和 camera。它不定义 material、shader、renderer、GPU buffer，也不依赖 `Resources`。

`render.mesh` payload 不携带 bounds，只传输几何本体：顶点、法向、顶点色和索引。viewport 或原生 renderer 应按几何数据和 `render.transform` 自行计算运行时包围盒；碰撞检测也不复用 render bounds，而应走独立 collider 数据。

`Camera` 表达后端控制的渲染相机。一个 camera payload 可以包含多个相机，并通过 `activeCameraId` 指定当前相机。相机位姿不在 `render.camera` 内，而是通过相同 ID 的 `render.transform` 组件表达。前端只能读取 `render.camera` 与 `render.transform` 并按 ID 拼装，不写回、不自行改变相机。

`Transform` 是 EC 风格的独立组件，只包含 transform ID 与 local-to-world 矩阵。Scene object、camera、collider 等数据使用相同 ID 时，即视为挂载在同一个逻辑物体上。这个 ID 由后端 SceneObjectRegistry 分配和反查，不由前端生成。

viewport 的窗口尺寸、DPI、输入事件不属于 `Camera`。这些数据后续分别走 UI 容器或 InputPDO。

## 目录结构

- `RenderPDOTypes.h`：渲染 PDO 的基础类型、payload kind、渲染类别和标志位。
- `RenderPDOLayouts.h`：mesh、polyline、toolpath、instance list、camera 的二进制 layout。
- `RenderPDODecl.*`：渲染 PDO 槽声明辅助。
- `RenderPDOValidation.*`：payload header 与 offset/size 校验。
- `RenderPDO.h`：普通入口头。

## 测试

- `src/tests/icax-plugins/render/RenderPDO/RenderPDOTest/`：gtest 单元测试。
