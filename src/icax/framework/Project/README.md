# Project

`Project` 是 framework 层的产品与项目会话工程。

它区分两件事：

- `ProductDefinition` / `ProductCatalog`：产品类型，如三维五轴、平切、焊接。
- `ProjectSession` / `ProjectManager`：已打开的项目实例。每个项目实例独占自己的 `Repository`、`ResourceLibrary`、`UniverseContext`、`Universe`、`MailChannel` 和后台工作线程。

## 目录结构

- `ProductDefinition.*`：产品类型描述，包括产品 ID、显示名、启动组件、前端入口、协议路径和模块信息。
- `ProductCatalog.*`：产品目录，支持注册产品定义，也支持从产品 `manifest.json` 加载。
- `ProjectSession.*`：单个已打开项目的运行单元，负责项目线程、项目邮箱、数据库、资源、UniverseContext 和 Universe 生命周期。`Universe` 只承载行为结构，项目数据由 `Repository` 和 `UniverseContext` 提供。
- `ProjectManager.*`：多项目管理器，负责打开、关闭、查询和设置活动项目，不负责统一 Tick 所有项目。
- `Project.h`：普通入口头。
- `ProjectExport.h`：DLL 导出宏。
