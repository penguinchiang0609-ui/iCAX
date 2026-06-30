# Mail 通信 Tests

本测试目录对应 `src/icax-engine/framework/Mailbox`，放置 Mail 通信框架能力的单元测试。

## 目录结构

- `MailboxTest/`：对应 `src/icax-engine/framework/Mailbox/Mailbox.vcxproj` 的 Mail 通信单元测试，覆盖收发、线程安全、预分配池租约归还、容量不足、`SendText`、通道销毁和 Reset 后旧邮局失效。
