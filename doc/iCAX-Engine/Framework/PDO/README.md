# PDO

`PDO` 是 iCAX Engine Framework 层的高频可丢弃数据通道，对应源码目录 `src/icax/framework/PDO/`。

PDO 用于 backend 与 frontend 之间同步高频过程状态。普通命令、事件和请求响应使用 Mailbox。

## 目录结构

- `PDO规格文档.md`：PDO 的外部规格。
- `PDO方案文档.md`：PDO 的实现方案。
