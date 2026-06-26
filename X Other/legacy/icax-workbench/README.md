# icax-workbench

> Legacy：该目录是上一版 H5 工作台实验入口。新的正式前端框架位于 `src/iCAX-UI/`，由 `WebPageHost + WebPageShell + FrontendSDK` 组成。具体产品前端位于仓库根目录 `products/<product-id>/webpage/`。

`icax-workbench` 是 H5 前端通用工作台壳。它不表示某个具体业务应用，而是承载应用页面、布局、路由、通用 UI、Mailbox 和 PDO 前端封装。

它与 backend 的层级保持一致：

```text
FrontendHost       -> ApplicationHost
ProductWorkspace   -> ProductRuntime
ProjectWorkspace   -> Project
```

H5 前端不直接访问 C++ 对象，也不持有 Repository 或 ResourcePool。它通过宿主 bridge 获取 application mailbox，再按产品和项目逐级建立通信。

## 目录结构

- `shell/`：前端应用壳和启动入口。
- `bridge/`：宿主 bridge 抽象，屏蔽 WebView2、Qt WebEngine、CEF 或浏览器调试差异。
- `router/`：前端路由和应用页面注册。
- `layout/`：主布局、停靠区、菜单和面板容器。
- `state/`：H5 前端运行状态，保存应用、产品、项目和 UI 快照。
- `product/`：产品前端模块加载、产品工作区协议和产品级 UI 扩展。
- `project/`：项目工作区容器、项目级 UI 协议和项目命令入口。
- `common-ui/`：跨应用复用的基础 UI 组件。
- `theme/`：主题变量、图标、密度和视觉规范。
- `mailbox/`：前端侧 Mailbox 封装。
- `pdo/`：前端侧 PDO 订阅、缓存和数据绑定。

## 启动流程

```text
H5 bootstrap
  -> connect bridge
  -> get application mail id
  -> App.GetState / App.ListProducts
  -> render ApplicationWorkspace
  -> App.StartProduct or App.OpenProjectFile
  -> load product frontend entry
  -> render ProductWorkspace / ProjectWorkspace
```

双击项目文件时，前端只把路径交给 `App.OpenProjectFile`。产品类型由 backend 根据项目文件 magic 唯一识别，前端不提供产品选择绕过。

## 运行方式

`index.html` 是当前工作台入口。开发调试时可以直接在 Visual Studio 中将 `WorkbenchDevServer` 设为启动项目。

也可以在本目录手动启动：

```powershell
.\run-workbench.ps1
```

如果本机安装了 Node，也可以运行：

```powershell
node dev-server.mjs
```

如果宿主没有注入 `window.icaxBridge`，前端会自动使用 `MockIcaxBridge`。mock bridge 会延迟发送 response mail，用来模拟前端线程和后端线程之间的异步通信。
