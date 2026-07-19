# framework

`framework` 放置后台对象模型和运行规则。它定义组件、实体、仓库、行为运行容器和后台通信等核心概念。

## 运行结构

后台以 `ApplicationRuntime -> ProductRuntime -> Project -> Scene` 四层组织：

- `ApplicationRuntime` 是应用级入口，拥有应用上下文的生命周期、应用级线程与 Facade；`ApplicationContext` 自身管理应用配置、路径、数据和应用级服务环境。
- `ProductRuntime` 表达一个已启动产品，负责产品模块加载、产品级 Facade、产品命令、产品数据和 ProjectCatalog 生命周期。产品模块通过宏登记的 Service、ComponentMeta、Behaviour、ResourceLoader 和 Facades 全部回放到当前 ProductContext 环境。
- `Project` 表达一个已打开项目的管理容器，只承载项目身份、路径和跟图纸走的 ProjectSetting，并作为 `IProjectContext` 传入 Behaviour/Facade 调用。
- `Scene` 表达一个独立运行或编辑现场。每个 Scene 独占 Repository、ResourceLibrary/ResourcePool、Universe、Scene Facade channel、PDOHub 和后台工作线程，并作为 `ISceneContext` 传入 Behaviour/Facade 调用。

ApplicationContext 管理应用级 ServiceProvider，ProductContext 管理产品级 ServiceProvider；产品模块不能修改应用级服务环境。通信通道不进入 Context 的服务容器，`ApplicationRuntime` 直接持有应用级 `CFacadeChannelRegistry`，并显式注入 ProductRuntime / Project / Scene。PDO、Repository、Universe 和资源对象由 SceneContext 管理，Scene 线程、调度与通信生命周期由 Scene Runtime 管理。

## 目录结构

- `Database/`：Repository 单 EC 数据容器、实体、组件、元数据注册、变更日志、事务、撤销重做和快速保存日志。
- `ApplicationContext/`：应用描述、路径、应用级配置和配置读写边界。
- `ApplicationRuntime/`：应用级后台运行时，负责应用上下文生命周期、产品清单、产品运行时、应用级 Facade、事件订阅和后台工作线程。
- `ProductContext/`：产品定义、产品数据和产品运行上下文接口。
- `Product/`：产品级运行时，负责产品模块加载、产品级 Facade、最近项目列表和 ProjectCatalog 生命周期。
- `ProjectContext/`：项目上下文与场景上下文接口；ProjectContext 只表达项目级信息，SceneContext 表达运行现场能力。
- `Services/`：服务接口、服务提供器和自动注册辅助，不承载 Facades/PDO。
- `Project/`：项目管理容器和 Scene 运行现场实现。Project 管理主 Scene 与子 Scene；Scene 独占 Repository、ResourceLibrary、Universe、Facade channel、PDOHub 和后台工作线程。
- `Resources/`：工程资源系统，用资源来源字符串统一索引资源条目、对象、元信息、持久化策略和资源加载器。
- `Facades/`：后台与前台之间的 Facade 调用体系，用于命令、事件、请求响应和低频状态同步；包含方法注册、调用与 `CFacadeChannelRegistry` 通信通道。
- `PDO/`：后台与前台之间的高频可丢弃数据通道，用于同步过程数据；运行时归属 SceneContext，不作为 Service。
- `Behaviour/`：行为运行容器、行为注册和行为调度。

