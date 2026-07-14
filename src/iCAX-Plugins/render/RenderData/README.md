# RenderData

`RenderData` 是渲染插件族的中立数据契约项目。

它只描述后端希望渲染系统消费的数据，包括 mesh、polyline、toolpath、实例、material、transform 和 camera；它不依赖 PDO、Three.js、WPF、Qt、OpenGL 或具体产品。

`SRenderMeshData` 只表达几何顶点、法向、颜色和索引，不携带 bounds。包围盒属于消费者运行时缓存，应由 viewport 或原生 renderer 根据几何和 transform 自行计算，避免实例复用、旋转、缩放等场景下产生语义混淆。

`SRenderMaterialData` 是资源池中的材质源数据。业务组件、机床 visual、工件实例等只保存 `MaterialResourceID`，不直接保存 diffuse/ambient/specular/emissive 等散字段。`SRenderStyleData` 是发送给 RenderService/PDO 的运行时快照，用于把材质资源翻译成前端都能理解的轻量显示提示。这里不表达 shader、贴图、PBR、GPU 参数或具体渲染器状态。

`SRenderInstanceData::nObjectID` 是 Scene 层运行期对象 ID，不是渲染服务私有 ID。当前约定直接使用 Entity ID，使 render instance、camera、collider 和 transform 可以通过同一个 ID 在前端播放器里拼装。

目录结构：

- `RenderDataTypes.h`：渲染数据结构和基础 ID/版本类型。
- `RenderData.h`：项目统一入口头。
- `RenderDataExport.h`：DLL 导出宏。
