# Behaviour 方案文档

## 1. 设计目标

Behaviour 的目标是把 Component 的行为逻辑产品化组织起来，同时保持数据、产品和项目隔离。

核心原则：

- Behaviour 是 Component 的 system 部分。
- Universe 是行为运行容器，不拥有数据。
- Project 拥有 Repository、Universe 和运行时调度器。
- Product 拥有 BehaviourRegistry，并按产品模块回放自动注册。
- 每个 Project 创建自己的 Behaviour 实例，运行态不跨项目共享。
- Behaviour 回调不吞异常，错误应暴露在第一现场，由 Project/ApplicationHost 的运行时边界处理。

## 2. 代码结构

```text
Behaviour/
  BehaviourBase.*
    -> Behaviour 生命周期基类和调度描述。

  IUniverse.* / Universe.*
    -> Project 内的行为运行容器，对外提供 Tick、Repository 事件入口和绑定控制。

  BehaviourDispatcher.*
    -> 内部调度器，负责绑定 Behaviour、构建执行计划、翻译 Repository 事件和维护销毁队列。

  IBehaviourRegistry.* / BehaviourRegistry.*
    -> Behaviour 类型描述、工厂注册和实例创建。

  BehaviourRegistrationCatalog.*
    -> 进程级自动注册目录，按模块路径回放到产品级 BehaviourRegistry。
```

## 3. 注册隔离

自动注册宏并不直接写入某个产品的注册表，而是写入进程级 Catalog。产品运行时加载模块后，拿到模块路径，再调用：

```cpp
CBehaviourRegistrationCatalog::ReplayByModulePaths(productRegistry, loadedModulePaths);
```

这样既保留 DLL 静态注册的便利，又避免不同产品的 Behaviour 混到同一个运行注册表里。

## 4. Universe 初始化

`GenerateUniverse(registry)` 返回一个已经可用的 Universe。Universe 构造时创建内部 Dispatcher，外部没有二段式 `Initialize()`。

Project 创建时注入产品级 `IBehaviourRegistry`：

```cpp
auto universe = iCAX::Behaviour::GenerateUniverse(productBehaviourRegistry);
universe->BindBehaviour<CToolPathBehaviour>();
```

Universe 关闭时调用 `Cleanup(true)`，内部会触发 Behaviour `Detach()` 并释放 Dispatcher。

## 5. 事件流

Repository 事件由 Project 转发到 Universe：

```text
Component/Entity
  -> Repository event
  -> Project repository forwarder
  -> Universe
  -> BehaviourDispatcher
  -> Behaviour lifecycle callback
```

普通事件立即翻译：

- `AddComponent` -> `Awake`
- `EnableComponent` -> `Enable`
- `DisableComponent` -> `Disable`
- `ModifyComponent changing` -> `Modifying`
- `ModifyComponent changed` -> `Modified`
- `RemoveComponent changing` -> `DestroyImmediate` + delayed `Destroy`

批量事件通过 `RepositoryEventBatch` 处理，Dispatcher 同时读取原始事件记录和 ChangeSet：

- 原始事件记录用于保留真实事件顺序和删除当下快照。
- ChangeSet 用于判断净新增、净删除、净修改。
- Enable/Disable 在批量内按净启用状态合并，只派发最终变化。

## 6. 帧流

Project 的运行时调度器产生 `deltaTime/totalTime`，每帧调用：

```cpp
universe.Tick(applicationContext, productContext, projectContext, deltaTime, totalTime);
```

Dispatcher 执行顺序：

1. Flush delayed destroy queue。
2. 对新进入视图缓存的组件触发 `Start`。
3. 刷新 pre-cache。
4. 对启用组件执行 `PreUpdate`。
5. 对启用组件执行 `Update`。
6. 对启用组件执行 `PostUpdate`。

`PauseFrameUpdate` 只跳过第 4-6 步，不影响 `Start` 和 Repository 生命周期事件。

## 7. 销毁队列

`DestroyImmediate` 在 Repository remove 链路中同步执行。`Destroy` 使用 `CComponentDestroyInfo` 快照进入 Dispatcher 销毁队列，在下一次 Tick 开头统一处理。

销毁阶段允许级联删除。Dispatcher 在同一个销毁阶段持续处理新入队的销毁通知，并设置最大处理数量保护，避免错误代码导致无限级联。

## 8. 调度计划

Dispatcher 在每次绑定或解绑 Behaviour 后重建执行计划。

执行计划构建步骤：

1. 收集绑定顺序和各 Behaviour 的 `CBehaviourSchedule`。
2. 根据 `RunAfter/RunBefore` 建有向边。
3. 以 `ExecutionOrder` 和绑定顺序作为无依赖节点的选择优先级。
4. 拓扑排序生成 `ExecutionList`。
5. 若无法完成排序，抛出循环依赖异常。

该执行计划用于 Tick、批量事件和销毁队列排序。

## 9. 产品化边界

Behaviour 已具备产品化主干能力，但依赖以下外部约束：

- Project 必须在同一个工作线程内驱动 Repository 和 Universe。
- ProductRuntime 必须向 Project 注入正确的产品级 BehaviourRegistry。
- 模块 DLL 不应热卸载；Catalog 中的注册回放函数按进程常驻处理。
- Behaviour 内不要保存跨 Project 的共享运行态。

