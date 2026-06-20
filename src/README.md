# src

`src` 是当前代码主线入口，按 C++ 后台平台、可选前端壳、共享协议、业务应用、测试和工具分层。

## 目录结构

- `icax/`：C++ 后台平台，包含核心运行时、基础库、框架、Mailbox、PDO 和平台级扩展。
- `icax-workbench/`：前端工作台壳实验目录，当前偏 H5/WebView 形态，后端不依赖该实现。
- `icax-protocol/`：C++ 后台与前端 bridge 共享的协议约定，主要包含 Mailbox、PDO 和通用常量。
- `apps/`：业务应用目录。每个应用可以包含 icax 侧扩展、前端页面或宿主代码、应用级协议。
- `tests/`：测试代码和测试资料，按 `icax/`、`icax-protocol/`、`icax-workbench/` 与源码主目录一一对应。
- `tools/`：项目维护、生成、迁移和编码处理工具。
