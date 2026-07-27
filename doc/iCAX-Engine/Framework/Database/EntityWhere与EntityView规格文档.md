# EntityWhere 与 EntityView 规格文档

## 1. 定位

`EntityView` 是 `Repository` 上的只读 Entity 成员视图。

它接收一棵声明式查询表达式和一组不可变参数，返回当前满足条件的 Entity ID 集合。视图在首次创建时完成一次全量求值，此后由 Repository 事件增量维护，调用方不需要逐帧扫描全部 Entity。

`EntityView` 解决的问题是：

> 在同一份 Repository 数据中，持续维护“哪些 Entity 属于某个关注集合”。

典型用途包括：

- 机床视图关注同时具有机床语义和渲染实例的 Entity。
- 工件视图关注具有工件语义和渲染实例的 Entity。
- 按组件字段、组件状态或运行上下文筛选 Entity。
- 沿 Entity 引用字段筛选与其他 Entity 存在特定关系的 Entity。

`EntityView` 是派生数据，不是 Entity 的持久化归属信息。它不会向 Entity 添加 View 组件，也不会修改业务数据。

## 2. 职责边界

### 2.1 负责

- 描述 Entity 成员条件。
- 校验查询引用的组件类型和顶层字段。
- 绑定查询参数。
- 规范化查询表达式。
- 共享等价的物化查询实例。
- 首次计算成员集合。
- 根据 Repository 事件增量维护成员集合。
- 跟踪字段、组件状态、Entity 引用和派生字段依赖。
- 提供稳定、有序的 Entity ID 集合和成员 Revision。

### 2.2 不负责

- 产品级 View ID。
- 相机、视口、LayerMask 或渲染管线。
- PDO 的选择、生成和发送。
- Entity 在不同产品 View 下的颜色、材质等表现覆盖。
- UI 布局和大区切换。
- 聚合、分组、统计、排序规则和任意字段投影。
- 写入、更新或删除查询结果中的 Entity。
- 持久化 View 定义或 View 结果。

上层场景 View 可以使用 `EntityView` 的成员结果，但两者不是同一个概念。

## 3. 核心类型

### 3.1 SEntityWhere

`SEntityWhere` 是查询定义，根节点为 `SEntityWhereNode`：

```cpp
struct SEntityWhere
{
    SEntityWhereNode Root;
};
```

查询定义是纯数据结构，不接受任意 lambda。这样 Database 才能检查查询、生成稳定缓存键并知道数据变化会影响哪些结果。

### 3.2 SEntityWhereNode

查询节点可以表达：

| 节点 | 语义 |
|---|---|
| `Constant` | 固定为 `true` 或 `false` |
| `HasComponent` | Entity 精确拥有指定组件 |
| `ComponentEnabled` | Entity 拥有指定组件且组件处于启用状态 |
| `PropertyComparison` | 组件字段与字面量或参数进行比较 |
| `All` | 所有子节点成立 |
| `Any` | 至少一个子节点成立 |
| `Not` | 唯一子节点不成立 |
| `Reference` | 读取 Entity 引用字段，并在目标 Entity 上求值唯一子节点 |

### 3.3 IEntityView

物化视图公开以下只读接口：

```cpp
virtual const SEntityWhere& GetWhere() const = 0;
virtual const ObjectMap& GetParameters() const = 0;
virtual uint64_t GetRevision() const = 0;
virtual std::vector<uuid> GetEntityIDs() const = 0;
virtual bool Contains(const uuid& entityID) const = 0;
```

`GetWhere()` 和 `GetParameters()` 返回的是 Repository 规范化、绑定后的不可变定义。

`IEntityView` 同时实现 `IEntityViewEventPublisher`。上层可以通过
`AddObserver` / `RemoveObserver` 监听成员集合变化：

```cpp
struct EntityViewEventArgs
{
    uint64_t nPreviousRevision;
    uint64_t nRevision;
    std::vector<uuid> AddedEntityIDs;
    std::vector<uuid> RemovedEntityIDs;
};

class IEntityViewEventListener
{
public:
    virtual void OnEntityViewChanged(
        void* sender,
        const EntityViewEventArgs& args) = 0;
};
```

事件是 Database 内部的同步成员变化通知，不包含邮件、Scene、产品 View 或前端协议。
持有 EntityView 的上层负责把该事件转换成自己的失效、消息或前端通知机制。

## 4. 一次性查询

