# Mailbox 规格文档

## 1. 定位

`Mailbox` 是 SDK 内部的前端低频消息模块。它把 Facade 方法调用封装为 Mail，并以 Promise 表达异步结果。

## 2. Facade 方法模型

公开名称固定为：

```text
FacadeName.MethodName
```

名称只允许两个 PascalCase 标识符段。例如 `Machine.Jog`；`Cam.Machine.Import` 非法。

前端与 C++ 分别对 Facade 名称和方法名称执行 FNV-1a 32 位编码，再组合成十进制字符串表示的 64 位 `typeCode`。JS 不用 number 保存 64 位值。

实例信息不进入名称：

```js
mailbox.invoke(channelId, "Machine.Jog", { machineId, axis, delta })
```

## 3. Mail Envelope

字段包括 `channelId`、`id`、`originId`、`typeCode`、`stamp` 和 `payloadText`。`payloadText` 使用 Variant 文本格式。

## 4. 调用与结果

- `MailboxClient.invoke()` 生成 `id`，序列化参数并发送 Mail；
- 后端结果的 `originId` 必须等于调用 Mail 的 `id`；
- Client 通过 `originId` 完成或拒绝 pending Promise；
- 每次调用必须设置正数超时；
- 超时后移除 pending，迟到结果不得再次完成 Promise。

## 5. 事件

后端主动事件的 `originId` 为 0。事件名称也采用两个标识符段，例如 `Machine.Changed`。`subscribe()` 订阅指定名称，`subscribeAll()` 订阅当前 channel 的全部事件。

## 6. 生命周期

- `stop(channelId)`：停止 channel、清理订阅并拒绝该 channel 的 pending 调用；
- `dispose()`：停止全部 channel 并释放 Client；
- AppProxy、ProductProxy 和 SceneProxy 必须在自身释放时清理订阅。

## 7. 边界

Mailbox 不解释 Facade 业务语义，不承载 PDO 高频数据，也不把调用重新抽象为 Command 或 Target。产品页面通过 Proxy 使用它。
