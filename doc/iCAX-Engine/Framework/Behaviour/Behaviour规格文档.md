# Behaviour 规格文档

## 1. 定位

`Behaviour` 是 Component 对应的行为逻辑层。Component 负责表达实体的身份、状态或能力；Behaviour 负责响应这些数据的生命周期事件和帧更新。

Behaviour 不拥有 `Repository`，也不代表 `Project` 或 `Scene`。运行现场由 Scene 在调用时显式传入：

```cpp
OnUpdate(component, applicationContext, productContext, projectContext, sceneContext, deltaTime, totalTime);
```

## 2. 运行层级

- `ApplicationContext`：应用级上下文，如应用配置、路径和应用级依赖。
- `ProductContext`：产品级上下文，如产品定义、产品数据、产品级注册表和产品服务。
- `ProjectContext`：项目级上下文，只包含项目身份、路径和跟图纸走的 ProjectSetting。
- `SceneContext`：场景级上下文，包含 Repository、资源库、PDO、mail 和场景可用服务。
- `Universe`：Scene 内的 Behaviour 运行容器。每个 Scene 拥有自己的 Universe。
- `BehaviourRegistry`：保存 Behaviour 类型和创建工厂，通常按 Product 隔离。

## 3. 注册与绑定

产品模块可以通过自动注册宏把注册动作追加到进程级 `BehaviourRegistrationCatalog`：

```cpp
class CToolPathBehaviour final : public iCAX::Behaviour::CBehaviourBase
{
public:
    AUTO_REGIST_BEHAVIOUR(CToolPathBehaviour)

    std::string GetComponentClass() const override
    {
        return CToolPathComponent::S_ClassName;
    }
};
```

Catalog 只保存注册回放函数，不保存运行实例。产品启动时按模块路径把注册动作回放到自己的 `IBehaviourRegistry`，Scene 创建 Universe 时注入该注册表。Universe 绑定 Behaviour 后，会为当前 Scene 创建独立 Behaviour 实例。

一个 `ComponentClass` 只能注册一个 Behaviour。若一个 Behaviour 越来越庞大，应优先检查 Component 抽象是否过粗。

## 4. 生命周期

Behaviour 提供以下生命周期：

- `Awake`：组件进入 Universe 事件流时触发。
- `Start`：组件第一次进入帧循环时触发。
- `Enable`：组件启用后触发。
- `Disable`：组件禁用后触发。
- `PreUpdate` / `Update` / `PostUpdate`：帧更新阶段触发。
- `Modifying`：组件字段修改前触发。
- `Modified`：组件字段修改后触发。
- `DestroyImmediate`：组件移除事件链路中同步触发。
- `Destroy`：下一次 Tick 开头的统一销毁阶段触发。
- `Detach`：Behaviour 从 Universe 解绑或 Universe 关闭时触发。

组件默认启用。`Disable()` 只跳过该组件的帧更新阶段，不屏蔽 Repository 生命周期事件，也不影响已经发生的一次性 `Start`。

## 5. 调度顺序

Behaviour 可以通过 `GetSchedule()` 声明调度：

```cpp
iCAX::Behaviour::CBehaviourSchedule GetSchedule() const override
{
    iCAX::Behaviour::CBehaviourSchedule schedule;
    schedule.ExecutionOrder = 100;
    schedule.RunAfter.push_back("CGeometryBehaviour");
    return schedule;
}
```

规则：

- `ExecutionOrder` 越小越早执行。
- `RunAfter` 表示当前 Behaviour 在目标 Behaviour 之后执行。
- `RunBefore` 表示当前 Behaviour 在目标 Behaviour 之前执行。
- 缺失依赖目标会被忽略。
- 循环依赖会在绑定时抛出异常。
- 顺序相同且没有依赖关系时，按绑定顺序执行。

该执行计划作用于帧更新和批量 Repository 生命周期事件。

## 6. 销毁语义

组件移除时分两段：

- `DestroyImmediate(Component&)`：组件仍在删除事件链路中，适合维护索引、同步级联删除、解绑关系。
- `Destroy(CComponentDestroyInfo)`：组件已经离开 Entity/Repository，只提供 `EntityID`、`ComponentClass` 和删除前字段快照。

`Destroy` 阶段会持续处理到销毁队列为空。如果 `Destroy(A)` 删除 `B`，`B.DestroyImmediate` 会同步触发，`B.Destroy` 会在同一个销毁阶段继续处理。

## 7. 线程模型

Behaviour 遵循 Scene 单线程模型。Scene 工作线程驱动 Repository、Universe 和 Behaviour。外部线程不应直接调用 Behaviour，也不应直接修改 Behaviour 内部状态。

跨线程输入应通过 Mailbox、PDO 或命令通道进入对应 Scene 线程，再由 Scene 线程修改 Repository 或驱动 Universe。

## 8. 非目标

- 不负责 Repository 生命周期。
- 不负责 Scene 线程和帧节奏。
- 不负责资源池、服务注册和命令注册。
- 不支持 Behaviour 插件热卸载或热替换。
- 不提供内部并发保护。
