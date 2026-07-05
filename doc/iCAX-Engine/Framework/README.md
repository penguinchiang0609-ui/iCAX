# Framework

`Framework` 目录记录 iCAX Engine 框架层项目的规格和方案文档。

框架层承接 Foundation 基础能力，并向 ApplicationHost、插件和业务代码提供可复用的后台应用框架能力。

当前后台以 `ApplicationHost -> ProductRuntime -> Project -> Scene` 四层组织。ApplicationHost 持有应用级 ServiceProvider 和 MailChannelRegistry；ProductRuntime 持有产品级 MetaRegistry、BehaviourRegistry、ResourceLoaderRegistry、CommandRegistry、产品 mailbox 和 ProductData；Project 是项目管理容器，承载项目身份、路径和 ProjectSetting；Scene 是运行现场隔离边界，独占 Repository、ResourceLibrary/ResourcePool、Scene ResourceLoaderRegistry、Universe、Scene mail channel、PDOHub 和 Scene 线程。

## 目录结构

```text
Framework/
  ApplicationHost/
    ApplicationHost规格文档.md
    ApplicationHost方案文档.md
    README.md
  ApplicationContext/
    ApplicationContext规格文档.md
    ApplicationContext方案文档.md
    README.md
  Behaviour/
    Behaviour规格文档.md
    Behaviour方案文档.md
    README.md
  CommandHandler/
    CommandHandler规格文档.md
    CommandHandler方案文档.md
    README.md
  Database/
    Database规格文档.md
    Database方案文档.md
    README.md
  Mailbox/
    Mailbox规格文档.md
    Mailbox方案文档.md
    README.md
  PDO/
    PDO规格文档.md
    PDO方案文档.md
    README.md
  Product/
    Product规格文档.md
    Product方案文档.md
    README.md
  Project/
    Project规格文档.md
    Project方案文档.md
    README.md
  Resources/
    Resources规格文档.md
    Resources方案文档.md
    README.md
  Services/
    Services规格文档.md
    Services方案文档.md
    README.md
```

## 当前项目

- `ApplicationContext`：应用描述、路径、应用级配置和配置读写边界。
- `ApplicationHost`：应用级后台宿主，负责应用上下文、产品清单、产品运行时、应用级 mailbox、事件订阅和后台工作线程。
- `Behaviour`：Component 对应的行为系统，负责 Universe、Behaviour 注册、生命周期回调、调度顺序和销毁队列。
- `CommandHandler`：Mailbox 之上的后端命令处理抽象。
- `Database`：Repository 单 EC 数据容器、实体组件管理、字段元数据、事件、版本和派生字段。
- `Mailbox`：后台与前台之间的普通 Mail 通信通道。
- `PDO`：后台与前台之间的高频可丢弃数据通道。
- `Product`：产品级运行时，负责产品模块加载、产品级 mailbox、最近项目列表和 ProjectCatalog 生命周期。
- `Project`：项目管理容器和 Scene 运行现场。Project 管理主 Scene 与子 Scene；Scene 独占数据、资源、Universe、mail channel、PDOHub 和线程。
- `Resources`：工程资源系统。资源对象和 Scene ResourceLoaderRegistry 按 Scene 隔离，ResourceLoader 注册动作由产品模块回放。
- `Services`：服务接口、服务提供器、自动注册辅助和应用级 MailChannelRegistry。

