# RenderData

`RenderData` 是渲染插件族的中立数据契约项目。

它只描述后端希望渲染系统消费的数据，包括 mesh、polyline、toolpath、实例、样式和视图状态；它不依赖 PDO、Three.js、WPF、Qt、OpenGL 或具体产品。

目录结构：

- `RenderDataTypes.h`：渲染数据结构和基础 ID/版本类型。
- `RenderData.h`：项目统一入口头。
- `RenderDataExport.h`：DLL 导出宏。
