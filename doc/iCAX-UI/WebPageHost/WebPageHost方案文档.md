# WebPageHost 方案文档

## 1. 当前实现

代码位置：

```text
src/iCAX-UI/WebPageHost/
```

当前已实现：

- `CWebPageHostConfig`
- `CWebPageHost`
- H5 mail envelope 类型别名。
- H5 mail envelope 到 `CFrontendBridge` 的转发。
- Engine response/event 到 H5 envelope 的取出。

## 2. 内部结构

```text
CWebPageHost
  CWebPageHostConfig
    non-owning CFrontendBridge*
    WebPageRoot
  started flag
  mutex
```

`WebPageHost` 不解析产品 manifest，不持有 `ApplicationHost`，不缓存 post office。

Engine、mail channel、post office 缓存和产品/项目运行时由 `iCAX-Application` 与 `iCAX-Engine` 负责。

## 3. 宿主适配接入点

真实宿主适配器需要完成：

- 创建浏览器窗口或其他 UI 容器。
- 加载 `AppShell/index.html`。
- 注入 `window.icax`。
- 将 JS `postMail` 调到 `CWebPageHost::PostMail()`。
- 将 `PollMails()` 结果推送到 JS。
- 将 PDO lease 包装成 JS 可访问的视图。

当前 CEF 适配器位于 `src/iCAX-UI/CefWebPageHost`。

## 4. 启动方案

应用启动层先构造 `CApplication`：

```text
CApplication
  -> ApplicationHost
  -> FrontendBridge
```

然后 UI 宿主拿到 `CApplication::Frontend()`：

```cpp
iCAX::Application::CApplication app;
app.SetConfig(config);
app.Start();

iCAX::Frontend::CWebPageHost host;
iCAX::Frontend::CWebPageHostConfig hostConfig;
hostConfig.pFrontendBridge = &app.Frontend();
hostConfig.WebPageRoot = ".../AppShell";
host.SetConfig(hostConfig);
host.Start();
```

关闭顺序应先停 UI 宿主，再停 `CApplication`。

## 5. Mail 转发方案

H5 调用 `window.icax.postMail(envelope)` 后，宿主适配器将 envelope 转给 `CWebPageHost::PostMail()`。

处理流程：

```text
channelId
  -> CWebPageHost.PostMail()
  -> CFrontendBridge.PostMail()
  -> Engine channel service
  -> command handler
```

Engine 返回 response/event 后，宿主适配器调用 `PollMails()` 取出 envelope，再推送给 JS 订阅者。

## 6. Channel 注册方案

`CWebPageHost` 只转发 channel 注册请求：

- `GetApplicationChannelIDText()` 委托 `CFrontendBridge` 返回应用级入口。
- `RegisterProductChannel(productId)` 委托 `CFrontendBridge` 缓存并返回产品 channel。
- `RegisterProjectChannel(projectId)` 委托 `CFrontendBridge` 缓存并返回项目 channel。

channel 生命周期、post office 缓存和具体查找规则都在 `CFrontendBridge` 内部维护。

## 7. 宿主适配器落地要求

宿主适配器应作为 `WebPageHost` 的外层适配，不把浏览器内核细节写入 Engine，也不把 Engine 生命周期写入 UI Host。

adapter 负责：

- 创建原生窗口。
- 创建浏览器视图。
- 注入 `window.icax`。
- 处理 JS 到 C++ 的调用。
- 处理 C++ 到 JS 的事件推送。
- 管理 shared memory 到 JS 视图的生命周期。

## 8. 验收标准

构建验收：

```powershell
msbuild src\iCAX-UI\WebPageHost\WebPageHost.vcxproj /p:Configuration=Debug /p:Platform=x64
```

行为验收：

- application channel id 能通过 `FrontendBridge` 返回。
- product/project mail channel 能通过 `FrontendBridge` 注册。
- request/response mail 循环能完成。
- 停止后重复 `Stop()` 不产生副作用。
- `Stop()` 不触碰 Engine 生命周期。
