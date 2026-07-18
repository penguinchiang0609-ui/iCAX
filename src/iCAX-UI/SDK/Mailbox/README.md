# Mailbox

`Mailbox` 是 SDK 内部模块，负责前端 mailbox 协议封装。

## 目录结构

- `facadeMethod.mjs`：`FacadeName.MethodName` 校验及 64 位 typeCode 编码。
- `mailboxClient.mjs`：Promise 风格的 Facade 调用/结果客户端。
- `variantSerializer.mjs`：JS 对象和 backend Variant 文本格式之间的转换。

## 边界

本模块只负责低频调用、结果和事件消息，不承载高频数据。产品页面不直接 new `MailboxClient`，应通过三层 Proxy 使用。