Repository 可以直接执行 Where 并返回当前 Entity ID 快照：

```cpp
virtual std::vector<uuid> Query(
    const SEntityWhere& where,
    const ObjectMap& parameters = {}) = 0;
```

`Query` 使用与 EntityView 完全相同的规范化、参数绑定、Schema 校验和求值器，但它不创建缓存、不注册事件监听，返回后结果不再自动变化。

低频读取、命令目标选择和只需一个时间点结果的调用应使用 `Query`；需要持续观察成员变化时才创建 EntityView。

## 5. 创建、共享与释放

Repository 对外提供成对的生命周期接口：

```cpp
virtual std::shared_ptr<IEntityView> CreateEntityView(
    const SEntityWhere& query,
    const ObjectMap& parameters = {}) = 0;

virtual void ReleaseEntityView(
    const std::shared_ptr<IEntityView>& view) = 0;
```

Repository 使用以下二元组作为实例键：

```text
规范化后的查询 + 查询实际引用的参数
```

规则如下：

- 每次成功调用 `CreateEntityView`，调用方都取得一次明确的 View 使用权。
- 同一 Repository 内，相同实例键共享同一个 `IEntityView`，Repository 单独记录该实例的创建计数。
- 查询未引用的参数不会进入实例键。
- 查询引用的参数值不同，会得到不同的物化视图。
- 每次成功的 `CreateEntityView` 必须与一次 `ReleaseEntityView` 配对。
- `ReleaseEntityView` 只减少 Repository 维护的创建计数，不能使用 `shared_ptr::use_count()` 代替该计数。
- 创建计数归零时，Repository 注销该 View 的事件监听、从实例缓存移除它，并释放成员集合和依赖索引。
- 调用方完成 `ReleaseEntityView` 后不得继续使用自己持有的该 View。
- Repository 销毁时统一释放仍未释放的物化视图。

不同 Repository 即使查询和参数相同，也不会共享结果。

## 6. 类型安全查询构造

Database 提供两种 C++ 构造形式：

- 直接调用 `CEntityWhereBuilder` 构造节点。
- 使用 `Where::From(lambda)` 和表达式代理构造节点。

两者生成完全相同的 `SEntityWhere` AST，进入同一套规范化、缓存、求值和增量维护流程。

### 6.1 直接 Builder

业务代码应优先使用 `CEntityWhereBuilder`：

```cpp
using Where = iCAX::Database::CEntityWhereBuilder;

auto query = Where::Build(Where::All({
    Where::Has<CMachineElementComponent>(),
    Where::Has<CRenderInstanceComponent>(),
}));
```

模板 Builder 要求组件类型继承 `CComponentBase`，组件类名通过 `TComponent::S_ClassName` 获取，业务代码不需要手写组件类名。

字段使用组件生成的 `PropertyName_*` 常量：

```cpp
auto query = Where::Build(
    Where::Compare<CSumComponent>(
        CSumComponent::PropertyName_A,
        EEntityWhereComparison::Greater,
        PropertyValue(10)));
```

字段标识最终仍属于 Database Meta 的运行时 schema，因此 Repository 会再次校验组件和字段是否已注册。

### 6.2 Lambda 表达式

更复杂的查询可以使用 Lambda 表达式前端：

```cpp
using Where = iCAX::Database::CEntityWhereBuilder;

auto query = Where::From([](Where::Entity& entity)
{
    return entity.Has<CWorkpieceComponent>()
        && entity.Has<CRenderInstanceComponent>()
        && entity.Field<CSizeComponent>(
               CSizeComponent::PropertyName_Length)
               >= entity.Parameter("minimumLength");
});
```

Lambda 接收的 `Where::Entity` 不是真实 Entity，而是 AST 构造代理。`Where::From` 只执行 Lambda 一次：

```text
Where::Entity 代理
  -> Has/Field/Parameter 返回表达式代理
  -> &&、||、!、>= 等运算符拼装节点
  -> Lambda 返回 CEntityWhereExpression
  -> Where::From 返回 SEntityWhere
```

它不会保存 Lambda，也不会对 Repository 中每个 Entity 调用 Lambda。运行期物化视图只解释生成后的声明式 AST。

Entity 代理提供：

