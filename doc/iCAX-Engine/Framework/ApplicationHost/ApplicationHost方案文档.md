# ApplicationHost 方案文档

## 1. 工程位置

```text
src/icax/framework/ApplicationHost/
```

`ApplicationHost` 是后台工作区宿主，不是某个产品的 backend，也不是 Project 容器本身。

## 2. 架构

```text
ApplicationHost
  ApplicationContext
  CommandRegistry / CommandDispatcher
  MailPostOfficeService
  ProjectManager
    ProductCatalog
    ProjectSession*
```

`ApplicationHost` 只负责装配和驱动；产品和项目状态由 `Project` 工程表达。
这里的“驱动”只指应用宿主自己的生命周期和应用级消息处理。项目帧循环由各自 `ProjectSession` 的线程驱动。

## 3. 装配流程

```text
CApplicationHost::Start
  -> start work thread
  -> wait until Running or Faulted

WorkThread
  -> Notify Starting
  -> Phase Loading
  -> LoadApplicationSettings
  -> Create ApplicationContext
  -> Resolve IMailPostOfficeService
  -> Create ProjectManager
  -> LoadProductDefinitions
  -> LoadProductModules
  -> ProductCatalog.Set
  -> Prepare application CommandContext
  -> Open startup projects
  -> Phase Running
  -> Notify Started
  -> loop MainLoop(application mailbox only)
  -> Phase Stopping
  -> Notify Stopping
  -> Phase Unloading
  -> Close all projects
  -> Clear mail offices
  -> Unload service instances
  -> Notify Stopped
```

## 4. 主循环

```text
MainLoop
  -> DispatchBackendMails(application mail id)
```

项目会话内部持有 Repository、UniverseContext 和 Universe。Repository 承载数据，UniverseContext 把当前项目数据和计时器传给 Behaviour，Universe 只承载 Behaviour 调度器。每个项目打开后由 `ProjectSession::Start()` 启动独立线程：

```text
ProjectSession::WorkerMain
  -> BindStartup
  -> loop
       PreSwapPDO
       DispatchBackendMails(project backend post office)
       Universe.Tick(project universe context)
       PostSwapPDO
```

`ApplicationHost::OpenProject` 会创建项目、设置项目帧处理函数，并启动该项目线程。这样每个 Project 都有自己的线程、邮箱、Repository 和 ResourceLibrary。

## 5. 命令上下文

宿主保存一个应用级 `CommandContext`，用于暴露应用级依赖。

每次分发 Mail 时，宿主会构造本次命令上下文：

```text
Application command:
  ApplicationContext
  CommandRegistry
  ProjectManager
  MailPostOfficeService

Project command:
  ApplicationContext
  CommandRegistry
  ProjectManager
  ProjectSession
  IRepository
  IUniverse
  CResourceLibrary
  MailPostOfficeService
```

这样命令处理器不需要自己判断全局单例 Repository，也不会误操作其他项目。

## 6. 产品模块

产品模块由产品 manifest 声明，`ApplicationHost` 在加载产品定义后调用 `LoadLibraryA` 加载模块。

产品模块加载后可以通过自注册把组件、Behaviour、Service、命令注册到对应注册表。产品 ID 用于区分产品类型；项目 ID 用于区分打开的项目实例。

## 7. 通信通道

`ApplicationHost` 使用 `IMailPostOfficeService` 管理应用级通信通道：

```text
application mail id -> app channel
```

项目级通信通道归属 `ProjectSession`：

```text
ProjectSession
  MailChannel
    EndA: frontend post office
    EndB: backend post office
```

项目关闭时，`ProjectSession::Close()` 停止项目线程并重置自己的 `MailChannel`，旧项目邮局随之失效。

## 8. 设计原则

- ApplicationContext 只放应用级状态。
- ProductDefinition 只描述产品类型。
- ProjectSession 承载项目实例状态。
- Repository、ResourceLibrary、UniverseContext 都属于 ProjectSession。
- Universe 由 ProjectSession 持有并驱动，但它本身不拥有项目数据。
- MailChannel 和后台工作线程也属于 ProjectSession。
- ApplicationHost 不直接保存单个 Repository 或 Universe。
- 多产品并存靠 ProductID 区分；多项目并存靠 ProjectID 隔离。
