# Project

`Project` 是 framework 层的项目工程。

它表达已打开的项目运行单元。一个 `ProjectCatalog` 是一组项目的目录，目录内部最多一个主项目，同时允许若干临时项目用于预览、导入和转换。`ProductRuntime` 维护 ProjectCatalog 和 `IProjectRuntime` 运行时句柄，`ApplicationHost` 只在需要时跨已启动产品查找项目邮箱。每个项目实例独占自己的 `Repository`、`ResourceLibrary`、`Universe`、项目 mail channel、可选 PDOHub、运行时调度器和后台工作线程；channel 实体由应用级 `IMailChannelService` 按 `projectChannelId` 托管。

Project 是数据隔离边界，也是 Universe Tick 的驱动方。`IProjectContext` 公共契约位于 `ProjectContext` 项目，`Project` 只负责运行时实现。组件元数据和行为定义来自当前 ProductRuntime 的产品级注册表；资源加载器由当前产品模块回放到项目自己的 ResourceLoaderRegistry；实体数据、组件实例、变更日志、资源对象、项目消息、项目 PDO、帧时间和项目执行循环都只属于当前 Project。

Project 还负责快速保存日志生命周期。主项目文件本体由产品/文件模块读写；Project 在基线加载后携带产品传入的日志 `magic/version` 回放 `ProjectPath + ".log"`，项目启动后用同一组 `magic/version` 打开追加日志，项目文件保存成功后通过 `MarkProjectFileSaved` 截断并重开日志，关闭时先关日志再清理 Repository。

## 目录结构

- `Project.*`：单个已打开项目的运行单元，负责项目线程、项目邮箱、数据库、资源、可选 PDOHub 和 Universe 生命周期，并标识自身是主项目还是临时项目。项目实现 `IProjectContext`，保存 `ProjectID` 和 `ProjectChannelID`；`Universe` 只承载行为结构，项目数据由 `Repository`、`ResourceLibrary` 和项目级 PDOHub 提供。
- `ProjectCommands.h`：项目级公共命令常量，当前包含 `Project.GetState`、`Project.Undo`、`Project.Redo`、`Project.GetUndoRedoState`。这些命令由 `ProductRuntime` 在产品级命令注册表中注册，执行时要求 mail 来自项目邮箱。
- `ProjectRuntimeScheduler.*`：项目运行时调度器，负责帧时间统计、目标帧间隔和下一帧唤醒时间；Project 每帧把调度器产生的 `deltaTime/totalTime` 传给 Universe。
- `ProjectCatalog.*`：一个项目目录，拥有自己的 `catalogId/name/path`，负责登记、打开、关闭和查询目录内的主项目与临时项目，不负责统一 Tick 所有项目。Catalog 构造时必须拿到完整上下文、注册表、邮件通道服务和资源加载器注册表工厂。
- `ProjectRuntime.*`：项目运行时抽象与本地实现。`CLocalProjectRuntime` 包装进程内 `CProject`，后续进程级隔离可以实现同一个 `IProjectRuntime` 接口。
- `ProjectExport.h`：DLL 导出宏。
