# PDO

`PDO` 是 SDK 内部模块，负责前端访问 PDO 高频数据。

## 目录结构

- `pdoClient.mjs`：根据 project state 中的 PDO descriptor 查找声明，并通过 bridge 获取 read lease。

## 边界

本模块不解释具体业务数据结构。某个 PDO 的二进制布局由产品 `protocol/` 约定。产品页面通过 `scene.pdo` 使用，不直接构造 `PDOClient`。
