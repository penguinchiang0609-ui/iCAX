# render

`render` 存放渲染和显示数据相关插件。

这类项目可以定义 viewport 消费的数据、渲染适配器或具体 renderer backend，但不进入 engine/framework，避免框架被某条 UI 或渲染路线绑定。

## 目录结构

- `RenderData/`：中立渲染数据契约，不绑定具体渲染路线。
- `RenderInteraction/`：把 BRep、mesh 和 material 转为资源池中的前端资源，不管理场景、不绑定具体产品。

场景中“哪些对象可见、对象使用什么几何/材质及其位姿”只能由 `View` 投影表达。前端读取 View 快照中的资源 URL，再从资源池取得几何和材质并自行渲染。后端不维护第二套渲染现场。
