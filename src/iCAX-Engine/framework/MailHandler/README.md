# MailHandler

本目录提供 Mailbox 与 Facades 之间的桥接能力。

Mailbox 只负责收发 `Mail`，Facades 定义并执行对外方法。MailHandler 负责把运行体收到的邮件转换为 `CFacadeCall`，调用 `CFacadeInvoker`，再把 `CFacadeResult` 转成回复邮件发送回去。

MailHandler 不拥有运行体、上下文、通道或注册表，也不依赖 ApplicationHost / Product / Project。

## 目录结构

- `MailHandler.vcxproj`：MailHandler 动态库工程。
- `CMailFacadeHandler.h` / `CMailFacadeHandler.cpp`：Mail 与 Facade 调用/结果之间的适配器。
- `MailHandlerExport.h`：DLL 导出宏。
