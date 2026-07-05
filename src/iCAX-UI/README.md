# iCAX-UI

`iCAX-UI` 是 iCAX 的前端框架目录。它不是某个具体产品的页面，也不拥有 Engine。

当前默认前端是 H5/CEF：`UIContainer + CefUIContainer + AppShell + AppProxy + ProductProxy + ProjectProxy + SceneProxy + UI + SDK`。但底层 `UIContainer` 契约必须保持技术无关，WPF/QT 等前端只要实现 `IUIContainer` 并通过 `IFrontendBridge` 交互，就可以替换 H5/CEF。

前端结构与 backend 保持同样的概念分层，但前端层统一使用 `Proxy` 后缀，避免和后端实体混淆：

- `AppProxy`：应用入口代理，负责连接宿主、列出产品、启动产品、按文件打开项目。
- `ProductProxy`：产品会话代理，负责产品级命令、产品级事件、项目入口和产品 webpage 模块加载。
- `ProjectProxy`：项目容器代理，负责项目状态、Scene 列表和主 Scene 入口。
- `SceneProxy`：场景会话代理，负责 Scene 命令、Scene 事件、撤销重做和 PDO 访问。

Mail、PDO、Bridge 是 `SDK` 内部通信能力，UI 是公共界面能力。前端不拥有 repository、resource pool、ECS 等业务数据；这些数据仍由 Engine 管理，前端只持有状态快照、通道和视图状态。

## 目录结构

- `UIContainer/`：UI 容器公共契约、静态注册工厂和内置 headless 容器。
- `CefUIContainer/`：CEF 版 `IUIContainer` 实现，负责 CEF runtime、浏览器窗口、JS bridge 注入和 Engine 邮件推送。
- `WpfUIContainer/`：WPF 版 `IUIContainer` 可选实现，不是当前默认前端。
- `SDK/AppShell/`：SDK 自带的唯一 H5 页面入口。它承载应用首页、产品工作区、项目工作区和产品前端模块挂载点。
- `AppProxy/`：前端应用层对象，封装 application channel 和应用级命令。
- `ProductProxy/`：前端产品层对象，封装 product channel、产品状态、项目入口和产品 webpage 模块加载。
- `ProjectProxy/`：前端项目层对象，封装项目状态、Scene 列表和主 Scene 入口。
- `SceneProxy/`：前端场景层对象，封装 scene channel、Scene 状态、命令和 PDO client。
- `UI/`：公共 UI 基础工具、主题和后续公共组件。
- `SDK/`：给 `AppShell` 和产品 `webpage` 模块使用的统一导出门面，内部包含 Bridge、Mailbox、PDO。

## 与产品目录的关系

具体产品不放在 `iCAX-UI` 下面。产品是独立交付单元，统一放在 `src/apps/<product-id>/`：

```text
src/apps/<product-id>/
  product.manifest.json
  backend/
  webpage/
  protocol/
```

`iCAX-Application` 读取产品 manifest 并构造 Engine 产品定义；`AppShell` 根据 manifest 中的 `webpage.entry` 动态加载产品前端模块。

## 运行模型

```text
AppShell
  -> SDK
    -> AppProxy / ProductProxy / ProjectProxy / SceneProxy
    -> Bridge / Mailbox / PDO
    -> UI
    -> window.icax
      -> CefUIContainer / WpfUIContainer / QtUIContainer
        -> IFrontendBridge
          -> iCAX-Application FrontendBridge
          -> Engine ApplicationHost
            -> ProductRuntime
              -> Project
                -> Scene
```

H5 页面只看到普通 JS 对象和 Promise。`PostOffice`、`VariantSerializer`、`PDO` header 和共享内存租约都由 `iCAX-Application`、具体 `IUIContainer` 与 `SDK` 隐藏。
