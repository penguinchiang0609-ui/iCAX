# RenderPDO

`RenderPDO` 是渲染插件层的渲染 PDO 协议项目。

它只定义后端/前端通过 PDO 交换的固定二进制 layout，包括 mesh、polyline、toolpath、实例列表和 view state。它不定义 material、shader、renderer、GPU buffer，也不依赖 `Resources`。

`ViewState` 表达 viewport 尺寸、相机语义和矩阵。它可以由前端写给后端用于拾取/LOD，也可以由后端写给前端用于默认视角或仿真视角同步。

## 目录结构

- `RenderPDOTypes.h`：渲染 PDO 的基础类型、payload kind、渲染类别和标志位。
- `RenderPDOLayouts.h`：mesh、polyline、toolpath、instance list、view state 的二进制 layout。
- `RenderPDODecl.*`：渲染 PDO 槽声明辅助。
- `RenderPDOValidation.*`：payload header 与 offset/size 校验。
- `RenderPDO.h`：普通入口头。

## 测试

- `src/tests/icax-plugins/render/RenderPDO/RenderPDOTest/`：gtest 单元测试。
