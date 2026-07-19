# Application 规格文档

## 1. 定位

`Application` 是进程级可执行程序，拥有 Engine 和通用 UI 桥接。

`Application` 不绑定具体 UI 技术。H5/CEF、Qt、WPF 都应通过同一个 `CFrontendBridge` 与 Engine 通信；UI adapter 只依赖 `IFrontendBridge` 接口，不反向链接 `Application.exe`。

## 2. 对外对象

- `CApplication`
- `CApplicationConfig`
- `CFrontendBridge`

`CFrontendFacadeFrame` 和 `IFrontendBridge` 契约定义在 `iCAX-UI/UIContainer/FrontendBridgeContract.h`，用于隔离 UI adapter 和 Application 可执行程序。

## 3. 生命周期

```text
CApplication.SetConfig(config)
CApplication.Start()
  -> ApplicationRuntime.SetConfig(config.RuntimeConfig)
  -> ApplicationRuntime.Start()
  -> FrontendBridge.Attach(ApplicationRuntime)

Load Setting/UIContainer.Setting
CUIContainerFactory.Create(uiConfig)
IUIContainer.Start()

CApplication.Stop()
  -> FrontendBridge.Detach()
  -> ApplicationRuntime.Stop()
```

## 4. FrontendBridge

`CFrontendBridge` 提供：

- `GetApplicationChannelIDText()`
- `RegisterProductChannel(productId)`
- `RegisterSceneChannel(projectId, sceneId)`
- `PostFacadeFrame(frame)`
- `PollFacadeFrames()`
- `SetFacadeFrameHandler(handler)`

这些能力不属于 H5，也不属于 CEF。Qt 或 WPF UI 也应复用同一套桥。

## 5. 约束

- UI 宿主不创建 `ApplicationRuntime`。
- UI 宿主不停止 Engine。
- UI 宿主不链接 `Application.exe`，只通过 `IFrontendBridge` 指针回调 Application 持有的桥实现。
- 产品 manifest 必须在进入 `CApplicationConfig::RuntimeConfig` 前完成解析。
- `RegisterProductChannel` 只缓存 Facade endpoint，不启动产品。
- `RegisterSceneChannel` 只缓存 Facade endpoint，不打开项目或 Scene。

## 6. 默认启动

没有 `Setting/UIContainer.Setting` 时，`Application.exe` 使用内置 `headless` 容器完成一次 ApplicationProxy 握手：

```text
getApplicationChannelId
postFacadeFrame(App.GetState Request)
poll response
```

这用于验证后端 `ApplicationRuntime` 与前端代理边界已经接通。
