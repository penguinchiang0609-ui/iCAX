# Mailbox

`Mailbox` 记录 iCAX Engine Foundation 层普通消息通道的规格与方案文档。

Mailbox 对应源码目录 `src/icax/foundation/Mailbox/`。它是 C++ backend 与 H5 frontend 之间低频命令、事件、请求响应的基础能力；高频可丢弃数据使用 PDO。

## 目录结构

- `Mailbox规格文档.md`：Mailbox 基础消息与队列的外部规格。
- `Mailbox方案文档.md`：Mailbox 基础消息与队列的实现方案。
