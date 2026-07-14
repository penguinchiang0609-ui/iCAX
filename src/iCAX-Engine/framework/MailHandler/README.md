# MailHandler

本目录提供 Mailbox 与 CommandTargets 之间的桥接能力。

Mailbox 只负责收发 `Mail`，CommandTargets 只负责命令路由与分发。MailHandler 负责把运行体收到的邮件转换为 `CCommandRequest`，调用 `CCommandDispatcher`，再把 `CCommandResponse` 转成回复邮件发送回去。

MailHandler 不拥有运行体、上下文、通道或注册表，也不依赖 ApplicationHost / Product / Project。

## 目录结构

- `MailHandler.vcxproj`：MailHandler 动态库工程。
- `CMailCommandHandler.h` / `CMailCommandHandler.cpp`：Mail 到 Command 的桥接器。
- `MailHandlerExport.h`：DLL 导出宏。
