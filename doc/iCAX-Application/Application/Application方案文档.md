# Application 方案文档

## 1. 模块结构

```text
src/iCAX-Application/Application/
  Main.cpp
  Application.h
  Application.cpp
  FrontendBridge.h
  FrontendBridge.cpp
```

`Application.vcxproj` 生成 `Application.exe`，不是动态库。

`CApplication` 负责拥有 `ApplicationRuntime` 和 `CFrontendBridge`。

`CFrontendBridge` 负责在 UI frame 与 Engine `CFacadeFrame` 之间转换，并双向传递 Request、Report、Response 和 Event。

## 2. 与 UI 的关系

H5/CEF UI：

```text
CApplication
  -> CFrontendBridge
  -> IFrontendBridge
  -> CUIContainerFactory
  -> IUIContainer
    -> CCefUIContainer / HeadlessUIContainer
```

Qt UI：

```text
CApplication
  -> CFrontendBridge
  -> IFrontendBridge
  -> QtHost
```

因此具体 UI 容器不再是 Engine 宿主，也不链接 `Application.exe`，只是通过 `IFrontendBridge` 连接后端。

## 3. Facade 调用流程

```text
UI
  -> CFrontendBridge.PostFacadeFrame()
  -> Engine frontend Facade endpoint
  -> ApplicationRuntime/ProductRuntime/Project Facade invoker
  -> response frame
  -> CFrontendBridge.PollFacadeFrames()
  -> UI
```

反向调用复用相同流程：Engine 通过 endpoint 发送 Request，前端公开的 Facade 异步处理后返回 Response。C++ 调用方得到 Task，H5 调用方得到 Promise；continuation 不在 frame 收取/分发线程执行。

## 4. 失败策略

`CApplication.Start()` 中如果 Engine 启动成功但 bridge attach 失败，应立即停止 Engine 并重新抛出异常。

`CFrontendBridge` 不吞业务异常。投递失败、channel 未注册、Facade endpoint 无效都应在调用现场抛出。

## 5. UI 容器配置

`Main.cpp` 从以下位置读取 UI 容器配置：

```text
Setting/UIContainer.Setting
UIContainer.Setting
```

默认值：

```ini
type=headless
startupHandshakeTimeoutMS=5000
```

CEF 示例：

```ini
type=cef
modulePath=CefUIContainer.dll
webPageRoot=.../src/iCAX-UI/SDK/AppShell
```
