# iCAX-UI

本目录记录 iCAX H5 前端工作台设计。

H5 前端不直接拥有 Engine 数据，也不直接访问 C++ 对象。前端通过 SDK 连接宿主 bridge，再按 `AppProxy -> ProductProxy -> ProjectProxy` 的层级逐步获得产品和项目入口。底层的 Mail、PDO、Bridge 是 SDK 内部通信能力，UI 是公共界面能力。

## 目录结构

- `iCAX-UI规格文档.md`：H5 前端对外能力、页面结构、通信入口和启动流程。
- `iCAX-UI设计文档.md`：iCAX-UI 的总体设计，包括模块关系、产品扩展、消息流和边界。
- `iCAX-UI方案文档.md`：当前落地方案和实现拆分，偏工程实现与迭代计划。

## 模块文档

- `WebPageHost/`：原生宿主模块规格和方案。
- `CefWebPageHost/`：CEF 宿主适配模块规格和方案。
- `AppProxy/`：前端应用层模块规格和方案。
- `ProductProxy/`：前端产品层与产品 webpage 加载模块规格和方案。
- `ProjectProxy/`：前端项目层模块规格和方案。
- `UI/`：公共 UI 基础模块规格和方案。
- `SDK/`：前端统一导出门面规格和方案，内部包含 AppShell、Bridge、Mailbox、PDO 文档。

## 文档完整性约定

每个前端模块必须配套两份文档：

- `规格文档`：写清楚模块定位、职责边界、对外接口、输入输出、生命周期、错误处理、异步/线程假设和验收要求。
- `方案文档`：写清楚源码位置、文件结构、内部流程、关键实现策略、依赖方向、测试方法和验收标准。

公共框架文档不得混入具体产品业务。具体产品的前端协议、页面和 PDO 解释规则应写在 `src/apps/<product-id>/protocol` 或 `src/apps/<product-id>/webpage` 下。

