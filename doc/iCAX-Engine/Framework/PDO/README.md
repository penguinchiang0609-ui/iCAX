# PDO

`PDO` 是 iCAX Engine Framework 层的高频可丢弃数据通道，对应源码目录 `src/icax-engine/framework/PDO/`。

PDO 用于 backend 与 frontend 之间同步高频过程状态。普通命令、事件和请求响应使用 Mailbox。

PDO 不做运行时字段自描述，而是模拟硬件 PDO：双方预先约定 `PDOID + payload protocol version + payload size + 固定二进制 layout`。CEF 前端场景下，PDO 可通过 OS shared memory arena 映射到 renderer 进程，再由 JS 使用 TypedArray 读取。

Slot 另有运行期 data version，用于表达当前 payload 对应的源数据版本。Mesh、实例矩阵等重数据只有在源版本高于 Slot 最新版本时才需要重新序列化。正式读写使用 `CPDOReadLease` / `CPDOWriteLease`，每个 Slot 只允许一个写入方向。

## 目录结构

- `PDO规格文档.md`：PDO 的外部规格。
- `PDO方案文档.md`：PDO 的实现方案。
