# Backend 概要文档

## 1. 定位

iCAX backend 是 C++ 后台框架，负责应用宿主、产品运行时、项目数据、资源、行为调度、命令分发和高频状态同步。

backend 不绑定具体 UI 技术。Qt、WPF、H5 或其他前端宿主都应通过 bridge 获取 mailbox endpoint，并通过 Mailbox、PDO 和 Resource 与 backend 交互。

## 2. 总体结构

```text
ApplicationHost
  ApplicationContext
  CServiceProvider
  CMailChannelRegistry
    applicationChannelId -> CMailChannel
    productChannelId*    -> CMailChannel
    projectChannelId*    -> CMailChannel
  ProductRuntime*
    ProductDefinition
    ProductData
    Product mail id
    Product IMetaRegistry
    Product IBehaviourRegistry
    Product ResourceLoaderRegistry
    Product CommandRegistry / CommandDispatcher
    ProjectCatalog*
      Main Project
      Transient Project*
        Repository
        ResourceLibrary / ResourcePool
        Universe
        Project mail id
        Project WorkThread
```

核心规则：

- ApplicationHost 是应用级入口。
- ProductRuntime 是产品级入口。
- Project 是数据和运行隔离边界。
- ServiceProvider 在 Application 级共享。
- ComponentMeta、Behaviour 定义、ResourceLoader 和 CommandHandler 按 ProductRuntime 回放到产品级注册表。
- MailChannelRegistry 在 ApplicationHost 中共享，但不进入 ServiceProvider；channel 按 mail id 显式创建和删除。
- PDO 不进入 ServiceProvider；项目 PDOHub 归 Project 持有，并通过 ProjectContext 暴露。
- Repository、资源对象、项目 ResourceLoaderRegistry、Universe 实例、项目 mail channel、项目 PDO 和项目线程按 Project 隔离。

## 3. 启动流程

```text
Host process
  -> create ApplicationHost
  -> frontend bridge receives ApplicationHost frontend post office
  -> ApplicationHost.Start()
       create ApplicationContext
       create application ServiceProvider
       create CMailChannelRegistry
       create application mail channel
       bind ApplicationContext runtime capabilities
       replay framework registrations
       register application commands
       start configured product?
       enter application loop
```

前端选择产品后：

```text
Frontend
  -> App.ListProducts / App.GetState
  -> App.StartProduct(productId)

ApplicationHost
  -> create ProductRuntime
  -> ProductRuntime.Start()
       load product modules
       replay module Service registrations
       replay module ComponentMeta registrations to product MetaRegistry
       replay module Behaviour registrations to product BehaviourRegistry
       replay module ResourceLoader registrations to product ResourceLoaderRegistry
       replay module Command registrations to product CommandRegistry
       create product mail channel
       register product commands
  <- productChannelId / frontendEntry / recentProjects
```

打开项目后：

```text
Frontend
  -> Product.OpenProjectCatalog

ProductRuntime
  -> create ProjectCatalog
  -> open main Project
  -> create project-local ResourceLoaderRegistry from product modules
  -> create project mail channel
  -> create LocalProjectRuntime
  -> start Project thread
  <- catalogId / projectId / projectChannelId / project frontend post office
```

## 4. 通信模型

`Mailbox` 提供基础 `CMailQueue`、`CMailChannel`、`CMailPostOffice` 和 `CMailChannelRegistry`。`ApplicationHost` 直接持有应用级 channel 目录：

```text
applicationChannelId -> application CMailChannel
productChannelId     -> product CMailChannel
projectChannelId     -> project CMailChannel
```

`ApplicationHost`、`ProductRuntime` 和 `Project` 只保存自己的 mail id，并从显式注入的 `CMailChannelRegistry` 获取 frontend/backend post office。`GetFrontendPostOffice` / `GetBackendPostOffice` 不隐式创建 channel；创建和删除必须显式走 `CreateChannel` / `RemoveChannel`。Application/Product/ProjectContext 对外暴露本层 post office，调用方不需要从 ServiceProvider 解析通信能力。

`CMailPostOffice` 是轻量 endpoint 视图，不拥有队列。上级运行体负责向前端 bridge 发放下级运行体的 frontend post office。运行体停止或关闭时删除自己的 channel，旧 post office 自动失效。

Mailbox 用于命令、事件、请求响应和低频状态；PDO 用于高频、可丢弃、可批量覆盖的过程状态。大资源不应塞进 Mail payload，应通过 Resource 机制按 source/id 获取。

## 5. 数据模型

Database 当前以 Repository 为单 EC 数据容器：

- Entity 表达对象身份。
- Component 表达实体的能力、身份或状态。
- ComponentMeta 描述字段、持久化、变更策略和派生字段。
- Repository 负责实体组件管理、事件、变更日志、事务、撤销重做和快速保存日志。

