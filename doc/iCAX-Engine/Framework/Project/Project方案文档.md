# Project 方案文档

## 1. 工程位置

```text
src/icax/framework/Project/
```

`Project` 放在 framework 下，因为它不是基础数据结构，而是把 Database、Resources、Behaviour 组合成项目运行单元的框架能力。

## 2. 分层

```text
ApplicationHost
  ProductCatalog
    ProductDefinition: five-axis
    ProductDefinition: flat-cut
    ProductDefinition: welding

  ProjectManager
    ProjectSession A: product=five-axis
      Repository
      ResourceLibrary
      UniverseContext
      Universe
      MailChannel
      WorkThread

    ProjectSession B: product=flat-cut
      Repository
      ResourceLibrary
      UniverseContext
      Universe
      MailChannel
      WorkThread
```

`ApplicationHost` 负责应用级生命周期和项目管理；`Project` 负责产品类型和项目实例。每个项目实例自己运行，不共用同一条后台帧循环。

## 3. 产品类型

`CProductDefinition` 只描述产品类型：

- 产品 ID 和显示名。
- 产品根目录和 manifest 路径。
- 前端入口和协议目录。
- 后台模块列表。
- 启动组件。
- 文件扩展名。

它不持有 Repository、资源对象或运行时状态。

## 4. 项目实例

`CProjectSession` 是多项目隔离边界。每次打开项目都会创建：

- 独立 Repository。
- 独立 ResourceLibrary。
- 独立 UniverseContext。
- 独立 Universe。
- 独立 MailChannel。
- 独立项目工作线程。

`Universe` 只保存 Behaviour 调度器，不保存 Repository，也不作为 Repository 事件订阅者。`ProjectSession` 持有 `UniverseContext`，并把当前项目的 Repository、计时器和应用设置通过 context 传入 Behaviour。

`ProjectSession` 订阅自己的 Repository 事件，再把事件和 `UniverseContext` 转发给 `Universe`。因此数据归属仍在 ProjectSession/Repository，Behaviour 只是被当前项目上下文驱动。

## 5. 打开项目

打开项目流程：

```text
ProjectManager::OpenProject(productId)
  -> ProductCatalog.Require(productId)
  -> Generate project id
  -> Database::GenerateRepository(project id)
  -> create UniverseContext(repository, application settings)
  -> Behaviour::GenerateUniverse()
  -> create ResourceLibrary
  -> create MailChannel
  -> ProjectSession subscribes repository events
  -> set active project if none exists
```

`ProjectManager::OpenProject` 只创建项目实例，不自动启动线程。由 `ApplicationHost::OpenProject` 或其他上层宿主完成运行接入：

```text
ApplicationHost::OpenProject(productId)
  -> ProjectManager.OpenProject(productId)
  -> ProjectSession.SetFrameHandler(dispatch project mailbox)
  -> ProjectSession.Start()
```

如果产品定义包含 `StartupComponent`，`ProjectSession` 在线程启动时执行 `BindStartup`。`BindStartup` 会查找启动组件对应的 Behaviour，并把启动组件添加到 Repository 的 meta entity。

## 6. 通信

项目通信通道归属 `ProjectSession`，不再由全局邮局服务用 ProjectID 统一维护。每个项目内部有自己的 `CMailChannel`：

```text
ProjectSession
  frontend post office
  backend post office
```

`ApplicationHost::GetProjectFrontendPostOffice(projectId)` 先通过 `ProjectManager` 找到项目，再返回该项目自己的 frontend post office。项目线程每帧从自己的 backend post office 收取邮件并分发命令。

应用级命令仍使用 `ApplicationHost::GetApplicationMailID()` 对应的应用级邮局，适合打开项目、列项目、修改应用设置等不属于某个项目的命令。

## 7. 与全局注册表的关系

当前组件、Behaviour 和 Service 仍使用全局注册入口。为了支持多产品并存，产品侧类型必须使用产品命名空间命名，避免类型名冲突。

长期可以演进为：

```text
GlobalRegistry
ProductRegistry
ProjectSession
```

本次改造先把实例状态隔离到 `ProjectSession`，不把产品实例状态塞进全局注册表。
