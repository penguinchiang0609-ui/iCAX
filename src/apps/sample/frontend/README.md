# frontend

`frontend` 保留 sample 应用上一版 H5 实验代码。

新的产品前端应放在 `src/apps/<product-id>/webpage/`，并通过 `src/iCAX-UI/SDK` 接入 Application、Product、Project 三层前端运行模型。

## 目录结构

- `pages/`：完整页面。
- `panels/`：可嵌入工作台布局的功能面板。
- `components/`：应用内部复用的前端组件。
- `project/`：项目主编辑视图、项目工具栏和项目级面板。
- `commands/`：sample 产品前端命令。
- `entry`：后续放置产品前端入口，对应 `ProductDefinition.FrontendEntry`。
