# MailHandler

MailHandler 是 Mailbox 与 Facades 之间的适配项目。

它不定义邮件队列，也不定义 Facade 协议；它负责把 `Mail` 转换为 `CFacadeCall`，调用 `CFacadeInvoker`，再把 `CFacadeResult` 写回邮件通道。

## 文档

- `MailHandler规格文档.md`：对外使用接口和约束。
- `MailHandler方案文档.md`：内部处理流程和依赖关系。
