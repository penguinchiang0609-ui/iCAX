# PDO

`PDO` 是 framework 中的高频可丢弃数据通道工程，用于承载过程数据声明、PDO Hub、共享内存双缓冲数据槽和前后端高频数据同步接口。

结构化 PDO 采用双方预先约定的 Google FlatBuffers schema，不做运行时字段自描述。业务模块拥有 `.fbs`、file identifier 和生成类型；framework 只提供 shared-memory 槽、精确字节边界和校验接入。历史固定 C++ 布局仍可兼容使用。PDO 面向高频数据，CEF 场景下可通过 `CSharedPDOArena` 使用 Windows OS shared memory 暴露给 renderer。PDO 允许丢帧，但读侧必须通过 `CPDOReadLease` 声明读租约，写侧不会覆盖正在读取的 buffer。

Slot 维护独立的运行期 data version。`PDODecl.nVersion` 只表示 payload/schema 协议版本；Mesh、实例矩阵等源数据版本通过 `CPDOWriteLease::TryBeginIfNewer(slot, dataVersion)` 写入 Slot，用来避免未变化数据每帧重复序列化。Slot 声明保存最大容量，每个双缓冲区另外保存本次发布的实际 payload size，因此普通 FlatBuffer 不需要额外的 size prefix。每个 Slot 只能有一个写入方向；双向数据必须拆成两个 PDO。

`PDOHub` 支持动态 Arena：Scene 可以先创建一块固定大小的 shared memory，再在运行期通过 `AllocateSlot()` / `FreeSlot()` 分配和回收 slot。前端必须以 `PDOID` 定位 slot，不缓存 arena offset。

## 目录结构

- `PDODecl.*`：PDO ID、方向和固定载荷声明。
- `PDOPayload.h`：payload C++ 固定布局声明辅助。
- `PDOLease.h`：读写 RAII 租约，产品代码优先使用。
- `PDOFlatBuffer.h`：标准 Google FlatBuffer 写入、精确字节范围、`Verifier` 校验和零拷贝 root 读取辅助。
- `IPDOSlot.*`：PDO 双缓冲槽抽象接口。
- `IPDOHub.*`：PDO Hub 抽象接口，可获取共享内存名称、大小和声明快照。
- `SharedPDOArena.*`：Windows OS shared memory backed 双缓冲 PDO Arena 与 Slot 实现。
- `PDOHub.*`：一组共享内存 PDO Slot 的集合，负责按方向在帧边界统一交换。
