# Protocol

`protocol` 保存平面激光 CAM 的前后端协议约定。

## 目录结构

- `mailbox/`：低频命令和事件协议。
- `pdo/`：高频 PDO layout。
- `file/`：项目文件格式、magic、schema version 和 migration 说明。

Mailbox 命令使用 `Main.Sub` 名称，底层由 `FrontendSDK` 和 C++ `CommandHandler` 使用一致的 FNV-1a 32 位组合算法生成 64 位路由码。

PDO 不做运行时自描述。通信双方提前约定 `typeName + instanceName -> PDOID`、payload version、payload size 和二进制布局。
