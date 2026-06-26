# Application 方案文档

## 1. 模块结构

```text
src/iCAX-Application/Application/
  Application.h
  Application.cpp
  FrontendBridge.h
  FrontendBridge.cpp
```

`CApplication` 负责拥有 `ApplicationHost` 和 `CFrontendBridge`。

`CFrontendBridge` 负责把 UI envelope 转为 Engine mail，并轮询 Engine 发给 UI 的 response/event。

## 2. 与 UI 的关系

H5/CEF UI：

```text
CApplication
  -> CFrontendBridge
  -> CWebPageHost
  -> CCefWebPageHost
```

Qt UI：

```text
CApplication
  -> CFrontendBridge
  -> QtHost
```

因此 `WebPageHost` 不再是 Engine 宿主，只是 H5 侧的 bridge adapter。

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
