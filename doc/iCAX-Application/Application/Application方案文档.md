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

`CApplication` 负责拥有 `ApplicationHost` 和 `CFrontendBridge`。

`CFrontendBridge` 负责把 UI envelope 转为 Engine mail，并轮询 Engine 发给 UI 的 response/event。

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

## 3. Mail 流程

```text
UI
  -> CFrontendBridge.PostMail()
  -> Engine frontend post office
  -> ApplicationHost/ProductRuntime/Project command dispatcher
  -> response mail
  -> CFrontendBridge.PollMails()
  -> UI
```

## 4. 失败策略

`CApplication.Start()` 中如果 Engine 启动成功但 bridge attach 失败，应立即停止 Engine 并重新抛出异常。

`CFrontendBridge` 不吞业务异常。投递失败、channel 未注册、post office 无效都应在调用现场抛出。

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
