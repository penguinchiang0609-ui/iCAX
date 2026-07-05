# Project

`Project` 是 framework 层的项目工程。

它表达已打开的项目管理容器。一个 `ProjectCatalog` 是一个项目打开上下文，内部只持有一个主 Project；预览、导入、局部编辑和仿真等隔离现场由 Project 内部的子 Scene 承载。`ProductRuntime` 维护 ProjectCatalog 和 `IProjectRuntime` 运行时句柄，`ApplicationHost` 只在需要时跨已启动产品查找项目入口。

Project 只承载项目身份、路径、项目级 Settings 和 Scene 集合。具体运行现场属于 `ProjectScene`：每个 Scene 独占自己的 `Repository`、`ResourceLibrary`、`Universe`、scene mail channel、可选 PDOHub、运行时调度器和后台工作线程；channel 实体由应用级 `CMailChannelRegistry` 按 `sceneChannelId` 托管。主项目启动后会创建 MainScene，`CProject` 上所有运行现场便利入口都使用 `MainScene*` 前缀，不表示 Project 自己拥有这些资源。

Project 负责协调快速保存日志生命周期，但具体日志写入和回放落在 Scene 的 Repository 上。主项目文件本体由产品/文件模块读写；Project 在基线加载后携带产品传入的日志 `magic/version` 回放 `ProjectPath + ".log"`，MainScene 启动后用同一组 `magic/version` 打开追加日志，项目文件保存成功后通过 `MarkProjectFileSaved` 截断并重开日志，关闭时先关日志再清理 Scene。

## 目录结构

- `Project.*`：单个已打开项目的管理容器，实现 `IProjectContext`，保存 `ProjectID`、项目名称、项目路径和项目级 Settings，并管理 MainScene/ChildScene。
- `ProjectScene.*`：单个 Scene 的运行单元，实现 `ISceneContext`，负责 Scene 线程、Scene 邮箱、数据库、资源、可选 PDOHub 和 Universe 生命周期，并标识自身是 MainScene 还是临时 Scene。
- `ProjectCommands.h`：项目级公共命令常量，当前包含 `Project.GetState`、`Project.Undo`、`Project.Redo`、`Project.GetUndoRedoState`。这些命令操作项目数据，但执行时要求 mail 来自具体 Scene mailbox，并同时携带 ProjectContext 和 SceneContext。
- `SceneRuntimeScheduler.*`：Scene 运行时调度器，负责帧时间统计、目标帧间隔和下一帧唤醒时间；Scene 每帧把调度器产生的 `deltaTime/totalTime` 传给 Universe。
- `ProjectCatalog.*`：一个项目目录，拥有自己的 `catalogId/name/path`，负责打开、关闭和查询目录内的主 Project，不负责 Tick；Scene 线程由 ProjectScene 自己持有。Catalog 构造时必须拿到完整上下文、注册表、邮件通道注册表和资源加载器注册表工厂。
- `ProjectRuntime.*`：项目运行时抽象与本地实现。`CLocalProjectRuntime` 包装进程内 `CProject`，后续进程级隔离可以实现同一个 `IProjectRuntime` 接口。
- `ProjectExport.h`：DLL 导出宏。

