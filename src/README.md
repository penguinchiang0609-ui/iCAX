# src

`src` 是当前代码主线入口，按平台、前端壳、共享协议、具体产品、测试和工具分层。

## 目录结构

- `icax/`：C++ 后台平台，包含核心运行时、基础库、框架、ProcedureCall、Mailbox、PDO 和平台级扩展。
- `icax-workbench/`：H5 前端通用工作台壳，负责页面运行环境、布局、路由和前端侧通信封装。
- `icax-protocol/`：C++ 后台与 H5 前台共享的协议约定，主要包含 Mailbox、PDO 和通用常量。
- `products/`：具体产品目录。每个产品同时包含 icax 侧扩展、H5 页面和产品级协议。
- `tests/`：测试代码和测试资料，按 `icax/`、`icax-protocol/`、`icax-workbench/` 与源码主目录一一对应。
- `tools/`：项目维护、生成、迁移和编码处理工具。
