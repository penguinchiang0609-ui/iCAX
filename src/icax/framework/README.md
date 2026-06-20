# framework

`framework` 放置后台对象模型和运行规则。它定义组件、实体、仓库、行为运行容器和后台通信等核心概念。

## 运行结构

后台以 `ApplicationHost -> ProductRuntime -> Project` 三层组织：

- `ApplicationHost` 是应用级入口，拥有应用上下文、应用级 mailbox、应用级服务容器，以及应用级 Meta/Behaviour/ResourceLoader 注册表。
- `ProductRuntime` 表达一个已启动产品，负责产品模块加载、产品级 mailbox、产品命令和 ProjectCatalog 生命周期。产品模块通过宏登记的 Service、ComponentMeta、Behaviour、ResourceLoader 会被回放到应用级注册表中，扩展代码不需要手写注册流程。
- `Project` 表达一个已打开项目。每个项目独占自己的 Repository、ResourceLibrary/ResourcePool、UniverseContext、Universe、项目 mail channel 和后台工作线程。

Service 在整个 Application 内共享，`IMailChannelService` 也是应用级服务，负责按 mail id 托管 Application/Product/Project 的通信通道。Behaviour 的定义注册表也在 Application 内共享，但 `Universe` 实例按 Project 创建。数据对象、资源对象、项目邮箱和项目线程全部按 Project 隔离。

## 目录结构

- `Database/`：Repository 单 EC 数据容器、实体、组件、元数据注册、变更日志、事务、撤销重做和快速保存日志。
- `ApplicationContext/`：应用描述、路径、应用级配置和配置读写边界。
- `ApplicationHost/`：应用级后台宿主，负责应用上下文、产品清单、产品运行时、应用级 mailbox、事件订阅和后台工作线程。
- `Product/`：产品级运行时，负责产品模块加载、产品级 mailbox、最近项目列表和 ProjectCatalog 生命周期。
- `Services/`：服务接口、服务提供器、自动注册辅助和应用级 MailChannelService。
- `Project/`：主项目和临时项目实例。每个项目独占 Repository、ResourceLibrary、Universe、项目 mail channel 和后台工作线程。
- `Resources/`：工程资源系统，用资源来源字符串统一索引资源条目、对象、元信息、持久化策略和资源加载器。
- `Mailbox/`：后台与前台之间的普通 Mail 通信通道，用于命令、事件、请求响应和低频状态同步。
- `PDO/`：后台与前台之间的高频可丢弃数据通道，用于同步过程数据。
- `CommandHandler/`：Mailbox 之上的后端命令请求、上下文、注册和分发抽象。
- `Behaviour/`：行为运行容器、行为注册和行为调度。
- `legacy-readme.md`：旧目录说明的迁移保留文件。
