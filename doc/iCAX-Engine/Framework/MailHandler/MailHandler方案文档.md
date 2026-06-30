# MailHandler 方案文档

## 1. 设计目标

MailHandler 的目标是把运行体中的邮件分发逻辑从 ApplicationHost / ProductRuntime 中抽出来，形成单独的桥接项目。

拆分后的依赖关系：

```text
Mailbox          CommandHandler
   \                 /
    \               /
       MailHandler
```

运行体依赖 MailHandler，但 MailHandler 不反向依赖运行体。

## 2. 处理流程

```text
PostOffice.Receive()
  -> Mail
  -> CMailCommandHandler::ToCommandRequest()
  -> CCommandDispatcher::Dispatch()
  -> CCommandResponse
  -> PostOffice.SendPayload()
```

回复直接使用 `SendPayload`，不再为了回复临时构造一封带 `new[]` payload 的 `Mail`。

## 3. Payload 生命周期

MailHandler 从 `Receive()` 拿到的邮件归它处理。每封邮件进入处理循环后都会创建作用域清理对象：

```text
正常处理结束 -> ReleaseMailPayload(requestMail)
命令 handler 抛异常 -> 栈展开 -> ReleaseMailPayload(requestMail)
```

异常不会被吞掉，仍然停在第一现场。

## 4. 上下文选择

MailHandler 不决定当前邮件属于应用、产品还是项目。

运行体负责传入：

- 应用级：`productContext == nullptr`，`projectContext == nullptr`
- 产品级：`productContext != nullptr`，`projectContext == nullptr`
- 项目级：`productContext != nullptr`，`projectContext != nullptr`

例如 ProductRuntime 知道某个项目邮箱对应哪个 ProjectRuntime，因此仍由 ProductRuntime 获取项目上下文，再传给 MailHandler。

## 5. 状态映射

命令状态到邮件状态戳：

```text
Ok             -> kMailOk
NoHandler      -> kMailNoHandler
InvalidRequest -> kMailInvalidPayload
```

未识别命令状态按 `kMailInvalidPayload` 处理。

## 6. 项目位置

```text
src/iCAX-Engine/framework/MailHandler/
```

测试位置：

```text
src/tests/icax-engine/framework/MailHandler/MailHandlerTest/
```

