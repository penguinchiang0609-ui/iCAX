# Mail 通信

本目录是 foundation 中的普通交互通道，用于进程内两个运行单元之间的命令、事件、请求响应和低频状态同步。

需要过程调用路由时，可使用同级 `ProcedureCall` 提供的 `PCID` 和注册表能力。

## 目录结构

- `Mailbox.vcxproj`：Mail 通信基础工程，提供邮件结构、单向队列、邮局和双向通道。
- `Mail.h`：普通消息头、消息体和签章定义。
- `MailQueue.h` / `MailQueue.cpp`：单向邮件队列，负责入队、取出和清空待处理邮件。
- `MailChannel.h` / `MailChannel.cpp`：双向邮件通道，内部包含 EndA->EndB 与 EndB->EndA 两个队列。
- `MailPostOffice.h` / `MailPostOffice.cpp`：从 `CMailChannel` 某一端获取到的轻量邮局视图，提供 `Send` / `Receive`。
