# Webpage

`webpage` 保存平面激光 CAM 的产品前端模块。

它不是独立 HTML 页面，而是被 `src/iCAX-UI/SDK/AppShell/index.html` 动态加载的 ESM 模块。

## 目录结构

- `entry.mjs`：产品前端入口。
- `pages/`：产品级页面。
- `panels/`：产品面板。
- `viewport/`：产品视口和渲染绑定。

模块导出 `mountProduct(context)` 和 `mountProject(context)`。`context` 由 `AppShell` 提供，包含当前产品、项目、运行时动作和挂载节点。


