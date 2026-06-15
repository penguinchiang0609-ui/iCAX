# Framework

`Framework` 目录记录 iCAX Engine 框架层项目的规格和方案文档。

框架层承接 Foundation 基础能力，并向 ApplicationHost、插件和产品代码提供可复用的应用框架能力。

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
  Services/
    Services规格文档.md
    Services方案文档.md
    README.md
```

## 当前项目

- `ApplicationContext`：应用描述、路径、应用级配置和配置读写边界。
- `ApplicationHost`：后台应用宿主，负责配置加载、插件加载、服务装配、Repository/Universe 初始化和主循环入口。
- `CommandHandler`：Mailbox 之上的后端命令处理抽象。
- `Database`：EC 数据模型、Domain 隔离、实体组件管理、字段元数据、事件、版本和派生字段。
- `Mailbox`：后台与前台之间的普通 Mail 通信通道。
- `PDO`：后台与前台之间的高频可丢弃数据通道。
- `Services`：服务接口、服务提供器、自动注册辅助和框架级服务实现。
