# Project 方案文档

## 1. 工程位置

```text
src/icax/framework/Project/
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
      UniverseContext
      Universe
      Project mail id
      WorkThread

    Transient Project*
      Repository
      ResourceLibrary
      UniverseContext
      Universe
      Project mail id
      WorkThread
```

`ApplicationHost` 负责应用级生命周期和产品运行时；`ProductRuntime` 负责项目目录管理和项目运行时句柄；`Project` 负责项目实例。运行态一个 ProjectCatalog 内只有一个主项目，临时项目用于预览、导入和转换。每个项目实例自己运行，不共用同一条后台帧循环。

## 3. 项目实例

`CProject` 是项目隔离边界。每次打开主项目或临时项目都会创建：

- 独立 Repository。
- 独立 ResourceLibrary。
- 独立 UniverseContext。
- 独立 Universe。
- 独立项目 mail channel，由应用级 `IMailChannelService` 按 `ProjectMailID` 托管。
- 独立项目工作线程。

当前实现是进程内隔离：每个 Project 拥有独立对象图和线程，但仍处在同一个 OS 进程地址空间内。一个 Project 内的标准 C++ 异常会被项目线程捕获并记录到 `CProjectFault`，该 Project 进入 `Faulted`，其他 Project 继续运行。

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

`Universe` 只保存 Behaviour 调度器，不保存 Repository，也不作为 Repository 事件订阅者。`Project` 持有 `UniverseContext`，并把当前项目的 Repository、计时器和应用设置通过 context 传入 Behaviour。

`Project` 订阅自己的 Repository 事件，再把事件和 `UniverseContext` 转发给 `Universe`。因此数据归属仍在 Project/Repository，Behaviour 只是被当前项目上下文驱动。

## 5. 打开项目

打开主项目流程：

```text
ProjectCatalog::OpenMainProject(name, path, startupComponent)
  -> fail if main project already exists
  -> Generate project id
  -> Database::GenerateRepository(project id)
  -> create UniverseContext(repository, application settings)
  -> Behaviour::GenerateUniverse()
  -> create ResourceLibrary
  -> create project mail channel through IMailChannelService
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

## 6. 通信

项目通信通道按 `projectMailId` 归属 `Project`，底层 `CMailChannel` 由应用级 `IMailChannelService` 维护：

```text
Project
  projectMailId
  frontend post office
  backend post office
```

`ProductRuntime::GetProjectFrontendPostOffice(projectId)` 先找到项目运行时，再通过项目的 `projectMailId` 返回该项目自己的 frontend post office。`ApplicationHost::GetProjectFrontendPostOffice(projectId)` 只是跨已启动产品做一次查找代理。项目线程每帧从自己的 backend post office 收取邮件并分发命令。

应用级命令仍使用 `ApplicationHost::GetApplicationMailID()` 对应的应用级邮局，适合列产品、启动产品、停止产品。打开 ProjectCatalog 走产品级邮局。

## 7. 与应用级注册表的关系

当前组件元数据、Behaviour 定义、Service 和 ResourceLoader 都在 Application 级装配。产品模块加载后，模块中的宏注册项会被 `ProductRuntime` 回放到 `ApplicationHost` 持有的应用级注册表或服务容器。

Project 不维护自己的业务类型目录。Project 创建 Repository、Universe 和 ResourceLibrary 时只引用应用级定义能力；实体数据、组件实例、资源对象、项目消息和项目线程仍完全归属当前 Project。
