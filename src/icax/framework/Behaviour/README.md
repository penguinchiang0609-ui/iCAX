# Behaviour

`Behaviour` 是 framework 中的行为工程，负责行为运行容器、行为注册、行为调度和组件生命周期回调。

`Universe` 只表达行为运行容器，不拥有 `Repository`，也不代表 `Project`。需要访问 EC 数据时，由外部运行单元传入 `IUniverseContext`。

## 目录结构

- `IUniverse.*` / `Universe.*`：行为运行容器，持有 Behaviour 调度器，不持有数据仓储。
- `IUniverseContext.*` / `UniverseContext.*`：一次运行上下文，由外部运行单元持有，提供 Database、计时器和应用设置。
- `BehaviourBase.*` / `BehaviourDispacther.*`：Behaviour 生命周期回调和调度。
- `BehaviourRegistry.*`：Behaviour 类型注册。
