# MailHandler Tests

本目录对应 `src/iCAX-Engine/framework/MailHandler`，验证 Mailbox 与 CommandHandler 的桥接逻辑。

## 目录结构

- `MailHandlerTest/`：`CMailCommandHandler` 单元测试，覆盖 Mail 到 CommandRequest 的转换、成功响应、无 handler 响应和邮件 payload 释放路径。