| API | 结果 |
|---|---|
| `Constant(bool)` | 常量表达式 |
| `Has<T>()` | 组件存在表达式 |
| `Enabled<T>()` | 组件启用表达式 |
| `Field<T>(path)` | 字段代理 |
| `Parameter(name)` | 参数操作数 |
| `Literal(value)` | 显式字面量操作数 |
| `Ref<T>(path)` | Entity 引用代理 |
| `Reference<T>(path)` | `Ref<T>` 的完整名称别名 |

字段代理支持 C++ 比较运算符：

```cpp
field == value
field != value
field < value
field <= value
field > value
field >= value
```

右值可以是普通 C++ 字面量、`PropertyValue`、`entity.Literal(value)` 或 `entity.Parameter(name)`。

字符串和集合操作使用命名方法：

```cpp
field.Contains(value)
field.StartsWith(value)
field.EndsWith(value)
field.In(values)
```

布尔表达式使用：

```cpp
left && right
left || right
!expression
```

运算符优先级遵守 C++ 规则。重载后的 `&&` 和 `||` 用于构造 AST，不具有运行期短路语义；左右两侧都会在构造期生成节点。

表达式代理禁止转换为 `bool`，因此不能把查询表达式误写成真实数据分支：

```cpp
Where::From([](Where::Entity& entity)
{
    if (entity.Has<CWorkpieceComponent>()) // 编译失败
    {
        // ...
    }
    return entity.Constant(true);
});
```

Lambda 可以根据捕获的普通构造期值选择或生成 AST。直接参与字段比较的捕获值会成为查询字面量：

```cpp
const double minimum = 100.0;

auto query = Where::From([minimum](Where::Entity& entity)
{
    return entity.Field<CSizeComponent>(
        CSizeComponent::PropertyName_Length) >= minimum;
});
```

如果同一个查询结构需要在运行期绑定不同值，应使用 `entity.Parameter(name)`，不要捕获字面量。

### 6.3 Lambda 引用表达式

引用代理的 `Any` 和 `All` 接收另一个只执行一次的表达式 Lambda：

```cpp
auto query = Where::From([](Where::Entity& entity)
{
    return entity.Ref<CChainComponent>(
        CChainComponent::PropertyName_ParentID)
        .Any([](Where::Entity& parent)
        {
            return parent.Field<CSumComponent>(
                       CSumComponent::PropertyName_A)
                   >= parent.Parameter("minimum");
        });
});
```

内部 Lambda 生成 `Reference` 节点的目标查询子树，不会在构造期访问目标 Entity。

## 7. 逻辑表达式

### 7.1 All

`All` 表示所有子条件都成立：

```cpp
Where::All({
    Where::Has<CWorkpieceComponent>(),
    Where::Enabled<CRenderInstanceComponent>(),
})
```

空 `All` 等价于 `true`。

### 7.2 Any

`Any` 表示至少一个子条件成立：

```cpp
Where::Any({
    Where::Has<CMeshComponent>(),
    Where::Has<CCurveComponent>(),
})
```

空 `Any` 等价于 `false`。

### 7.3 Not

`Not` 必须有且只有一个子节点：

```cpp
Where::Not(Where::Has<CHiddenComponent>())
```

## 8. 组件条件

### 8.1 HasComponent

`HasComponent` 使用组件类名进行精确匹配：

```cpp
Where::Has<CWorkpieceComponent>()
```

它不把继承查询隐式混入成员语义。需要父类或能力语义时，应显式定义相应业务组件或扩展查询节点。

组件禁用不会使 `HasComponent` 变为 `false`。

### 8.2 ComponentEnabled

`ComponentEnabled` 同时要求：

- Entity 拥有该组件。
- `Component::IsEnable()` 返回 `true`。

```cpp
Where::Enabled<CRenderInstanceComponent>()
```

组件的 `EnableComponent` 和 `DisableComponent` 事件会增量刷新依赖该条件的 Entity。

## 9. 字段条件

### 9.1 字面量

```cpp
Where::Compare<CSizeComponent>(
    CSizeComponent::PropertyName_Length,
    EEntityWhereComparison::GreaterOrEqual,
    PropertyValue(100.0))
```

### 9.2 参数

```cpp
auto query = Where::Build(
    Where::CompareParameter<CSizeComponent>(
        CSizeComponent::PropertyName_Length,
        EEntityWhereComparison::GreaterOrEqual,
        "minimumLength"));

auto view = repository->CreateEntityView(query, {
    { "minimumLength", PropertyValue(100.0) },
});
```

查询引用的参数缺失时，`CreateEntityView` 抛出 `std::invalid_argument`。

