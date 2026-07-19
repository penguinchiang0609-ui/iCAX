# Backend 概要文档

## 1. 定位

iCAX backend 是 C++ 后台框架，负责应用宿主、产品运行时、项目数据、资源、行为调度、命令分发和高频状态同步。

backend 不绑定具体 UI 技术。Qt、WPF、H5 或其他前端宿主都应通过 bridge 获取 Facade endpoint，并通过 Facades、PDO 和 Resource 与 backend 交互。

## 2. 总体结构

```text
ApplicationRuntime
  ApplicationContext
  CServiceProvider
  CFacadeChannelRegistry
    applicationChannelId -> CFacadeChannel
    productChannelId*    -> CFacadeChannel
    sceneChannelId*      -> CFacadeChannel
  ProductRuntime*
    ProductDefinition
    ProductData
    Product Facade channel id
    Product IMetaRegistry
    Product IBehaviourRegistry
    Product ResourceLoaderRegistry
    Product FacadeRegistry / FacadeInvoker
    ProjectCatalog*
      Project*
        ProjectID / ProjectPath / ProjectSetting
        Main Scene
          Repository
          ResourceLibrary / ResourcePool
          Universe
          Scene Facade channel id
          Scene PDOHub
          Scene WorkThread
        Child Scene*
          Repository
          ResourceLibrary / ResourcePool
          Universe
          Scene Facade channel id
          Scene PDOHub
          Scene WorkThread
```

核心规则：

- ApplicationRuntime 是应用级入口。
- ProductRuntime 是产品级入口。
- Project 是项目级管理容器，承载项目身份、路径和跟图纸走的 ProjectSetting。
- Scene 是数据和运行隔离边界。
- ServiceProvider 在 Application 级共享。
- ComponentMeta、Behaviour 定义、ResourceLoader 和 Facades 按 ProductRuntime 回放到产品级注册表。
- FacadeChannelRegistry 在 ApplicationRuntime 中共享，但不进入 ServiceProvider；channel 按 Facade channel id 显式创建和删除。
- PDO 不进入 ServiceProvider；Scene PDOHub 归 Scene 持有，并通过 SceneContext 暴露。
- Repository、资源对象、Scene ResourceLoaderRegistry、Universe 实例、Scene Facade channel、Scene PDO 和 Scene 线程按 Scene 隔离。

## 3. 启动流程

```text
Host process
  -> create ApplicationRuntime
  -> frontend bridge receives ApplicationRuntime frontend Facade endpoint
  -> ApplicationRuntime.Start()
       create ApplicationContext
       create application ServiceProvider
       create CFacadeChannelRegistry
       create application Facade channel
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

ApplicationRuntime
  -> create ProductRuntime
  -> ProductRuntime.Start()
       load product modules
       replay module Service registrations
       replay module ComponentMeta registrations to product MetaRegistry
       replay module Behaviour registrations to product BehaviourRegistry
       replay module ResourceLoader registrations to product ResourceLoaderRegistry
       replay module Command registrations to product FacadeRegistry
       create product Facade channel
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
  -> create main Scene
  -> create scene-local ResourceLoaderRegistry from product modules
  -> create scene Facade channel
  -> create LocalProjectRuntime
  -> start main Scene thread
  <- catalogId / projectId / mainSceneId / mainSceneChannelId / scene frontend Facade endpoint
```

## 4. 通信模型

`Facades` 提供基础 `CFacadeQueue`、`CFacadeChannel`、`CFacadeEndpoint` 和 `CFacadeChannelRegistry`。`ApplicationRuntime` 直接持有应用级 channel 目录：

```text
applicationChannelId -> application CFacadeChannel
productChannelId     -> product CFacadeChannel
sceneChannelId       -> scene CFacadeChannel
```

`ApplicationRuntime`、`ProductRuntime` 和 `Scene` 只保存自己的 Facade channel id，并从显式注入的 `CFacadeChannelRegistry` 获取 frontend/backend Facade endpoint。`GetFrontendFacadeEndpoint` / `GetBackendFacadeEndpoint` 不隐式创建 channel；创建和删除必须显式走 `CreateChannel` / `RemoveChannel`。通信端点由拥有运行时与 channel 生命周期的 `ApplicationRuntime`、`ProductRuntime`、`ProjectScene` 暴露，不放入 `ApplicationContext`；调用方也不需要从 ServiceProvider 解析通信能力。

`CFacadeEndpoint` 是轻量 endpoint 视图，不拥有队列。上级运行体负责向前端 bridge 发放下级运行体的 frontend Facade endpoint。运行体停止或关闭时删除自己的 channel，旧 Facade endpoint 自动失效。

