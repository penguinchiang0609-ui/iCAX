# layout

`layout` 放置 H5 工作台的主布局能力。

Layout 是通用工作台外壳，不包含具体产品业务。产品模块可以向布局贡献工具栏、面板和主编辑视图。

## 目录结构

- `README.md`：本目录说明。
- `workbenchView.mjs`：原生 DOM 工作台视图，包含 AppBar、ProductRail、Workspace、Inspector 和 BottomDock。

## 主布局

```text
AppBar
ProductRail + WorkspaceHost + InspectorDock
BottomDock
```

`WorkspaceHost` 根据当前状态显示 ApplicationWorkspace、ProductWorkspace 或 ProjectWorkspace。
