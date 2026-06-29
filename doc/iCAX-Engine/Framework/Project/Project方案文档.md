# Project 方案文档

## 1. 工程位置

```text
src/icax-engine/framework/Project/
```

`Project` 放在 framework 下，因为它不是基础数据结构，而是把 Database、Resources、Behaviour 和 Mailbox 组合成项目运行单元的框架能力。

## 2. 分层

```text
ApplicationHost
  ProductRuntime
    ProjectCatalog
    IProjectRuntime*
    Main Project?
      Repository
      ResourceLibrary
      Universe
      PDOHub?
      Project mail id
      WorkThread

    Transient Project*
      Repository
      ResourceLibrary
      Universe
      PDOHub?
      Project mail id
      WorkThread
```

`ApplicationHost` 负责应用级生命周期和产品运行时；`ProductRuntime` 负责项目目录管理和项目运行时句柄；`Project` 负责项目实例。运行态一个 ProjectCatalog 内只有一个主项目，临时项目用于预览、导入和转换。每个项目实例自己运行，不共用同一条后台帧循环。

## 3. 项目实例

`CProject` 是项目隔离边界。每次打开主项目或临时项目都会创建：

- 独立 Repository。
- 独立 ResourceLibrary。
- 独立 Universe。
- 独立项目 mail channel，由应用级 `CMailChannelRegistry` 按 `ProjectChannelID` 托管。
- 可选独立 PDOHub，由 `CProjectCreateInfo::PDODeclarations` 决定是否创建。
- 独立项目工作线程。

当前实现是进程内隔离：每个 Project 拥有独立对象图和线程，但仍处在同一个 OS 进程地址空间内。Behaviour 回调不做异常拦截或过滤，运行错误按第一现场暴露。

严格意义上的独立内存空间必须使用进程级隔离，即每个 Project 由独立 Project Worker 进程承载。线程和对象边界不能阻止野指针、堆破坏、访问冲突等 native 崩溃影响整个进程。

## 4. ProjectRuntime

`IProjectRuntime` 是 ProductRuntime 看到的项目运行句柄。它提供项目身份、状态、前端邮局、启动、停止和关闭能力，但不要求上层知道项目内部是不是 `CProject`。

当前实现为 `CLocalProjectRuntime`：

```text
CLocalProjectRuntime
  -> CProject
  -> Project thread
  -> project mail id
```

`CLocalProjectRuntime` 仍运行在当前进程中，用于保持当前功能和测试稳定。后续如果要做到每个 Project 独立内存空间，应新增 `CProcessProjectRuntime`，由它启动独立 Project Worker 进程，并把 mailbox/PDO 转换成跨进程通道。

`Universe` 只保存 Behaviour 调度器，不保存 Repository，也不作为 Repository 事件订阅者。`Project` 在每帧调度和 Repository 事件转发时，显式传入 `ApplicationContext`、`ProductContext` 和 `ProjectContext`。

`Project` 订阅自己的 Repository 事件，再把事件和三层上下文转发给 `Universe`。因此数据归属仍在 Project/Repository，Behaviour 只是被当前项目上下文驱动。

## 5. 打开项目

打开主项目流程：

```text
ProjectCatalog::OpenMainProject(name, path, startupComponent)
  -> fail if main project already exists
  -> Generate project id
  -> require product MetaRegistry / BehaviourRegistry / project ResourceLoaderRegistry / MailChannelRegistry
  -> Database::GenerateRepository(project id, product MetaRegistry)
  -> Behaviour::GenerateUniverse(product BehaviourRegistry)
  -> create ResourceLibrary(project ResourceLoaderRegistry)
  -> create PDOHub if PDODeclarations is not empty
  -> create project mail channel through CMailChannelRegistry
  -> Project subscribes repository events
  -> set main project id
```

打开临时项目使用 `ProjectCatalog::OpenTransientProject(name, path, startupComponent)`，流程相同，但不会占用主项目位置。

`ProjectCatalog::OpenMainProject` 和 `OpenTransientProject` 只创建项目实例，不自动启动线程。由 `ProductRuntime::OpenProjectCatalog`、`OpenTransientProject` 或其他上层宿主完成运行接入：

```text
ProductRuntime::OpenProjectCatalog(name, path)
  -> ProjectCatalog.OpenMainProject(name, path, startupComponent)
  -> CreateLocalProjectRuntime(project)
  -> ProjectRuntime.SetFrameHandler(dispatch project mailbox)
  -> ProjectRuntime.Start()
```