### 9.3 参数不可原地修改

规范化后的 Where 和绑定参数共同构成 EntityView 的身份。EntityView 创建后，`GetWhere()` 和 `GetParameters()` 返回的定义保持不变，不提供 `SetParameters` 或 `UpdateParameters`。

参数改变表示另一个查询实例。调用方必须先创建新 View，切换自己的引用，再释放旧 View：

```cpp
auto newView = repository->CreateEntityView(query, {
    { "minimumLength", PropertyValue(200.0) },
});

auto oldView = std::exchange(currentView, std::move(newView));
repository->ReleaseEntityView(oldView);
```

顺序必须是：

```text
创建并完成新参数 View 的首次物化
  -> 调用方切换到新 View
  -> 释放旧 View
```

这样规定的原因是：

- 相同实例可能由多个调用方共享，原地修改会隐式改变其他调用方看到的查询。
- 参数属于实例缓存键，原地修改会破坏缓存身份。
- 参数变化可能改变任意 Entity 的匹配结果，通常需要重新物化全部候选 Entity。
- 不可变定义可以避免读取方看到参数和成员集合不一致的中间状态。

调用方传入的参数在 `CreateEntityView` 时完成绑定和复制；调用方之后修改原始 `ObjectMap` 不影响已创建的 View。

必须区分两类变化：

- Repository 数据变化：EntityView 身份不变，由 View 根据 Repository 事件自动增量更新。
- Where 或参数变化：创建新 EntityView，切换后释放旧 EntityView。

未来可以提供替换 EntityView 的便利接口，但其语义仍必须是“创建新 View，再释放旧 View”，不得原地修改现有 View。

### 9.4 PropertyPath

`PropertyPath` 的第一个片段必须是已注册的组件顶层字段，其余片段使用 `Variant::GetByPath` 语法：

```text
Settings.Visible
Points[0]
Material.Color[2]
```

Repository 创建视图时校验顶层字段。嵌套对象或数组成员在运行时读取；路径不存在时，该字段条件为 `false`。

字段事件按顶层字段跟踪。修改与查询无关的同组件字段不会触发该查询的重新求值。

## 10. 比较操作

`EEntityWhereComparison` 支持：

| 比较 | 支持语义 |
|---|---|
| `Equal` | 值相等 |
| `NotEqual` | 值不相等 |
| `Less` | 小于 |
| `LessOrEqual` | 小于或等于 |
| `Greater` | 大于 |
| `GreaterOrEqual` | 大于或等于 |
| `Contains` | 字符串包含、数组包含值、对象包含键 |
| `StartsWith` | 字符串前缀 |
| `EndsWith` | 字符串后缀 |
| `In` | 左值存在于右侧数组，或左侧字符串是右侧对象的键 |

数值比较规则：

- 支持不同数值 Variant 类型之间比较。
- 有符号整数和无符号整数采用不丢精度的整数比较。
- 任一侧为浮点数时进入浮点比较。
- `NaN` 不参与相等或大小排序。
- 不兼容类型的大小比较结果为 `false`。

非数值的相等比较遵守 `Variant` 相等语义。

## 11. Entity 引用

`Reference` 从当前 Entity 的组件字段中读取目标 Entity ID，并在目标 Entity 上继续执行一个查询子树：

```cpp
auto query = Where::Build(Where::Reference<CChainComponent>(
    CChainComponent::PropertyName_ParentID,
    Where::Compare<CSumComponent>(
        CSumComponent::PropertyName_A,
        EEntityWhereComparison::Greater,
        PropertyValue(10))));
```

引用字段支持：

- `uuid`
- 可解析为 UUID 的字符串
- 包含上述值的 `VariantArray`
- 嵌套 `VariantArray`

nil UUID、非法 UUID 字符串和其他值类型会被忽略。重复目标 ID 会合并。

### 11.1 Any

`EEntityReferenceMatch::Any` 表示至少一个目标 Entity 满足目标条件。

### 11.2 All

`EEntityReferenceMatch::All` 表示所有目标 Entity 都满足目标条件。

无有效目标 ID 时，`Any` 和 `All` 都返回 `false`，避免空引用被解释为有效业务关系。

目标 Entity 不存在时，Database 仍会登记目标 ID 的存在性依赖。以后创建该 Entity 时，引用它的候选 Entity 会被重新求值。

引用可以嵌套。查询 AST 有限，因此嵌套求值深度由查询定义本身决定。

