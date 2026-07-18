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
                  ├── Universe
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
| 产品层 | ProductRuntime          | 产品模块加载、Facade、项目目录 |
| 项目层 | Project                 | Repository、Resource、Universe 和项目线程 |
| 通信层 | Mailbox / PDO / Resource | 调用、状态和资源访问           |
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

Repository 由 Project 持有。临时编辑、预览和导入由轻量 Project 或 Repository 快照表达。Universe 只承载行为调度，不对应也不拥有 Repository；Project 在调度时显式传入 ApplicationContext、ProductContext 和 ProjectContext。

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
- 按 Project 传入的 Application/Product/Project 三层上下文执行 Behaviour

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
OnDestroyImmediate
OnDestroy
OnModifying
OnModified
```

其中 `OnDestroyImmediate` 在组件移除链路中同步触发；`OnDestroy` 在下一次 Tick 开头的统一销毁阶段触发，参数是 `CComponentDestroyInfo` 销毁快照，而不是已经脱离 Entity/Repository 的组件对象。

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

Mail 是离散交互的传输机制，用于承载 Facade 调用、调用结果和事件。

PDO 是状态同步机制。

区别：

| 类型   | PDO  | Mail |
| ------ | ---- | ---- |
| 类型   | 状态 | 调用 / 结果 / 事件 |
| 持久化 | 否   | 可选 |
| 用途   | 同步 | 控制 |

示例：

```
Machine.Jog
WorkpieceModel.Import
Project.Undo
```

------

Mail 用于：

- 承载 `FacadeName.MethodName` 调用；
- 返回结构化调用结果和错误；
- 发布离散业务事件；
- 在 Facade 内部触发领域操作和 Undo/Redo 事务。

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
PDO → 连续状态
Mail → Facade 调用 / 结果 / 事件
```

## 10.1 Facade：面向对象的对外交互协议

UI 与 Engine 不共享内存中的业务对象，但产品仍然需要向 UI、脚本和外部集成提供自然、稳定的业务交互方式。Facade 用于定义这个产品边界。

Facade 是产品定义的**面向对象交互协议**。它将一个稳定的业务概念组织为可读取的属性、可调用的方法以及可订阅的事件。它表达产品承诺提供什么能力，而不表达这些能力在后端由哪些对象实现。

Facade 不是：

- 后端 C++ 对象的远程代理；
- Entity 或 Component 的直接包装；
- ECS 数据结构的通用反射接口；
- 允许调用方任意读写内部字段的通道；
- 机床实例、轴、寄存器或其他硬件元素的地址空间。

Facade 方法的公开名称固定为两段：

```text
FacadeName.MethodName
```

例如 `Machine.Jog`、`WorkpieceModel.Import`、`Project.Undo`。Facade 名称和方法名称都必须是单段稳定标识符，完整名称只允许一个点。内部可以把两段名称分别编码为 32 位稳定码，并组合成 Mail 使用的 64 位类型码；编码是传输细节，名称才是公开协议身份。

Facade 的两段结构不是两级硬件寻址。实例标识、轴标识和运动量属于方法参数，不得进入 Facade 名称：

```text
硬件寻址思维：MachineID#AxisNo#Jog
软件接口思维：Machine.Jog({ machineId, axis, delta })
```

`Machine.Jog` 在产品生命周期内保持稳定。产品拥有多少台机床、某台机床有哪些轴，都不会增加新的 Facade 或方法。`machineId` 可以定位一个 Entity，也可以定位由多个 Entity、Component、Resource 和领域 Service 共同表达的逻辑对象，但它只是一次调用的数据，不是远程对象地址。

```text
FacadeName.MethodName + Parameters
    ↓ 参数校验、业务编排和事务
产品 Facade
    ↓ 业务校验、编排、事务和状态投影
Entity / Component / Resource / Service
```

例如，产品可以向外提供如下 `Machine` Facade：

```text
Machine
├── Properties
│   ├── instances
│   └── supportedDefinitionFormats
├── Methods
│   ├── Get(machineId)
│   ├── SetName(machineId, name)
│   ├── SetElementAppearance(machineId, elementId, appearance)
│   ├── Jog(machineId, axis, delta)
│   └── SetEnabled(machineId, enabled)
└── Events
    ├── Changed(machineId)
    └── JointPositionChanged(machineId, axis, position)
```

调用 `Machine.Jog({ machineId, axis, delta })` 时，Facade 可以查找实例、校验轴和限位、修改多个 Component、执行约束检查并发布状态变化。调用方只依赖 `Machine.Jog` 的业务语义，不依赖这些内部实现。

## 10.2 Facade 的职责边界

Facade 负责：

- 定义产品对外可见的业务能力；
- 定义属性、方法、事件及其业务语义；
- 将外部调用转换为领域操作；
- 执行权限、参数、状态和业务规则校验；
- 编排跨 Entity、Component、Resource 和 Service 的操作；
- 确定事务边界并返回稳定的结果；
- 将内部状态投影为对外数据，而不泄漏 ECS 结构。

Facade 不负责取代 Component、Behaviour 或 Service。持久化数据仍由 Component 表达，自动响应和调度仍由 Behaviour 承担，可复用能力仍由 Service 提供。Facade 位于产品边界，是这些内部能力的对外编排层。

不得默认提供 `SetProperty(componentName, fieldName, value)` 一类任意字段修改能力。写操作应表达为具有明确业务含义的方法，例如 `rename`、`setColor`、`split` 或 `addMicroJoint`，使产品能够长期维护业务约束和数据不变量。

## 10.3 Facade 的产品所有权

共享插件提供可复用的数据模型、资源、算法、Behaviour 和 Service；具体产品拥有 Facade，并决定哪些业务概念、属性和操作可以对外暴露。

```text
共享插件能力
    ↓
Laser3DCAM Facade     TubeCAM Facade     其他产品 Facade
    ↓                      ↓                    ↓
各产品自己的对外交互协议和策略
```

同一套底层 Machine 能力在不同产品中可以形成不同的 `Machine` Facade。产品可以选择不同的属性、方法、校验规则和工作流程，而不要求共享插件暴露一套覆盖所有产品的公共大接口。

## 10.4 Facade 文档规范

Facade 的对外文档采用面向对象方式组织。每个 Facade 至少说明：

- Facade 的业务语义和稳定名称；
- Properties：可读取状态及其一致性语义；
- Methods：参数、前置条件、事务边界、结果和错误；
- Events：触发条件、载荷和顺序保证；
- 参数中对象标识的语义、来源和失效条件；
- 调用范围、线程、并发和权限规则。

文档描述的是稳定的产品协议，不公开实现该协议所使用的 Entity、Component 类型或后端类。内部数据结构可以持续重构，只要 Facade 的对外语义保持成立。

## 10.5 Facade 与 Mail、PDO 的关系

Facade 定义业务交互语义，Mail、PDO 和 Resource 承载这些语义，两者不属于同一抽象层级。

- 属性读取和状态订阅通常投影到 PDO 或只读查询；
- 方法调用可由 Mail 承载，并在对应运行范围的线程中执行；
- 大型几何、网格等数据可以通过 Resource 传输；
- 一个 Facade 方法可以在内部组合多种通信和存储机制。

Mail 的 `typeCode` 由 Facade 名称码和方法名称码组成，Mail payload 承载参数，回复 Mail 承载 `CFacadeResult`。Mail 只负责运输，不解释业务对象，也不把调用重新解释为 Command 投递。

因此，对外 API 不应暴露“发送某个 Mail”或“修改某个 PDO 字段”作为业务概念。调用方只面对 Facade 的属性、方法和事件，具体通信方式由实现选择。

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

