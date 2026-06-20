# Project

`Project` 是 framework 层的项目工程。

它表达已打开的项目运行单元。一个 `ProjectCatalog` 是一组项目的目录，目录内部最多一个主项目，同时允许若干临时项目用于预览、导入和转换。`ProductRuntime` 维护 ProjectCatalog 和 `IProjectRuntime` 运行时句柄，`ApplicationHost` 只在需要时跨已启动产品查找项目邮箱。每个项目实例独占自己的 `Repository`、`ResourceLibrary`、`UniverseContext`、`Universe`、项目 mail channel 和后台工作线程；channel 实体由应用级 `IMailChannelService` 按 `projectMailId` 托管。

Project 是数据隔离边界。组件元数据、行为定义和资源加载器来自应用级注册表；实体数据、组件实例、变更日志、资源对象、项目消息和项目执行循环都只属于当前 Project。

## 目录结构

- `Project.*`：单个已打开项目的运行单元，负责项目线程、项目邮箱、数据库、资源、UniverseContext 和 Universe 生命周期，并标识自身是主项目还是临时项目。项目保存 `ProjectID` 和 `ProjectMailID`，`Universe` 只承载行为结构，项目数据由 `Repository` 和 `UniverseContext` 提供。
- `ProjectCatalog.*`：一个项目目录，拥有自己的 `catalogId/name/path`，负责登记、打开、关闭和查询目录内的主项目与临时项目，不负责统一 Tick 所有项目。
- `ProjectRuntime.*`：项目运行时抽象与本地实现。`CLocalProjectRuntime` 包装进程内 `CProject`，后续进程级隔离可以实现同一个 `IProjectRuntime` 接口。
- `ProjectExport.h`：DLL 导出宏。