## 12. 查询规范化

Repository 在创建物化视图前规范化查询：

- 递归规范化所有子节点。
- 展平嵌套的同类 `All` 和 `Any`。
- 对逻辑子节点排序并去重。
- 消除逻辑运算中的无效常量。
- 折叠单子节点 `All` 和 `Any`。
- 折叠双重否定。
- 将直接的条件与其否定同时出现折叠为常量。

规范化用于稳定缓存键和共享常见的结构等价查询，不承诺完成通用布尔代数证明。例如，所有德摩根等价形式不一定共享同一个实例。

## 13. Schema 校验

规范化后，Repository 根据 `IMetaRegistry` 校验：

- `HasComponent` 和 `ComponentEnabled` 使用的组件类型必须已注册。
- `PropertyComparison` 和 `Reference` 使用的组件类型必须已注册。
- 字段路径的顶层字段必须已注册。
- 参数名不能为空。
- 查询引用的参数必须存在。
- `Not` 和 `Reference` 必须各有且只有一个子节点。

校验失败时抛出 `std::invalid_argument`，不会创建或缓存半有效视图。

## 14. 物化与增量维护

### 14.1 首次创建

首次创建视图时，Database 遍历 Repository 当前的 Entity ID，并对每个 Entity 求值一次。

这次全量求值同时建立运行时依赖图：

```text
候选 Entity
  -> 被读取的 Entity
    -> Entity 存在性
    -> 组件存在性
    -> 组件启用状态
    -> 组件顶层字段
```

### 14.2 后续更新

物化视图订阅 Repository Changed 事件。

事件到来时：

1. 根据事件中的 Entity、组件、字段和事件类型查找依赖它的候选 Entity。
2. 只重新求值这些候选 Entity。
3. 用新求值过程产生的依赖替换旧依赖。
4. 更新最终成员集合。
5. 只有成员集合实际变化时才递增 Revision。

逻辑节点求值不会为了布尔短路而漏掉依赖。即使 `All` 已经遇到 `false`，或 `Any` 已经遇到 `true`，仍会读取其余声明式子节点并收集依赖。

### 14.3 Repository 事件

增量维护处理：

- Entity 创建和删除。
- Component 添加和移除。
- Component 启用和禁用。
- Component 字段修改。
- `kBatchChanged` 批量事件。

与某个查询无关的字段变化不会重算该查询。

### 14.4 批量最终态

批量事件中的记录只用于收集受影响候选 Entity。所有候选 Entity 在批次提交后按 Repository 最终状态求值一次。

因此：

- 批次内同一字段多次修改不会逐次刷新 View。
- 中间状态不会暴露为 View 成员。
- 一个批次最多使同一 View 的 Revision 增加一次。
- 批次最终成员未变化时 Revision 不变。

### 14.5 LoadBaseline

`LoadBaseline` 不发布普通 Repository 事件。基线加载结束时，Repository 会让所有已经存在的 `EntityView` 与当前数据执行一次完整对齐。

## 15. 派生字段依赖

查询可以比较已注册的 Derived 字段。

读取 Derived 字段时，`CDerivedPropertyManager` 已经记录其真实源字段。`EntityView` 会读取该派生字段的传递依赖，并把源字段纳入自己的运行时依赖图。

这意味着以下情况都能正确增量刷新：

- Derived 字段依赖同一 Entity 的其他字段。
- Derived 字段依赖其他 Entity 的字段。
- Derived 字段继续依赖另一个 Derived 字段。
- Derived 字段运行时更换所引用的目标 Entity。

派生依赖查询只存在于 `CRepository` 和 `CEntityView` 内部，不属于公共 `IRepository` 契约。

## 16. Revision

Revision 规则：

- 视图完成首次初始化后 Revision 为 `1`。
- 成员 Entity ID 集合实际变化时递增。
- 仅依赖图变化、成员集合不变时不递增。
- 无关 Repository 事件不递增。
- 批量提交中多个成员变化合并为一次递增。

Revision 只表示成员集合版本，不表示 Entity 内容版本，也不表示上层表现版本。

如果上层结果还包含颜色、材质等 View 局部表现，上层必须维护自己的内容 Revision。

### 16.1 成员变化事件

