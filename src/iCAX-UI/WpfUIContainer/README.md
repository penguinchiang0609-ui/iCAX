# WpfUIContainer

`WpfUIContainer` 是基于 WPF 的 `IUIContainer` 实现。

## 职责

- 通过 `ICAX_REGISTER_UI_CONTAINER("wpf", CWpfUIContainer)` 注册到 `CUIContainerFactory`。
- 在 native `Application.exe` 进程内启动 WPF UI 线程。
- 使用 `IFrontendBridge` 直接连接 backend `ApplicationRuntime`。
- 通过 Facade 发送 application/product/project 请求并轮询 report/response/event；Report 保留 pending 调用，Response 才结束调用。
- 为后续 native viewport 留出中心区域，便于用 `HwndHost` 嵌入 C++ 渲染窗口。

WPF 的 Facade 定时器运行在 UI Dispatcher 线程；每次轮询帧前会调用 `IFrontendBridge::RunFrontTasks()`，因此绑定 Front Task scheduler 的 continuation 始终在该 UI 线程执行。

## 边界

WPF 前端不承载大 mesh 或刀路数据。大数据应留在 C++ `ResourcePool` 和 native viewport 中，WPF 只负责面板、命令、状态和参数编辑。

## 配置

`Setting/UIContainer.Setting` 示例：

```ini
type=wpf
modulePath=WpfUIContainer.dll
startupHandshakeTimeoutMS=5000
```
