# render

`render` 存放渲染和显示数据相关插件。

这类项目可以定义 viewport 消费的数据、渲染适配器或具体 renderer backend，但不进入 engine/framework，避免框架被某条 UI 或渲染路线绑定。

## 目录结构

- `RenderData/`：中立渲染数据契约，不绑定具体渲染路线。
- `RenderService/`：渲染服务接口，按 ProjectID + SceneID 管理渲染现场。
- `RenderPDO/`：渲染 PDO 协议插件，表达后端通过 PDO 发布给前方的 mesh、polyline、toolpath 和实例列表 layout。
- `PDORenderService/`：基于 PDO 的 RenderService 实现，将 RenderData 写入 RenderPDO slot。
