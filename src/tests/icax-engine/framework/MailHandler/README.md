# MailHandler Tests

本目录对应 `src/iCAX-Engine/framework/MailHandler`，验证 Mailbox 与 Facades 的桥接逻辑。

## 目录结构

- `MailHandlerTest/`：`CMailFacadeHandler` 单元测试，覆盖 Mail 到 FacadeCall 的转换、成功结果、方法缺失结果和邮件 payload 释放路径。