Facades 用于命令、事件、请求响应和低频状态；PDO 用于高频、可丢弃、可批量覆盖的过程状态。大资源不应塞进 Facade payload，应通过 Resource 机制按 source/id 获取。

## 5. 数据模型

Database 当前以 Repository 为单 EC 数据容器：

- Entity 表达对象身份。
- Component 表达实体的能力、身份或状态。
- ComponentMeta 描述字段、持久化、变更策略和派生字段。
- Repository 负责实体组件管理、事件、变更日志、事务、撤销重做和快速保存日志。

Scene 创建自己的 Repository，因此不同 Scene 的实体和组件实例互不可见。Project 只管理主 Scene 和子 Scene，不直接承载 EC 数据。

## 6. 行为模型

Behaviour 定义在 ProductRuntime 的产品级注册表中注册，Scene 创建自己的 Universe 实例。Universe 不持有数据；Scene 在调度 `Universe::Tick` 或转发 Repository 事件时，显式传入 `ApplicationContext`、`ProductContext`、`ProjectContext` 和 `SceneContext`。

因此 Behaviour 定义属于产品，ProjectContext 提供项目级参数，SceneContext 提供当前运行现场。

## 7. 资源模型

Resource 对象按 Scene 隔离：

```text
Scene
  -> CResourceLibrary
       -> CResourcePool
```

ResourceLoader 注册动作由产品模块提供，ProductRuntime 会回放到产品级注册表；每个 Scene 创建自己的 ResourceLoaderRegistry，再从产品模块回放注册动作，因此 loader 实例和资源对象都不跨 Scene 共享：

```text
ProductRuntime
  -> product ResourceLoaderRegistry
Scene
  -> scene ResourceLoaderRegistry
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

- Service 回放到对应 ProductContext 的产品级 ServiceProvider；应用级服务环境由 ApplicationContext 管理，只能由 ApplicationRuntime 初始化。
- ComponentMeta 回放到 ProductRuntime 的产品级 MetaRegistry。
- Behaviour 回放到 ProductRuntime 的产品级 BehaviourRegistry。
- ResourceLoader 回放到 ProductRuntime 的产品级 ResourceLoaderRegistry，并在创建 Scene 时再次回放到 Scene 级 ResourceLoaderRegistry。
- Facades 回放到 ProductRuntime 的产品级 FacadeRegistry。

产品 DLL 在进程内只加载一次。由于自动注册宏不提供注销能力，模块加载后按进程常驻处理。

自动注册 Catalog 是追加式的进程级目录：模块加载时追加注册回放函数，ProductRuntime 根据模块路径把这些回放函数写入自己的产品级注册表。当前不支持插件热卸载、热替换或从 Catalog 中删除注册记录。若需要卸载某个 Scene 的行为，只能解除该 Scene/Universe 内的 Behaviour 实例绑定；这不会影响产品级注册表，也不会卸载 DLL。

## 9. 线程模型

当前进程内线程边界：

- ApplicationRuntime 拥有应用级工作线程，轮询 application Facade。
- ProductRuntime 拥有产品级工作线程，轮询 product Facade。
- 每个 Scene 拥有自己的 Scene 线程，并由该线程驱动本 Scene 的 Repository、Universe 和 Behaviour 实例。

Behaviour 不提供内部并发保护。`Tick`、Repository 事件转发、Behaviour 绑定/解绑、暂停/恢复帧更新应在同一个 Scene 线程内发生。暂停帧更新只跳过 `PreUpdate / Update / PostUpdate`，不拦截 `Start` 和 Repository 生命周期事件。组件移除时，`OnDestroyImmediate` 在 Repository remove 链路内同步触发，`OnDestroy` 在下一次 `Tick` 开始的统一销毁阶段触发。`OnDestroy` 接收 `CComponentDestroyInfo` 快照，不接收已经脱离 Entity/Repository 的组件对象；若 `OnDestroy` 中继续删除其他组件，同一销毁阶段会持续处理到队列为空。前端线程、ApplicationRuntime 线程或其他 Scene 线程不直接调用 Behaviour；跨线程输入通过 Facades、PDO 或命令通道进入目标 Scene，再由目标 Scene 线程消费。

Behaviour 回调不做异常拦截或过滤，运行错误按第一现场暴露。访问冲突、堆破坏等 native 崩溃仍可能影响整个进程；若未来需要硬隔离，应通过 `IProjectRuntime` 增加每 Project 一个 worker process 的实现。

## 10. 相关文档

- `Framework/ApplicationRuntime/`
- `Framework/Product/`
- `Framework/Project/`
- `Framework/Database/`
- `Framework/Resources/`
- `Framework/Facades/`
- `Framework/PDO/`
- `Framework/Facades/`
- `Framework/Behaviour/`

