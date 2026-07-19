# WpfUIContainer 方案文档

## 1. 源码位置

```text
src/iCAX-UI/WpfUIContainer/
  WpfUIContainer.vcxproj
  WpfUIContainer.h
  WpfUIContainer.cpp
```

## 2. 工程形态

`WpfUIContainer` 是 C++/CLI mixed-mode DLL。

外层：

```text
CWpfUIContainer : IUIContainer
```

内层：

```text
CWpfRuntime
  -> STA Thread
  -> System.Windows.Application
  -> CWpfMainWindow
  -> DispatcherTimer PollFacadeFrames()
```

这样可以保留 native `Application.exe` 和 `IFrontendBridge`，同时使用 WPF 构建桌面 UI。

## 3. 消息流

```text
WPF Button
  -> CWpfRuntime.SendApplicationCommand()
  -> IFrontendBridge.PostFacadeFrame()
  -> ApplicationRuntime Facade
  -> backend command handler
  -> frontend Facade endpoint
  -> IFrontendBridge.PollFacadeFrames()
  -> WPF DispatcherTimer
  -> CWpfMainWindow
```

命令码仍使用与 C++/H5 一致的 FNV-1a 主/子命令 hash。

## 4. UI 布局

当前主窗口分为：

- 顶部 toolbar：刷新、产品列表、启动产品、打开项目、退出。
- 左侧 application 面板：显示 application channel 和当前项目入口。
- 中心 viewport 区域：预留 native viewport。
- 右侧 Facade/payload 面板：显示后端响应和 payload。
- 底部 status bar：显示连接状态和操作状态。

## 5. Native Viewport

中心区域后续应替换为 `HwndHost`：

```text
WPF Panel
  -> HwndHost
    -> C++ NativeViewport HWND
      -> OCC/OpenGL/DirectX renderer
      -> ResourcePool / Project data
```

mesh、刀路等大数据不进入 WPF binding，也不通过 CLR 拷贝。

## 6. 构建

```powershell
msbuild src/iCAX-UI/WpfUIContainer/WpfUIContainer.vcxproj `
  /p:Configuration=Release /p:Platform=x64
```

工程依赖：

- `UIContainer`
- .NET Framework 4.8
- WPF assemblies: `PresentationFramework`、`PresentationCore`、`WindowsBase`、`System.Xaml`

## 7. 当前限制

- 当前窗口只实现 application 级基础命令。
- product/project 级专用面板后续应在具体产品或公共 WPF shell 中继续扩展。
- native viewport 尚未接入，只预留布局边界。