Project 创建自己的 Repository，因此不同 Project 的实体和组件实例互不可见。

## 6. 行为模型

Behaviour 定义在 ProductRuntime 的产品级注册表中注册，Project 创建自己的 Universe 实例。Universe 不持有数据；Project 在调度 `Universe::Tick` 或转发 Repository 事件时，显式传入 `ApplicationContext`、`ProductContext` 和 `ProjectContext`。

因此 Behaviour 定义属于产品，行为执行上下文属于当前 Project。

## 7. 资源模型

Resource 对象按 Project 隔离：

```text
Project
  -> CResourceLibrary
       -> CResourcePool
```

ResourceLoader 注册动作由产品模块提供，ProductRuntime 会回放到产品级注册表；每个 Project 创建自己的 ResourceLoaderRegistry，再从产品模块回放注册动作，因此 loader 实例和资源对象都不跨 Project 共享：

```text
ProductRuntime
  -> product ResourceLoaderRegistry
Project
  -> project ResourceLoaderRegistry
  -> CResourceLibrary
  -> CResourcePool
```

资源 source 字符串就是资源身份，例如：

```text
D:/assets/robot.fbx
project://textures/base.png
memory://preview/model
```

`CResourceInfo::Persistence` 标记资源保存策略：

- `RuntimeOnly`：只在运行时存在。
- `Embedded`：保存工程时需要内嵌。
- `External`：保存工程时记录外部引用。

## 8. 扩展模型

扩展模块通过宏降低编写成本：

- Service 注册进入 ServiceRegistrationCatalog。
- ComponentMeta 注册进入 MetaRegistrationCatalog。
- Behaviour 注册进入 BehaviourRegistrationCatalog。
- ResourceLoader 注册进入 ResourceLoaderRegistrationCatalog。

ProductRuntime 加载产品模块后，按模块路径回放注册动作：

- Service 回放到 ApplicationHost 的应用级 ServiceProvider。
- ComponentMeta 回放到 ProductRuntime 的产品级 MetaRegistry。
- Behaviour 回放到 ProductRuntime 的产品级 BehaviourRegistry。
- ResourceLoader 回放到 ProductRuntime 的产品级 ResourceLoaderRegistry，并在创建 Project 时再次回放到项目级 ResourceLoaderRegistry。
- CommandHandler 回放到 ProductRuntime 的产品级 CommandRegistry。

产品 DLL 在进程内只加载一次。由于自动注册宏不提供注销能力，模块加载后按进程常驻处理。

自动注册 Catalog 是追加式的进程级目录：模块加载时追加注册回放函数，ProductRuntime 根据模块路径把这些回放函数写入自己的产品级注册表。当前不支持插件热卸载、热替换或从 Catalog 中删除注册记录。若需要卸载某个项目的行为，只能解除该 Project/Universe 内的 Behaviour 实例绑定；这不会影响产品级注册表，也不会卸载 DLL。

## 9. 线程模型

当前进程内线程边界：

- ApplicationHost 拥有应用级工作线程，轮询 application/product mailbox。
- ProductRuntime 不创建自己的工作线程。
- 每个 Project 拥有自己的项目线程，并由该线程驱动本 Project 的 Repository、Universe 和 Behaviour 实例。

Behaviour 不提供内部并发保护。`Tick`、Repository 事件转发、Behaviour 绑定/解绑、暂停/恢复帧更新应在同一个 Project 线程内发生。暂停帧更新只跳过 `PreUpdate / Update / PostUpdate`，不拦截 `Start` 和 Repository 生命周期事件。组件移除时，`OnDestroyImmediate` 在 Repository remove 链路内同步触发，`OnDestroy` 在下一次 `Tick` 开始的统一销毁阶段触发。`OnDestroy` 接收 `CComponentDestroyInfo` 快照，不接收已经脱离 Entity/Repository 的组件对象；若 `OnDestroy` 中继续删除其他组件，同一销毁阶段会持续处理到队列为空。前端线程、ApplicationHost 线程或其他 Project 线程不直接调用 Behaviour；跨线程输入通过 Mailbox、PDO 或命令通道进入目标 Project，再由目标 Project 线程消费。

Behaviour 回调不做异常拦截或过滤，运行错误按第一现场暴露。访问冲突、堆破坏等 native 崩溃仍可能影响整个进程；若未来需要硬隔离，应通过 `IProjectRuntime` 增加每 Project 一个 worker process 的实现。

## 10. 相关文档

- `Framework/ApplicationHost/`
- `Framework/Product/`
- `Framework/Project/`
- `Framework/Database/`
- `Framework/Resources/`
- `Framework/Mailbox/`
- `Framework/PDO/`
- `Framework/CommandHandler/`
- `Framework/Behaviour/`

