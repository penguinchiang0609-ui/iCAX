# CefWebPageHost 规格文档

## 1. 定位

`CefWebPageHost` 是 iCAX-UI 的 CEF 宿主适配层。

它不替代 `WebPageHost`。`WebPageHost` 负责 H5 adapter 到 `FrontendBridge` 的核心连接，`CefWebPageHost` 负责 CEF runtime、浏览器窗口、JS bridge 注入和 Engine 邮件推送。

## 2. 职责

- 初始化和关闭 CEF runtime。
- 提供 CEF 子进程入口。
- 创建 CEF browser。
- 加载 `AppShell/index.html`。
- 在 V8 context 创建阶段注入 `window.icax`。
- 把 JS `getApplicationChannelId/registerProductChannel/registerProjectChannel/postMail` 转发给 `CWebPageHost`。
- 轮询 `CWebPageHost::PollMails()`，并把 Engine response/event 推送到 JS。

## 3. 不负责

- 不拥有 Engine。
- 不启动或停止 `ApplicationHost`。
- 不实现业务命令。
- 不解释 Variant payload。
- 不解释 PDO 二进制数据。
- 不直接修改 repository/component/resource。
- 不把具体产品写死到宿主。

## 4. CEF SDK 要求

构建 `CefWebPageHost` 前必须准备 CEF binary distribution，并设置：

- `ICAX_CEF_ROOT`
- `ICAX_CEF_LIB_DIR`
- `ICAX_CEF_WRAPPER_LIB_DIR`

其中 `libcef_dll_wrapper.lib` 必须由同一份 CEF SDK 编译得到。

## 5. 启动顺序

宿主 EXE 应按如下顺序：

```text
CCefWebPageHost::ExecuteSubProcess(hInstance)
  -> 若返回 >= 0，直接退出当前进程

CApplication.Start()
  -> Engine ApplicationHost.Start()
  -> FrontendBridge.Attach(ApplicationHost)

CCefWebPageHost::InitializeRuntime(runtimeConfig)
CCefWebPageHost.Start(hostConfig)
  -> CWebPageHost.Start()
  -> CefBrowserHost::CreateBrowser()
  -> renderer 注入 window.icax
  -> AppShell bootstrap
```

## 6. JS Bridge

`window.icax` 至少提供：

- `getApplicationChannelId()`
- `registerProductChannel(productId)`
- `registerProjectChannel(projectId)`
- `postMail(mail)`
- `subscribeMail(channelId, handler)`

`subscribeMail` 在 JS 侧维护订阅表。native 轮询到 Engine mail 后调用 `window.icax.__dispatchMail(mail)` 分发。

## 7. 验收要求

- CEF 依赖不进入 `WebPageHost` 核心工程。
- CEF SDK 缺失时构建给出明确错误。
- JS 可通过 `AppProxy` 连到 application channel。
- mailbox request/response 可通过 CEF bridge 完成闭环。
- `CefWebPageHost.Stop()` 不停止 Engine。
