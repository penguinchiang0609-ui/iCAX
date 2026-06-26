# WebPageHost 规格文档

## 1. 定位

`WebPageHost` 是 H5 UI 宿主和 `iCAX-Application` 之间的 C++ adapter。

它只连接三类对象：

- H5 与宿主之间约定的 mail envelope。
- `iCAX-Application` 提供的 `CFrontendBridge`。
- CEF/WebView/Qt 等真实 UI 宿主适配器。

`WebPageHost` 不创建浏览器窗口，不初始化 CEF，不注入 JS，不启动 Engine，也不直接管理 PDO 到 JS 的映射。上述能力由 `CefWebPageHost` 或其他 UI 宿主适配器完成。

## 2. 对外能力

`WebPageHost` 必须提供：

- 绑定一个已经 attach 到 Engine 的 `CFrontendBridge`。
- 启动和停止 H5 adapter。
- 获取 application channel id。
- 注册 product/project channel。
- 接收 H5 mail envelope，并交给 `FrontendBridge` 投递到 Engine。
- 轮询或回调 Engine 发给 H5 的 mail envelope。

`WebPageHost` 不提供：

- Engine 生命周期管理。
- 浏览器 runtime。
- 原生窗口生命周期。
- 文件对话框、窗口标题、拖拽文件等桌面能力。
- shared memory 到 JS `ArrayBuffer` 的映射。

## 3. 生命周期

```text
CApplication.Start()
  -> ApplicationHost.Start()
  -> FrontendBridge.Attach(ApplicationHost)

CWebPageHost.SetConfig({ pFrontendBridge, WebPageRoot })
CWebPageHost.Start()
  -> validate FrontendBridge attached

CWebPageHost.Stop()
  -> stop H5 adapter
```

`Start()` 之后不允许修改配置。`Stop()` 只停止 H5 adapter，不停止 Engine。

## 4. 边界

`WebPageHost` 不实现具体产品逻辑，不解释业务 payload，不直接渲染 H5 UI。

产品逻辑属于 `src/apps/<product-id>/backend` 和 `src/apps/<product-id>/webpage`。

Engine 和 UI 的组合属于 `src/iCAX-Application/Application`。

CEF 集成属于 `src/iCAX-UI/CefWebPageHost`。

## 5. 配置契约

`WebPageHost` 的配置由 `CWebPageHostConfig` 表达。

配置内容：

- `pFrontendBridge`：`CApplication` 提供的通用前端桥，非拥有指针。
- `WebPageRoot`：H5 页面根目录，供宿主适配器加载。

配置规则：

- `SetConfig()` 必须在 `Start()` 之前调用。
- `Start()` 之后配置不可变。
- `pFrontendBridge` 必须非空，并且已经 attach 到 Engine。
- `WebPageHost` 的生命周期应短于或等于拥有该 bridge 的 `CApplication`。

## 6. Mail Envelope 契约

H5 与 `WebPageHost` 之间传递的 mail envelope 字段：

- `channelId`：目标 channel id。
- `id`：本次 mail id。
- `originId`：响应所对应的请求 id，普通请求可为空或为 0。
- `typeCode`：命令码，使用十进制字符串表达 64 位值。
- `stamp`：状态码或版本戳。
- `payloadText`：UTF-8 文本 payload。

`WebPageHost` 不解释 `payloadText` 的业务含义，只负责封送。

## 7. Engine 主动邮件

Engine 主动发给前端的邮件使用同一个 envelope。

约定：

- `originId` 为 0。
- `channelId` 由 Engine 分配。
- `typeCode` 表达事件类型。
- `payloadText` 使用 UTF-8 文本。

`WebPageHost::PollMails()` 会委托 `FrontendBridge` 取出这类 event mail。宿主适配器负责把它推送到 JS。

## 8. 线程与异步

Engine 运行在自己的线程中，H5 前端运行在前端线程中。

`WebPageHost` 是跨线程 adapter，必须保证：

- 自身启动状态访问是线程安全的。
- H5 发 mail 后不能同步等待 Engine 立即返回。
- Engine 返回 response/event 后，由宿主适配器异步推送到 H5。
- H5 侧以 Promise 或事件订阅接收结果。

post office 缓存和 channel 解析属于 `FrontendBridge`。

## 9. 错误处理

配置错误、重复启动、非法 channel id、找不到 product/project post office 属于宿主错误，应在第一现场抛出或返回明确错误。

业务命令失败不由 `WebPageHost` 解释，应由 Engine 写入 response payload，再由前端 mailbox 转成 Promise reject。

## 10. 验收要求

`WebPageHost` 规格验收项：

- 不直接依赖 `ApplicationHost`。
- 可以通过 `FrontendBridge` 返回 application channel id。
- 可以通过 `FrontendBridge` 注册 product/project channel。
- 可以转发 H5 mail 到 Engine。
- 可以把 Engine response/event 取回为 envelope。
- `Stop()` 不停止 Engine。
- C++ 项目 Debug x64 构建无错误。
