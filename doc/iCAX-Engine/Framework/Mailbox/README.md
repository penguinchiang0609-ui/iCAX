# Mail 通信

本目录记录 iCAX Engine Framework 层普通消息通道的规格与方案文档。

对应源码目录 `src/icax-engine/framework/Mailbox/`。它是进程内两个运行单元之间低频命令、事件、请求响应的框架能力；高频可丢弃数据使用 PDO。

普通 H5 命令的 Payload 约定为 UTF-8 JS string，通常承载 JSON 文本；Mailbox 底层仍只负责传输 bytes。

## 目录结构

- `Mailbox规格文档.md`：Mail 通信基础能力的外部规格。
- `Mailbox方案文档.md`：Mail 通信基础能力的实现方案。
