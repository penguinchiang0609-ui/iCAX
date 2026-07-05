# UIContainer 方案文档

## 1. 结构

```text
Application.exe
  -> CApplication
    -> ApplicationHost
    -> CFrontendBridge : IFrontendBridge
  -> CUIContainerFactory
    -> IUIContainer(headless/cef/wpf/qt)
```

`UIContainer` 公共项目只承载接口、工厂和内置 headless 容器。

## 2. 启动流程

```text
CApplication.Start()
  -> ApplicationHost.Start()
  -> FrontendBridge.Attach(ApplicationHost)

Load Setting/UIContainer.Setting
CUIContainerFactory.Create(config)
  -> load module if needed
  -> construct IUIContainer
IUIContainer.Start()
```

关闭顺序：

```text
IUIContainer.Stop()
CApplication.Stop()
  -> FrontendBridge.Detach()
  -> ApplicationHost.Stop()
```

## 3. 动态容器

具体 UI 容器以 DLL 形式扩展。DLL 静态注册自己支持的类型：

```cpp
ICAX_REGISTER_UI_CONTAINER("cef", iCAX::Frontend::Cef::CCefUIContainer)
```

配置指定：

```ini
type=cef
modulePath=CefUIContainer.dll
webPageRoot=.../src/iCAX-UI/SDK/AppShell
```

WPF/QT 使用同一流程：

```ini
type=wpf
modulePath=WpfUIContainer.dll
```

```ini
type=qt
modulePath=QtUIContainer.dll
```

工厂只负责创建实例，不理解产品、不调度业务、不维护 Engine 生命周期。

## 4. Mailbox 边界

具体 UI 技术只通过 `IFrontendBridge` 进入 backend。H5/CEF 容器会把 JS 的 `window.icax` 调用转成该接口；WPF/QT 容器可以直接封装这些调用：

- `getApplicationChannelId()`
- `registerProductChannel(productId)`
- `registerSceneChannel(projectId, sceneId)`
- `postMail(mail)`
- `subscribeMail(channelId, handler)`

具体 UI 容器负责把这些调用转成 `IFrontendBridge` 调用。H5 前端 Promise 由 `iCAX-UI/SDK/Mailbox` 管理；WPF/QT 前端可以用自己的 async/future 机制包装同一 mailbox request/response 语义。

## 5. 验收

当前验收命令：

```powershell
msbuild src\iCAX.slnx /p:Configuration=Debug /p:Platform=x64 /m:1 /v:minimal
Start-Process src\x64\Debug\Application.exe -Wait -PassThru
```

如果配置 `type=headless`，容器应完成 `App.GetState` 握手并返回退出码 0。如果配置 `type=cef`，应打开 CEF 窗口、加载 AppShell，并完成 application channel 初始化。如果配置 `type=wpf` 或 `type=qt`，对应容器也必须完成同样的 application channel 初始化。
