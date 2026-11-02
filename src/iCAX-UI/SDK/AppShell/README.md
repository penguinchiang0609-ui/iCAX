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


