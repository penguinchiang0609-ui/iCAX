# MailHandler

MailHandler 是 Mailbox 与 CommandHandler 之间的桥接项目。

它不定义邮件队列，也不定义命令路由；它负责在运行体收到邮件后，把 `Mail` 转换为 `CCommandRequest`，调用 `CCommandDispatcher`，再把 `CCommandResponse` 写回邮件通道。

## 文档

- `MailHandler规格文档.md`：对外使用接口和约束。
- `MailHandler方案文档.md`：内部处理流程和依赖关系。

