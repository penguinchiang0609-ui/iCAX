# Mail 通信

本目录是 framework 中的普通交互通道，用于进程内两个运行单元之间的命令、事件、请求响应和低频状态同步。

需要命令路由时，上层适配器可把邮件转换为 `CommandHandler` 的命令请求。

Mailbox 底层只保存 `MailHeader + bytes payload`，不解释 payload 内容。面向 H5 的普通命令约定 payload 为 UTF-8 文本，也就是 JS string；文本内容通常是 JSON。C++ 侧使用 `MailPayload.h` 中的 `SetMailPayloadText` / `GetMailPayloadText` / `CreateTextMail` 处理这类文本负载。

大块二进制资源不应塞进普通命令文本 payload，应通过资源池或专门的二进制通道传递；高频状态同步走 PDO。

## 目录结构

- `Mailbox.vcxproj`：Mail 通信基础工程，提供邮件结构、单向队列、邮局、双向通道和通道注册表。
- `Mail.h`：普通消息头、消息体和签章定义。
- `MailPayload.h` / `MailPayload.cpp`：文本 payload 辅助函数，面向 H5/JSON 命令负载。
- `MailQueue.h` / `MailQueue.cpp`：单向邮件队列，负责入队、取出和清空待处理邮件。
- `MailChannel.h` / `MailChannel.cpp`：双向邮件通道，内部包含 EndA->EndB 与 EndB->EndA 两个队列；`Reset()` 会让旧邮局失效并创建新队列。
- `MailChannelRegistry.h` / `MailChannelRegistry.cpp`：按 ChannelID 显式创建、查询和删除 `CMailChannel` 的线程安全注册表；它由运行时对象持有，不属于 ServiceProvider。
- `MailPostOffice.h` / `MailPostOffice.cpp`：从 `CMailChannel` 某一端获取到的轻量邮局视图，提供 `Send` / `Receive`。
