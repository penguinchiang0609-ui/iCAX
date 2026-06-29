# framework

`framework` 放置后台对象模型和运行规则。它定义组件、实体、仓库、行为运行容器和后台通信等核心概念。

## 运行结构

后台以 `ApplicationHost -> ProductRuntime -> Project` 三层组织：

- `ApplicationHost` 是应用级入口，拥有应用上下文、应用级 mailbox 和应用级服务容器。
- `ProductRuntime` 表达一个已启动产品，负责产品模块加载、产品级 mailbox、产品命令、产品数据和 ProjectCatalog 生命周期。产品模块通过宏登记的 ComponentMeta、Behaviour、ResourceLoader 和 CommandHandler 会被回放到产品级注册表中，Service 回放到应用级服务容器。
- `Project` 表达一个已打开项目。每个项目独占自己的 Repository、ResourceLibrary/ResourcePool、Universe、项目 mail channel 和后台工作线程，并作为 `IProjectContext` 传入 Behaviour 调度。

Service 在整个 Application 内共享，但通信通道不进入 ServiceProvider。`ApplicationHost` 直接持有应用级 `CMailChannelRegistry`，并显式注入 ProductRuntime / Project；PDO 由 Project 自己持有并通过 `IProjectContext::PDOHub()` 暴露。ComponentMeta、Behaviour、ResourceLoader 和 CommandHandler 按 ProductRuntime 隔离；`Universe` 实例、Repository、资源对象、项目 ResourceLoaderRegistry、项目邮箱、项目 PDO 和项目线程全部按 Project 隔离。

## 目录结构

- `Database/`：Repository 单 EC 数据容器、实体、组件、元数据注册、变更日志、事务、撤销重做和快速保存日志。
- `ApplicationContext/`：应用描述、路径、应用级配置和配置读写边界。
- `ApplicationHost/`：应用级后台宿主，负责应用上下文、产品清单、产品运行时、应用级 mailbox、事件订阅和后台工作线程。
- `ProductContext/`：产品定义、产品数据和产品运行上下文接口。
- `Product/`：产品级运行时，负责产品模块加载、产品级 mailbox、最近项目列表和 ProjectCatalog 生命周期。
- `ProjectContext/`：项目运行上下文接口。
- `Services/`：服务接口、服务提供器和自动注册辅助，不承载 Mailbox/PDO。
- `Project/`：主项目和临时项目实例。每个项目独占 Repository、ResourceLibrary、Universe、项目 mail channel 和后台工作线程。
- `Resources/`：工程资源系统，用资源来源字符串统一索引资源条目、对象、元信息、持久化策略和资源加载器。
- `Mailbox/`：后台与前台之间的普通 Mail 通信通道，用于命令、事件、请求响应和低频状态同步；`CMailChannelRegistry` 位于本目录。
- `PDO/`：后台与前台之间的高频可丢弃数据通道，用于同步过程数据；运行时归属 ProjectContext，不作为 Service。
- `CommandHandler/`：Mailbox 之上的后端命令请求、上下文、注册和分发抽象。
- `Behaviour/`：行为运行容器、行为注册和行为调度。

