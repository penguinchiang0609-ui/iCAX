# iCAX 白皮书 v0.1 目录结构

# 1. 引言（Introduction）

## 1.1 项目背景

- CAD/CAM 系统的复杂性挑战
- 大规模工程数据管理问题
- UI 与 Engine 解耦需求
- 插件化生态需求

## 1.2 设计目标

iCAX 的核心目标：

- 高性能
- 高稳定性
- 完全插件化
- 可撤销还原的数据架构
- UI 与 Engine 完全解耦
- 可扩展的工业级架构

## 1.3 核心设计原则

iCAX 遵循以下核心原则：

- 数据驱动
- 无锁优先
- 状态与行为分离
- 持久化与过程数据分离
- 插件优先
- 确定性执行

------

# 2. 总体架构概览（Architecture Overview）

## 2.1 系统整体结构

```text
Frontend Host / Bridge
   ↑ ↓ Mailbox / PDO / Resource
ApplicationHost
   ├── ApplicationContext
   ├── Application MailChannel
   ├── Application ServiceProvider
   ├── Application Meta / Behaviour / ResourceLoader Registry
   └── ProductRuntime*
        ├── Product MailChannel
        └── ProjectCatalog*
             └── Project*
                  ├── Repository
                  ├── ResourceLibrary / ResourcePool
                  ├── UniverseContext / Universe
                  ├── Project MailChannel
                  └── Project WorkThread
```

------

## 2.2 架构分层

iCAX 分为：

| 层级   | 名称          | 职责           |
| ------ | ------------- | -------------- |
| 层级   | 名称                    | 职责                         |
| ------ | ----------------------- | ---------------------------- |
| UI层   | Frontend Host / Bridge  | 用户交互、渲染和 endpoint 管理 |
| 宿主层 | ApplicationHost         | 应用上下文、产品清单、应用级通道 |
| 产品层 | ProductRuntime          | 产品模块加载、产品命令、项目目录 |
| 项目层 | Project                 | Repository、Resource、Universe 和项目线程 |
| 通信层 | Mailbox / PDO / Resource | 命令、状态和资源访问           |
| 扩展层 | Plugins / Modules       | 业务扩展和自动注册             |

# 3. 数据架构（Database Architecture）

本章描述 iCAX 的持久化数据模型。

------

## 3.1 Entity-Component 模型

Database 基于 EC 模型：

```
Repository
 └── Entity
      └── Components
```

特点：

- Entity 为组件容器
- Component 为数据载体
- Component 不包含行为

------

## 3.2 Repository

Repository 是数据库顶级容器：

职责：

- 管理项目内所有 Entity/Component 数据
- 管理持久化
- 管理事务

Repository 由 Project 持有。临时编辑、预览和导入由轻量 Project 或 Repository 快照表达。Universe 只承载行为调度，不对应也不拥有 Repository；Project 通过 UniverseContext 把当前 Repository 传给 Behaviour。

------

## 3.3 Component

Component 是最小持久化单元：

特点：

- 仅包含字段
- 无行为
- 支持事件通知
- 支持序列化

示例：

```
struct TransformComponent
{
    float x;
    float y;
    float z;
};
```

------

## 3.4 字段级事件系统

Component 字段修改自动触发：

```
OnModifying
OnModified
```

用于：

- Behaviour 响应
- Undo/Redo 记录
- UI 更新

## 3.5 事务系统（Transaction）

支持：

- 原子修改
- Undo/Redo
- 快速回滚

------

## 3.6 持久化系统

支持：

- 增量保存
- 快速加载
- 崩溃恢复

------

# 4. ECS 行为架构（Behaviour Architecture）

iCAX ECS 由：

Database + Behaviour

组成。

------

## 4.1 Universe

Universe 是行为运行容器：

职责：

- 管理 Behaviour 调度器
- 按 Project 传入的 UniverseContext 执行 Behaviour

------

## 4.2 Behaviour

Behaviour 是系统逻辑执行单元。

特点：

- 无状态
- 每 Component 类型一个 Behaviour

------

生命周期：

```
OnWake
OnStart
OnPreUpdate
OnUpdate
OnPostUpdate
OnDestroy
OnModifying
OnModified
```

------

## 4.4 Behaviour 与 Component 映射关系

```
Component Type
    ↓
Behaviour Instance
```

一对一映射。

------

# 5. 服务架构（Service Architecture）

Service 提供基础能力：

示例：

```
RenderService
InputService
GeometryService
UndoRedoService
```

Service 特点：

- Application 级共享
- 依赖注入
- 独立生命周期

------

# 6. PDOHub 与过程数据架构（Process Data Architecture）

（已完成）

内容包括：

- PDOHub
- 字段级同步
- 双缓冲
- Frontend/backend 通信

------

# 7. 邮件系统（Mail System）

Mail 是命令通信机制。

PDO 是状态同步机制。

区别：

| 类型   | PDO  | Mail |
| ------ | ---- | ---- |
| 类型   | 状态 | 命令 |
| 持久化 | 否   | 可选 |
| 用途   | 同步 | 控制 |

示例：

```
CreateEntityMail
DeleteEntityMail
ModifyComponentMail
```

------

Mail 用于：

- 修改 Database
- 触发行为
- 支持 Undo/Redo

------

# 8. 插件架构（Plugin Architecture）

iCAX 完全插件化。

插件包含：

```
Components
Behaviours
Services
PDO
Descriptor
```

------

## 8.1 插件描述文件

描述：

```
名称
版本
依赖插件
版本兼容性
```

版本规则：

```
Major.Minor.Patch

Major change = breaking change
```

------

## 8.2 插件注册机制

插件加载时注册：

```
Component Types
Behaviour Types
Service Types
PDO Types
```

------

## 8.3 插件隔离

插件之间：

- 通过 Database 通信
- 通过 PDO 通信
- 通过 Mail 通信

完全解耦。

------

# 9. Engine 执行模型（Execution Model）

Engine 每帧执行：

```
Swap PDO Input
Process Mail
Update Behaviours
Swap PDO Output
```

保证：

- 无锁执行
- 确定性执行

------

# 10. UI 与 Engine 解耦架构

特点：

- 不共享对象
- 不使用锁
- 不阻塞

通信：

```
PDO → 状态
Mail → 命令
```

------

# 11. Undo/Redo 架构

基于：

- Component 修改记录
- Transaction

支持：

- 无限撤销
- 高性能回滚

------

# 12. 崩溃恢复与可靠性

支持：

- 崩溃恢复
- 自动保存
- 数据一致性保证

------

# 13. 性能设计

优化：

- 无锁架构
- Cache-friendly 数据布局
- 字段级同步
- 增量更新

------

# 14. 可扩展性设计

支持：

- 无限插件扩展
- 新 Component 类型
- 新 Behaviour 类型
- 新 Service 类型
- 新 PDO 类型

无需修改核心引擎。

------

# 15. 应用场景

适用于：

- CAD
- CAM
- CAE
- 数字孪生
- 实时仿真
- 工业软件平台

------

# 16. 总结

iCAX 提供：

- 工业级 ECS 架构
- 高性能数据架构
- 完全插件化系统
- 无锁线程模型
- 高可靠性数据管理

是下一代工业软件平台架构。