- 首次初始化把 Revision 建立为 `1`，不发布成员变化事件。
- 只有最终成员集合实际变化时才发布 `OnEntityViewChanged`。
- 回调发生前，成员集合和 Revision 已经更新完成。
- `AddedEntityIDs` 和 `RemovedEntityIDs` 表示上一个 Revision 到当前 Revision 的净变化。
- 两组 Entity ID 都按 UUID 稳定排序。
- 一个 Repository 批次最多让同一个 EntityView 发布一次事件。
- 批次中相互抵消、最终成员不变时不发布事件。
- `LoadBaseline` 完成后的全量对齐如果改变已有 View 的成员，也发布一次净变化事件。
- Entity 内容变化但成员资格不变时不发布 EntityView 事件；该变化仍由 Repository 事件表达。
- 观察者由 EntityView 弱引用，观察者异常被隔离，不影响已经提交的数据或其他观察者。
- 事件回调中的嵌套数据修改按 Revision 排队派发，保证观察者看到的事件顺序不倒置。

## 17. 结果顺序

内部成员集合使用按 UUID 排序的集合，`GetEntityIDs()` 返回确定性顺序。

这个顺序只用于稳定结果，不是产品排序规则。需要按名称、加工顺序或空间位置排序时，由上层根据业务语义处理。

## 18. 错误与缺失值

- 未注册组件或字段：创建视图时报错。
- 缺失参数：创建视图时报错。
- Entity 缺少查询所需组件：对应条件为 `false`。
- 嵌套字段路径不存在：对应字段条件为 `false`。
- 引用字段没有有效 Entity ID：引用条件为 `false`。
- 引用目标不存在：引用条件为 `false`，但保留存在性依赖。
- 字段读取或派生字段求值失败：对应条件为 `false`。

## 19. 性能模型

设 Repository 中 Entity 数量为 `N`：

- 首次物化：`O(N × 单 Entity 查询代价)`。
- 普通事件：只重新求值受该事件影响的候选 Entity。
- 成员读取：复制当前已排序的 Entity ID 集合。
- `Contains`：集合查找。
- 内存：成员集合、每个候选 Entity 的依赖描述和反向依赖边。

视图不会逐帧扫描 Repository。相同查询和参数的共享可避免重复物化。

## 20. 线程模型

Repository 当前面向单后台线程写入，不提供完整并发读写事务。

`EntityView` 使用内部互斥量保护成员集合、Revision 和依赖索引的快照访问，但这不把整个 Repository 提升为多线程可并发写数据库。

调用方仍应遵守 Database 的单后台线程写入模型。

## 21. 与其他 View/Cache 的区别

### 21.1 ComponentFrameCache

`ComponentFrameCache` 是 Behaviour 帧调度缓存：

- 按组件类型提供当前帧和上一帧组件集合。
- 服务 Behaviour 的帧更新和组件变化判断。
- 不表达产品级 Entity 成员查询。

它的旧名称是 `EntitiesView`。该名称已经废弃，避免与本规格中的 View 混淆。

### 21.2 Framework View

Framework 层的产品 View：

- 具有产品定义的 View ID。
- 选择或复用一个 Database `EntityView`。
- 可以增加 View 局部表现。
- 可以结合相机、LayerMask、PDO 和前端布局。

Database 不认识 `Common`、`Machine`、`Workpiece` 等产品枚举。

### 21.3 SQL/DataView

`EntityView` 借鉴物化视图的思想，但不是通用 SQL 引擎。当前规格只维护 Entity 成员关系，不提供：

- 聚合函数。
- `GROUP BY`。
- 任意 Join 结果行。
- 任意字段 Projection。
- 可写 View。

如果将来需要统计报表或复合读模型，应在独立的 Query/ReadModel 能力中设计，不应把表现层职责塞入 Entity 成员视图。

## 22. 验收要求

Database 测试至少覆盖：

- 组件存在条件的增删维护。
- `All`、`Any`、`Not`。
- 查询规范化和共享。
- 字段参数和无关字段不刷新。
- 组件启用状态。
- Entity 引用目标变化。
- 引用替换后解除旧目标依赖。
- 缺失目标后补建。
- 批量最终态与 Revision。
- `LoadBaseline` 完整对齐。
- Derived 字段同 Entity 依赖。
- Derived 字段跨 Entity 依赖。
- 非法组件、非法字段和缺失参数。
- 数值跨类型比较和 `NaN` 边界。

当前实现对应测试位于：

```text
src/tests/icax-engine/framework/Database/DatabaseTest/DatabaseTests.cpp
```
