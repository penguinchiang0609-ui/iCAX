# MailHandler 方案文档

## 1. 定位

MailHandler 把运行体中的 Mail 适配逻辑从 ApplicationHost 和 ProductRuntime 中抽离。依赖关系为：

```text
Mailbox          Facades
   \               /
       MailHandler
```

运行体依赖 MailHandler，MailHandler 不反向依赖具体运行体。

## 2. 流程

```text
PostOffice.Receive()
  -> Mail
  -> CMailFacadeHandler::ToFacadeCall()
  -> CFacadeInvoker::Invoke()
  -> CFacadeResult
  -> CMailFacadeHandler::SendFacadeResult()
  -> PostOffice.SendPayload()
```

Mail 的 64 位 `typeCode` 直接承载 `FacadeName.MethodName` 编码，payload 承载参数。适配层不增加 Command、Target 或 Route 概念。

## 3. Payload 生命周期

MailHandler 处理 `Receive()` 返回的邮件，并保证每封请求 Mail 的 payload 在正常结束或异常路径释放。回复直接使用 `SendPayload`，避免构造带临时 `new[]` payload 的 Mail。

## 4. 上下文

- 应用范围：只有 `ApplicationContext`；
- 产品范围：包含 `ApplicationContext` 和 `ProductContext`；
- Scene 范围：同时包含 Application、Product、Project 和 Scene Context。

运行体决定上下文，MailHandler 只转交。

## 5. 状态映射

```text
Ok                         -> kMailOk
FacadeNotFound/MethodNotFound -> kMailNotFound
InvalidCall                -> kMailInvalidPayload
```

Facade 方法异常会在 Mail 边界转换为 `InvalidCall` 结果，错误文本写入回复 payload，避免异常击穿运行线程。