如果创建参数包含 `StartupComponent`，`Project` 在线程启动时执行 `BindStartup`。`BindStartup` 会查找启动组件对应的 Behaviour，并把启动组件添加到 Repository 的 meta entity。

## 6. 快速保存闭环

快速保存分为两层：

- Database 层：`Repository` 负责把普通修改、批量提交、事务提交、Undo/Redo 写成有序 `OperationBatch`，并能回放日志。
- Project 层：`Project` 负责日志路径、打开、回放、截断和关闭时机。

Project 不直接序列化主项目文件。正确流程是：

```text
打开项目文件
  -> 产品/文件模块读取主项目文件
  -> Repository.BeginLoadBaseline()
  -> 写入项目基线数据
  -> Repository.EndLoadBaseline()
  -> Project.ReplayQuickSaveLog(quickSaveMagic, quickSaveVersion)
  -> Project.Start()
  -> Project.OpenQuickSaveLog(false, quickSaveMagic, quickSaveVersion)
```

保存项目文件：

```text
保存项目文件
  -> 产品/文件模块把当前 Repository/ResourceLibrary 写入主项目文件
  -> 写文件成功
  -> Project.MarkProjectFileSaved(path?, quickSaveMagic, quickSaveVersion)
       -> close old operation log
       -> truncate quick-save log
       -> write quick-save log header
       -> reopen quick-save log for append
```

关闭项目：

```text
Project.Close()
  -> Stop project thread
  -> CloseQuickSaveLog()
  -> cleanup Universe/Repository/ResourceLibrary
  -> release project PDOHub
  -> remove project mail channel
```

关闭时先关闭日志，是为了避免 Repository 清理实体和组件时把删除操作追加到快速保存日志。

`ProductRuntime::OpenProjectCatalog` 会在创建主项目后先回放 quick-save 日志，再启动项目线程，启动成功后打开追加日志。临时项目默认不自动承担主项目快速保存职责。

quick-save 日志头部的 `magic` 和 `version` 来自产品/文件层。Project 只负责透传，Repository 只负责校验和写入日志头；产品文件版本的升级代码不放在 Project 或 Database 中。

## 7. 通信

项目通信通道按 `projectChannelId` 归属 `Project`，底层 `CMailChannel` 由应用级 `CMailChannelRegistry` 维护：

```text
Project
  projectChannelId
  frontend post office
  backend post office
```

`ProductRuntime::GetProjectFrontendPostOffice(projectId)` 先找到项目运行时，再通过项目的 `projectChannelId` 返回该项目自己的 frontend post office。`ApplicationHost::GetProjectFrontendPostOffice(projectId)` 只是跨已启动产品做一次查找代理。项目线程每帧从自己的 backend post office 收取邮件并分发命令。

应用级命令仍使用 `ApplicationHost::GetApplicationChannelID()` 对应的应用级邮局，适合列产品、启动产品、停止产品。打开 ProjectCatalog 走产品级邮局。

## 8. 与产品级注册表的关系

当前组件元数据、Behaviour 定义、ResourceLoader 和 CommandHandler 都在 ProductRuntime 级装配。产品模块加载后，模块中的宏注册项会被 `ProductRuntime` 回放到当前产品的注册表。Service 仍在 Application 级 ServiceProvider 中共享。

Project 不维护自己的业务类型目录。Project 创建 Repository 和 Universe 时引用产品级定义能力；创建 ResourceLibrary 时使用项目自己的 ResourceLoaderRegistry。实体数据、组件实例、资源对象、项目消息和项目线程仍完全归属当前 Project。

## 9. 与 PDO 的关系

PDO 声明由产品或文件/启动模块决定，通过 `CProjectCreateInfo::PDODeclarations` 传给 Project。Project 不理解 payload 字段含义，只负责创建、交换和释放项目级 PDOHub。

帧循环顺序：

```text
PreSwapPDO()
  -> Project PDOHub SwapInSlot()
  -> Universe PreSwapPDO()
FrameHandler(project mailbox)
Tick()
PostSwapPDO()
  -> Universe PostSwapPDO()
  -> Project PDOHub SwapOutSlot()
```

Behaviour 和 CommandHandler 通过 `IProjectContext::HasPDOHub()` 和 `IProjectContext::PDOHub()` 访问项目 PDO。未配置 PDO 的项目会在访问 `PDOHub()` 时抛出异常。

前端宿主通过 Mailbox 获取项目 PDO Arena name，再打开对应 shared memory。Project 关闭时释放 PDOHub；前端宿主收到项目关闭后必须停止访问该 Arena。

