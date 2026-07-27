# PDO

`PDO` 是 SDK 内部模块，负责前端访问 PDO 高频数据。声明中的 `payloadSize` 表示 Slot 容量；读回调收到的 `ArrayBuffer` 和 `meta.payloadSize` 只覆盖当前帧的实际有效字节，`meta.payloadCapacity` 保留声明容量。写回调可返回实际写入字节数，未返回时按完整容量发布。

## 目录结构

- `pdoClient.mjs`：根据 project state 中的 PDO descriptor 查找声明，并通过 bridge 获取 read lease。

## 边界

本模块不解释具体业务数据结构。结构化 PDO 的 Google FlatBuffers schema 由产品 `protocol/` 约定；读取前仍需按生成类型和 file identifier 校验。产品页面通过 `scene.pdo` 使用，不直接构造 `PDOClient`。
