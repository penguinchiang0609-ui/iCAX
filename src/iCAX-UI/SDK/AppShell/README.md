# AppShell

`AppShell` 是 iCAX 前端唯一 H5 页面入口。`UIContainer` 永远加载这里的 `index.html`，产品前端通过 manifest 中的 `webpage.entry` 作为模块挂载进来。

## 目录结构

- `index.html`：唯一页面入口。
- `app/`：启动、状态、布局渲染和产品模块挂载。
- `theme/`：工作台基础样式。
- `dev-server.mjs`：无构建开发服务器，方便浏览器调试。

## 运行方式

```powershell
node src/iCAX-UI/SDK/AppShell/dev-server.mjs
```

没有真实宿主注入时，`SDK` 会使用 mock bridge，仍可验证 AppProxy/ProductProxy/ProjectProxy 的前端流程。

宿主可在页面 URL 使用 `?product=<已注册的 productId>` 选择启动产品。默认仍为 `icax.tube-designer`；例如已加载五轴 CAM 清单时使用 `?product=icax.five-axis-cam`。产品必须出现在 `App.GetState` 清单中，未知 ID 拒绝启动，网页模块路径始终由产品清单提供。

## 底部面板扩展

产品可导出 `getBottomDockDefinition(context)`，返回 `{ panels: [{ id, title }], defaultTabId, initialHeight, minHeight }`。`AppShell` 为产品面板自动追加唯一的内建 `logs`（日志）页；`logs` 是保留 ID，不由产品重复实现。未声明产品面板时，原来的日志区和默认高度保持不变。选项卡模式默认初始高度为96、最小高度为72，可由声明覆盖；用户拖动高度按场景保留。

产品同时导出 `mountBottomDockPanel(context, panelId)`，在 `context.mount` 中挂载或局部更新内容；`context.projectMount` 指向原项目表面。回调可以同步返回，也可返回 Promise。产品自行管理面板内事件和暂存输入，不应在这个回调里再次请求底部刷新。

`context.bottomDock` 提供 `activeTabId`、`getPanelMount(id)`、`selectTab(id)` 和 `refresh()`；`context.actions.refreshBottomDock()` 与 `refresh()` 相同，只更新底部，不重建项目或整个应用。旧项目上下文不能取得或改变新项目的面板。

同一所有者与面板目录下，普通外壳刷新保留底部 DOM；切换页签仅设置 `hidden` 和 ARIA 状态，不移动输入节点、不自动聚焦命令区，日志使用同一个 `.log-list` 并保留滚动位置。产品的挂载回调也必须遵循局部更新要求，避免覆盖用户后续输入、选区和滚动。

公共自动化入口是 `[data-bottom-dock-tab="<id>"]`、`[data-bottom-dock-panel="<id>"]` 和既有 `[data-bottom-dock-resize]`。选项卡支持左右方向键、Home和End。


