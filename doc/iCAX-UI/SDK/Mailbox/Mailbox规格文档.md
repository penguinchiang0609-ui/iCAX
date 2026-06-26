# Mailbox 规格文档

## 1. 定位

`Mailbox` 是 SDK 内部的前端低频消息模块。

它负责把 JS 调用封装为 backend mailbox mail，并以 Promise 表达异步结果。

## 2. 命令模型

命令采用主命令和子命令：

```js
{ main: "App", sub: "GetState" }
```

前端和 C++ 使用一致算法生成 64 位 `typeCode`。

## 3. Mail Envelope

前端 envelope 包含：

- `channelId`
- `id`
- `originId`
- `typeCode`
- `stamp`
- `payloadText`

`payloadText` 是 Variant 文本格式。

## 4. 边界

mailbox 用于控制命令、普通业务消息、事件通知，不用于高频大数据传输。产品页面不直接 new `MailboxClient`，应通过 `AppProxy/ProductProxy/ProjectProxy` 间接使用。

## 5. Command Route

命令对象格式：

```js
{
  main: "Project",
  sub: "Save"
}
```

`main` 表达主命令域，`sub` 表达子命令。

编码要求：

- 前端和 C++ 使用同一个算法。
- 结果为 64 位无符号整数。
- JS 侧用十进制字符串保存，避免精度丢失。
- 上层产品代码优先使用字符串主/子命令，不直接手写数字。

## 6. Request/Response 语义

前端发起请求时：

- `id` 由 `MailboxClient` 生成。
- `originId` 为 0 或空。
- `payloadText` 为序列化后的 Variant 文本。

backend 返回响应时：

- `originId` 必须等于请求 `id`。
- `payloadText` 携带返回值或错误。

`MailboxClient` 通过 `originId` 匹配 pending Promise。

## 7. Timeout 语义

每个 request 必须有超时时间。

超时后：

- pending Promise reject。
- pending 表中移除该请求。
- 之后即使迟到 response 到达，也不得重新 resolve 已超时 Promise。

## 8. Event 语义

backend 主动发给前端的邮件称为 event mail。

event mail 约定：

- `originId` 必须为 0。
- `id` 由 backend 分配。
- `typeCode` 表达事件类型，仍使用主/子命令编码算法。
- `payloadText` 携带事件数据。

前端订阅 API：

```js
const unsubscribe = mailbox.subscribe(channelId, "Project.RepositoryChanged", event => {
  console.log(event.payload);
});

const unsubscribeAll = mailbox.subscribeAll(channelId, event => {
  console.log(event.typeCode, event.payload);
});
```

`subscribe()` 只接收指定事件类型。

`subscribeAll()` 接收指定 mailbox 下所有 event mail。

取消订阅后不得继续触发 handler。

## 9. 生命周期

`MailboxClient` 必须提供 channel 级和整体释放能力：

- `stop(channelId)`：取消指定 channel 的宿主订阅，清除该 channel 的事件订阅，并 reject 该 channel 的 pending request。
- `dispose()`：停止所有已启动 channel，reject 所有 pending request，并清空事件订阅。

上层 `AppProxy`、`ProductProxy`、`ProjectProxy` 在停止或关闭时必须释放自己持有的订阅，不能只删除前端对象引用。

## 10. Event 对象

前端 handler 接收的 event 对象包含：

- `channelId`
- `id`
- `originId`
- `typeCode`
- `stamp`
- `ok`
- `payload`
- `error`
- `raw`

如果 `payloadText` 反序列化失败，`ok` 为 false，`error` 保存反序列化异常。

## 11. Variant 支持范围

前端 Variant 文本至少支持：

- `null`
- `boolean`
- `number`
- `string`
- array
- plain object

超过 JS 安全整数范围的整数不能作为普通 number 传输。64 位标识类字段应使用字符串。

## 12. 错误处理

错误分两类：

- 投递错误：bridge `postMail()` 失败，request 立即 reject。
- 业务错误：backend response 表达失败，request reject。

具体业务错误码和错误内容由 backend command 定义。
