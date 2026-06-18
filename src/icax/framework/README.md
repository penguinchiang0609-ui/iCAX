# framework

`framework` 放置后台对象模型和运行规则。它定义组件、实体、仓库、行为运行容器和后台通信等核心概念。

## 目录结构

- `Database/`：Repository 单 EC 数据容器、实体、组件、元数据注册、变更日志、事务、撤销重做和快速保存日志。
- `ApplicationContext/`：应用描述、路径、应用级配置和配置读写边界。
- `ApplicationHost/`：后台工作区宿主，负责应用上下文、产品目录、多项目会话、命令分发、事件订阅和后台工作线程。
- `Services/`：服务接口、服务提供器、自动注册辅助和框架级 MailPostOfficeService。
- `Project/`：产品定义、产品目录、项目会话和多项目管理。每个项目会话独占 Repository、ResourceLibrary 和 Universe。
- `Resources/`：工程资源系统，用资源来源字符串统一索引资源条目、对象、元信息、持久化策略和资源加载器。
- `Mailbox/`：后台与前台之间的普通 Mail 通信通道，用于命令、事件、请求响应和低频状态同步。
- `PDO/`：后台与前台之间的高频可丢弃数据通道，用于同步过程数据。
- `CommandHandler/`：Mailbox 之上的后端命令请求、上下文、注册和分发抽象。
- `Behaviour/`：行为运行容器、行为注册和行为调度。
- `legacy-readme.md`：旧目录说明的迁移保留文件。
