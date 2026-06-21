# Flat Laser CAM Protocol

本目录描述平面激光 CAM 前后端协议。

协议只属于本产品。通用 mailbox、channel、PDO、command handler 机制由 framework 提供；具体命令名称、payload 和 PDO 字段由产品定义。

## 目录结构

- `mailbox/`：产品邮件命令和请求/响应说明。
- `pdo/`：产品高频数据同步约定。

