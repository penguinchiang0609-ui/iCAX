# Mail 通信

本目录提供 framework 的低频消息通道，用于同一进程内两个运行单元之间传递命令、事件、请求响应和低频状态同步。

Mailbox 只负责传递 `MailHeader + bytes payload`，不解释 payload 内容。面向 H5 的普通命令约定 payload 为 UTF-8 文本，也就是 JS string；文本内容通常是 JSON。C++ 侧使用 `MailPayload.h` 中的 `SetMailPayloadText` / `GetMailPayloadText` / `CreateTextMail` / `ReleaseMailPayload` 处理文本负载。

队列内部使用预分配 record 池和 payload 池。发送时 payload 会复制到队列池中；接收时返回的 payload 可能是队列池租约，也可能是普通 `new[]` 内存。业务代码一律使用 `ReleaseMailPayload` 释放，不允许手写 `delete[]`。

大块模型、刀路、渲染等高频数据不走普通 Mail payload，应通过 PDO 或资源/专用数据通道传递。

## 目录结构

- `Mailbox.vcxproj`：Mail 通信基础工程。
- `Mail.h`：邮件头、状态戳、payload 租约和邮件结构。
- `MailPayload.h` / `MailPayload.cpp`：payload 创建、读取和统一释放辅助函数。
- `MailQueue.h` / `MailQueue.cpp`：单向预分配邮件队列。
- `MailChannel.h` / `MailChannel.cpp`：双向邮件通道，内部包含 EndA->EndB 与 EndB->EndA 两个队列。
- `MailPostOffice.h` / `MailPostOffice.cpp`：某个端点看到的轻量邮局视图，提供 `Send` / `SendPayload` / `SendText` / `Receive`。
- `MailChannelRegistry.h` / `MailChannelRegistry.cpp`：按 ChannelID 管理通道生命周期的线程安全注册表。
