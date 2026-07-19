# Behaviour

`Behaviour` 是 framework 中的行为工程，负责行为运行容器、行为注册、行为调度和组件生命周期回调。

`Universe` 只表达行为运行容器，不拥有 `Repository`，也不代表 `Project`。Behaviour 需要访问运行现场时，由 Scene 在调度时显式传入 `ApplicationContext`、`ProductContext`、`ProjectContext` 和 `SceneContext`。每个 `Universe` 持有自己的 Behaviour 实例，Behaviour 之间不跨 Scene 共享运行状态。

`Universe` 不拥有计时器，也不决定帧节奏。Scene 负责运行时调度，在每帧调用 `Universe::Tick` 时显式传入 `deltaTime/totalTime`。Universe 持有一个 Engine Task scheduler；它只接受线程安全入队，不创建线程，并在下一次 `Tick` 开始时由所属 Scene 工作线程消费。

Universe 同时持有一个通用 `CCoroutineRuntime`。Runtime 只认识协程 handle，不认识 Component。每个 Behaviour 保存自己启动的“Component → handles”关联；Dispatcher 在组件启用、禁用和销毁时显式通知 Behaviour，由 Behaviour 对相应 handle 调用 Resume、Pause 和 Cancel。Component/Database 不依赖 Coroutine、Behaviour 或 Runtime。

`IBehaviourRegistry` 保存 Behaviour 的类型描述和创建工厂，不保存可执行实例。Application/Product 可以创建自己的注册表并回放自动注册目录；Scene 创建 `Universe` 时注入对应注册表，由 Dispatcher 在绑定 Behaviour 时创建 Scene 私有实例。

Behaviour 不提供全局默认注册表。产品启动时应显式创建自己的 `IBehaviourRegistry`，再按产品模块路径从 `BehaviourRegistrationCatalog` 回放注册动作。一个 `ComponentClass` 只能注册一个 Behaviour，冲突会在注册阶段暴露。

`Universe` 只负责接收 `Repository` 事件并转交给 `CBehaviourDispatcher`。事件翻译、批量事件解析、调度排序和销毁队列都由 Dispatcher 内部完成，避免外部代码直接拼装生命周期通知。

Dispatcher 处理 `Repository` 事件时同时支持普通事件和批量事件。普通事件立即转发生命周期回调；批量事件通过 `RepositoryEventBatch` 获取原始事件顺序和净变更结果，避免批处理期间缓存不刷新、生命周期缺失或净空变更误触发。

## 调度顺序

Behaviour 可以通过 `GetSchedule()` 声明调度顺序：

- `ExecutionOrder`：数值越小越早执行。
- `RunAfter`：当前 Behaviour 在指定 BehaviourClass 之后执行。
- `RunBefore`：当前 Behaviour 在指定 BehaviourClass 之前执行。

Dispatcher 会基于这些信息构建拓扑执行计划。缺失的依赖目标会被忽略，循环依赖会在绑定 Behaviour 时直接抛出异常。相同优先级且无依赖关系时，按绑定顺序保持稳定。

该执行计划同时作用于：

- `Start / PreUpdate / Update / PostUpdate`
- 批量 Repository 事件中的 `Awake / Enable / Disable / Destroy / Modified`

普通单条 Repository 事件只有一个目标 Behaviour，因此不存在跨 Behaviour 排序问题。批量事件会先收集通知，再按 Behaviour 执行计划排序；同一个 Behaviour 内通常保持原始事件顺序。`Enable/Disable` 在批量事件中按批次前后的净启用状态合并：如果最终状态与批次开始一致，则不发通知；如果不同，只发一次最终的 `Enable` 或 `Disable`。

组件移除时会产生两段销毁生命周期：

- `OnDestroyImmediate`：在 Repository remove 事件链路中同步触发，适合做级联删除、索引解绑等必须立即保持数据一致的工作。
- `OnDestroy`：由 Dispatcher 保留 `CComponentDestroyInfo` 销毁快照，在下一次 `Tick` 开始时触发，适合做延迟清理、通知、运行态回收等不应打断当前数据修改链路的工作。

`OnDestroy` 不接收 `CComponentBase&`。执行 delayed destroy 时，组件已经从 Entity/Repository 移除，继续暴露组件引用会误导调用者把它当作活组件使用。`CComponentDestroyInfo` 只包含 `EntityID`、`ComponentClass` 和 `PreviousProperties`。

一次 `Tick` 开头的销毁阶段会持续处理到销毁队列为空。如果 `OnDestroy(A)` 中继续删除 `B`，`B.OnDestroyImmediate` 会在删除链路中同步触发，`B.OnDestroy` 会在同一个销毁阶段继续触发，不会拖到再下一帧。Dispatcher 对单次销毁阶段设置最大通知数保护，避免错误代码造成无限级联删除。

