# Application 规格文档

## 1. 定位

`Application` 是进程级容器，拥有 Engine 和通用 UI 桥接。

`Application` 不绑定具体 UI 技术。H5/CEF、Qt、WPF 都应通过同一个 `CFrontendBridge` 与 Engine 通信。

## 2. 对外对象

- `CApplication`
- `CApplicationConfig`
- `CFrontendBridge`
- `CFrontendMailEnvelope`

## 3. 生命周期

```text
CApplication.SetConfig(config)
CApplication.Start()
  -> ApplicationHost.SetConfig(config.EngineConfig)
  -> ApplicationHost.Start()
  -> FrontendBridge.Attach(ApplicationHost)

UIHost.Start(application.Frontend())

CApplication.Stop()
  -> FrontendBridge.Detach()
  -> ApplicationHost.Stop()
```

## 4. FrontendBridge

`CFrontendBridge` 提供：

- `GetApplicationChannelIDText()`
- `RegisterProductChannel(productId)`
- `RegisterProjectChannel(projectId)`
- `PostMail(envelope)`
- `PollMails()`
- `SetMailHandler(handler)`

这些能力不属于 H5，也不属于 CEF。Qt 或 WPF UI 也应复用同一套桥。

## 5. 约束

- UI 宿主不创建 `ApplicationHost`。
- UI 宿主不停止 Engine。
- 产品 manifest 必须在进入 `CApplicationConfig::EngineConfig` 前完成解析。
- `RegisterProductChannel` 只缓存 post office，不启动产品。
- `RegisterProjectChannel` 只缓存 post office，不打开项目。
