# Mailbox

`Mailbox` 是 foundation 中的普通交互通道，用于 C++ backend 与 H5 frontend 之间的命令、事件、请求响应和低频状态同步。

需要过程调用路由时，Mailbox 使用同级 `ProcedureCall` 提供的 `PCID` 和注册表能力。

## 目录结构

- `Mailbox.vcxproj`：Mailbox 基础工程，提供 `Mail`、`MailHeader`、`MailData` 和 `CMailBox` 队列。
- `Mail.h`：普通消息头、消息体和签章定义。
- `MailBox.h` / `MailBox.cpp`：普通消息队列，负责投递、取回和清空待处理邮件。