批量事件中，`OnDestroy` 的 `PreviousProperties` 来自原始 remove 事件记录，即删除当下的组件快照；不使用 `ChangeSet.RemovedComponents` 的撤销用快照。`ChangeSet` 仍用于判断批次净变更、版本和派生字段失效，但不作为 Behaviour 销毁生命周期的事实来源。

## 插件加载与卸载策略

Behaviour 的自动注册宏只把“注册回放函数”追加到进程级 `BehaviourRegistrationCatalog`。Catalog 是追加式目录，不提供注销接口，也不承担 DLL 卸载管理。

当前约定是：产品模块 DLL 一旦加载到进程，就按进程常驻处理；不支持 Behaviour 插件热卸载、热替换或从 Catalog 中移除注册记录。产品级隔离依靠 `ReplayByModulePaths` 把指定模块的注册动作回放到当前产品自己的 `IBehaviourRegistry`，而不是依靠卸载全局记录。

`Universe::UnbindBehaviour` 只解除当前 Universe 内某个 Behaviour 实例的绑定，适用于项目关闭、行为禁用或测试清理；它会触发该 Behaviour 的 `Detach` 钩子，但不会注销 Behaviour 类型，不会修改产品级注册表，也不会卸载插件 DLL。

## 帧更新暂停

`PauseFrameUpdate` / `ResumeFrameUpdate` 只控制 Behaviour 的帧更新阶段，用于仿真暂停等场景。暂停帧更新后，`PreUpdate / Update / PostUpdate` 会被跳过；`Start` 仍会执行，Repository 生命周期事件也会正常分发，包括 `Awake / Enable / Disable / Destroy / Modifying / Modified`。

这意味着暂停帧更新不是“禁用 Behaviour”。如果需要让某个 Behaviour 不再接收任何事件，应使用 `UnbindBehaviour` 解除当前 Universe 内的实例绑定。

组件默认处于启用状态。调用 `ComponentBase::Disable()` 后，Dispatcher 会跳过该组件的 `PreUpdate / Update / PostUpdate`，直到组件再次 `Enable()`。组件禁用不影响 Repository 生命周期事件，也不影响已经进入视图缓存时触发的一次性 `Start`。

## Component Coroutine

Component Coroutine 固定由所属 Universe 的 Scene 工作线程恢复，帧内顺序为：

```text
Engine Task -> Start -> PreUpdate -> Update -> Component Coroutine -> PostUpdate
```

Coroutine 可等待 `NextFrame()`、`WaitForSeconds()`、`WaitUntil()` 或 `Await(Task<T>)`。即便被等待的 Task 在线程池完成，恢复动作也只会进入 Universe 的 Engine Task scheduler，随后在 Scene 工作线程推进，不会从线程池并发访问 Component/Repository。

`StartCoroutine(Component, CCoroutine<TResult>)` 返回 `CCoroutineHandle<TResult>`。协程可用 `co_return value` 产生结果，并通过 `handle.Completion()` 的 `Task<TResult>` 继续处理；无返回值流程使用 `CCoroutine<>`。

Component 禁用时，Behaviour 会暂停它记录的全部 handle，重新启用后恢复。Component 移除时，Dispatcher 会在 `OnDestroyImmediate` 之前要求 Behaviour 取消对应 handle，从而同步销毁所有 coroutine frame；迟到的 Task completion 只会成为无效唤醒，不能恢复已销毁的 frame。解绑 Behaviour 或关闭 Universe 也会取消对应协程。

## 线程模型与并发边界

Behaviour 不提供内部并发保护。标准运行模型是：每个 Scene 的工作线程驱动自己的 `Repository`、`Universe` 和 Behaviour 实例；`Tick`、Repository 事件转发、Behaviour 绑定/解绑、暂停/恢复帧更新都应在同一个 Scene 线程内发生。

前端线程、ApplicationRuntime 线程或其他 Scene 线程不应直接调用 Behaviour，也不应直接修改 Behaviour 保存的运行态。跨线程输入应先进入 Facades/PDO/命令通道；异步操作应在初始 Task 或 `TaskCompletionSource` 创建时绑定 `Universe::GetEngineTaskScheduler()`，后续 `ContinueWith()` 默认继承它，回到对应 Scene 线程后再修改 Repository、Component 或 Behaviour 运行态。

在上述约束下，Behaviour 可以保存 Scene 内轻量状态而不加锁。如果未来需要多线程计算，应由上层服务明确交付结果，再回到 Scene 线程应用数据修改；Behaviour 本身仍保持单线程回调语义。

## 目录结构

- `IUniverse.*` / `Universe.*`：行为运行容器，持有 Behaviour 调度器，不持有数据仓储。
- `BehaviourBase.*` / `BehaviourDispatcher.*`：Behaviour 生命周期回调和调度。
- `BehaviourRegistry.*`：Behaviour 类型描述、工厂注册和实例创建。
- `BehaviourRegistrationCatalog.*`：进程级自动注册目录，只追加注册回放函数，不支持注销或插件热卸载。
