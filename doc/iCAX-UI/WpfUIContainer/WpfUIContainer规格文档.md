# WpfUIContainer 规格文档

## 1. 定位

`WpfUIContainer` 是可选真实前端容器。当前默认真实前端仍是 CEF/H5；WPF 通过同一个 `IUIContainer` 契约接入 backend。

它通过 C++/CLI 实现 native `IUIContainer`，在同一进程内启动 WPF UI 线程，并通过 `IFrontendBridge` 与 backend `ApplicationHost` 交互。

## 2. 职责

- 注册容器类型 `wpf`。
- 启动 WPF 主窗口。
- 获取 application channel id。
- 发送 application/product/project mailbox 命令。
- 轮询 backend response/event 并更新 UI。
- 预留 native viewport 区域，后续通过 `HwndHost` 嵌入 C++ 渲染窗口。

## 3. 非职责

- 不解析具体产品业务数据。
- 不承载大 mesh、刀路或 BRep 数据。
- 不实现渲染内核。
- 不直接拥有 `ApplicationHost`。

大数据应留在 C++ `ResourcePool`、`Project` 和 native viewport 中。WPF 前端只处理命令、参数、状态和轻量 UI 数据。

## 4. 配置

```ini
type=wpf
modulePath=WpfUIContainer.dll
startupHandshakeTimeoutMS=5000
```

`modulePath` 可省略，`UIContainerFactory` 会按 `wpf` 默认解析为 `WpfUIContainer.dll`。

## 5. 启动语义

```text
Application.exe
  -> CApplication.Start()
  -> ApplicationHost.Start()
  -> FrontendBridge.Attach()
  -> CUIContainerFactory.Create(type=wpf)
  -> CWpfUIContainer.Start()
  -> WPF UI thread
```

WPF UI 线程必须是 STA。

## 6. Mailbox 语义

WPF 容器直接调用 `IFrontendBridge`：

- `GetApplicationChannelIDText()`
- `PostMail()`
- `PollMails()`

当前窗口提供 application 级基础操作：

- `App.GetState`
- `App.ListProducts`
- `App.StartProduct`
- `App.OpenProjectFile`

## 7. 验收要求

- `WpfUIContainer.vcxproj` 可独立构建。
- `Application.exe` 可加载 `WpfUIContainer.dll`。
- WPF 主窗口可启动并显示 application channel。
- 启动后能收到 `App.GetState`/`App.ListProducts` 响应。
- 关闭 WPF 窗口后 Application 正常停止 backend。
