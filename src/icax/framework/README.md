# framework

`framework` 放置后台对象模型和运行规则。它定义组件、实体、仓库、行为、世界、宇宙和后台通信等核心概念。

## 目录结构

- `Database/`：实体、组件、领域、仓库、元数据注册、变更日志、事务、撤销重做和快速保存日志。
- `ApplicationContext/`：应用描述、路径、应用级配置和配置读写边界。
- `ApplicationHost/`：后台应用宿主，负责配置加载、插件加载、服务装配、Repository/Universe 初始化和主循环入口。
- `Services/`：服务接口、服务提供器、自动注册辅助和框架级 MailPostOfficeService。
- `Mailbox/`：后台与前台之间的普通 Mail 通信通道，用于命令、事件、请求响应和低频状态同步。
- `PDO/`：后台与前台之间的高频可丢弃数据通道，用于同步过程数据。
- `CommandHandler/`：Mailbox 之上的后端命令请求、上下文、注册和分发抽象。
- `Behaviour/`：行为、世界、宇宙和行为调度。
- `legacy-readme.md`：旧目录说明的迁移保留文件。
