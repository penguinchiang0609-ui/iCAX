# RenderData

`RenderData` 是渲染插件族的中立数据契约项目。

它只描述后端希望渲染系统消费的数据，包括 mesh、polyline、toolpath、实例、样式、transform 和 camera；它不依赖 PDO、Three.js、WPF、Qt、OpenGL 或具体产品。

`SRenderMeshData` 只表达几何顶点、法向、颜色和索引，不携带 bounds。包围盒属于消费者运行时缓存，应由 viewport 或原生 renderer 根据几何和 transform 自行计算，避免实例复用、旋转、缩放等场景下产生语义混淆。

`SRenderInstanceData::nObjectID` 是 Scene 层运行期对象 ID，不是渲染服务私有 ID。业务对象、camera、collider 和 transform 的对应关系由 `ISceneContext::Objects()` 中的 SceneObjectRegistry 统一维护；RenderData 只消费这个 ID。

目录结构：

- `RenderDataTypes.h`：渲染数据结构和基础 ID/版本类型。
- `RenderData.h`：项目统一入口头。
- `RenderDataExport.h`：DLL 导出宏。
