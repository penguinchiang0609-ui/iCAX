# Entity 数据操作与语言规格文档

## 1. 目标

Entity 数据操作统一使用两种结构化表达：

- `SEntityWhere`：描述“哪些 Entity 是目标”，等价于 SQL 的 `WHERE` 部分。
- `SEntityUpdate`：描述“对每个目标 Entity 做什么组件修改”。

同一份 `Where` 可以复用于一次性查询、物化视图、更新和删除。所有前端最终都必须编译成 Database 的结构化表达，再由 Repository 执行。

```text
C++ Builder / C++ Lambda
              ───────────────┐
Lambda 字符串 / EntitySQL ── DatabaseLanguage
                              │
                              v
                 SEntityWhere / SEntityUpdate
                              │
                              v
                    Database Repository
```

这套能力以 Entity 为操作对象，不引入通用关系表、任意 Join、任意行 Projection 或数据库脚本执行器。

## 2. 工程边界

### 2.1 Database

`framework/Database` 负责：

- `SEntityWhere`、`SEntityWhereNode`、`SEntityValueOperand`。
- `SEntityUpdate`、`SComponentUpdate`。
- C++ Builder 与 C++ Lambda 表达式代理。
- Where 规范化、参数绑定、Schema 校验和求值。
- `Query`、`CreateEntityView`、`ReleaseEntityView`、`Update`、`Delete`。
- 事务、回滚、Undo/Redo、事件和增量 EntityView。

Database 不解析文本语言。

### 2.2 DatabaseLanguage

`framework/DatabaseLanguage` 单向依赖 Database，负责：

- 受控 Lambda 字符串的词法分析和语法分析。
- EntitySQL 的词法分析、语法分析和执行入口。
- 把文本编译成 Database 的 `SEntityWhere` / `SEntityUpdate`。

Database 不得反向依赖 DatabaseLanguage。只使用 C++ Builder 的底层模块无需链接语言解析器。

## 3. Where

`SEntityWhere` 是纯数据 AST。当前节点支持：

| 节点 | 含义 |
|---|---|
| `Constant` | 固定真或假 |
| `HasComponent` | Entity 拥有指定组件 |
| `ComponentEnabled` | 指定组件存在且启用 |
| `PropertyComparison` | 组件字段与字面量或参数比较 |
| `All` | 所有子条件成立 |
| `Any` | 至少一个子条件成立 |
| `Not` | 子条件取反 |
| `Reference` | 沿 UUID 引用到目标 Entity，再执行子 Where |

比较操作支持：

- `Equal`、`NotEqual`
- `Less`、`LessOrEqual`、`Greater`、`GreaterOrEqual`
- `Contains`、`StartsWith`、`EndsWith`
- `In`

操作数支持：

- `Literal`：字面量。
- `Parameter`：运行时参数名，从调用方传入的 `ObjectMap` 绑定。

Where 在执行前会被规范化。等价的 `All` / `Any` 子节点会排序、去重和折叠，保证物化视图可以共享稳定的实例键。

## 4. 读取 API

### 4.1 一次性 Query

```cpp
std::vector<uuid> Query(
    const SEntityWhere& where,
    const ObjectMap& parameters = {});
```

`Query`：

- 在调用时对 Repository 当前内容求值。
- 返回有稳定顺序的 Entity ID 快照。
- 不创建缓存。
- 不注册事件监听。
- 返回后不会自动变化。

适合一次性命令、校验和低频读取。

### 4.2 物化 EntityView

```cpp
std::shared_ptr<IEntityView> CreateEntityView(
    const SEntityWhere& where,
    const ObjectMap& parameters = {});

void ReleaseEntityView(
    const std::shared_ptr<IEntityView>& view);
```

EntityView：

- 首次创建时全量物化一次。
- 后续根据 Repository 事件增量维护成员集合。
- 提供 `GetRevision()`、`GetEntityIDs()` 和 `Contains()`。
- 等价的规范化 Where 和有效参数在同一 Repository 内共享实例。
- 每次成功 `CreateEntityView` 必须与一次 `ReleaseEntityView` 配对。
- Repository 使用独立创建计数，不使用 `shared_ptr::use_count()` 判断生命周期。
- 计数归零时注销监听并移除缓存。

EntityView 的 Where 和参数不可原地修改。参数变化表示另一个视图：先创建新视图，切换调用方引用，再释放旧视图。

## 5. Update

### 5.1 结构

`SEntityUpdate` 包含一组针对不同组件类型的 `SComponentUpdate`：

```cpp
enum class EComponentUpdateType
{
    Modify,
    Add,
    Remove,
};
```

单个 Update 中，同一组件类型只能出现一个操作。

### 5.2 Modify

Modify 可以：

- 修改组件的一个或多个顶层 Value 字段。
- 修改组件启用状态。
- 同时完成上述两种修改。

Modify 要求每个目标 Entity 已存在该组件。Derived 字段不可写。

### 5.3 Add

Add 可以：

- 给每个目标 Entity 添加一个组件。
- 提供组件顶层 Value 字段的初始化值。
- 指定初始启用状态，默认启用。

Add 要求每个目标 Entity 尚不存在该组件。未提供的字段使用组件创建器的默认值。

### 5.4 Remove

Remove 删除目标 Entity 上的完整组件。Remove 不能携带字段或启用状态。

Remove 要求每个目标 Entity 已存在该组件。

### 5.5 执行

```cpp
SEntityMutationResult Update(
    const SEntityWhere& where,
    const SEntityUpdate& update,
    const ObjectMap& parameters = {});
```

返回值：

- `MatchedCount`：命令开始时 Where 匹配的 Entity 数量。
- `ChangedCount`：实际发生净修改的 Entity 数量。

执行语义：

1. 规范化和校验 Where。
2. 绑定 Where 与 Update 引用的参数。
3. 对 Where 执行一次，取得命令开始时的 Entity ID 快照。
4. 对全部目标执行严格预检。
5. 预检成功后，在一个 Repository Transaction 中执行所有组件操作。
6. 任一目标或操作失败，整个命令回滚，不保留部分修改。
7. 命令作为一个 Undo/Redo 步骤；若外层已有 Undo Command，则加入外层步骤。
8. EntityView 只观察提交后的批次最终状态。

“快照”意味着 Update 导致某个 Entity 不再匹配 Where，也不会中途改变本次命令的目标集合。

## 6. Delete

```cpp
SEntityMutationResult Delete(
    const SEntityWhere& where,
    const ObjectMap& parameters = {});
```

Delete 删除命令开始时 Where 匹配的完整 Entity，而不是删除字段或组件。

- 目标集合使用调用开始时的 Entity ID 快照。
- 在一个事务中原子执行。
- 可 Undo/Redo。
- Repository 的 Meta Entity 不可删除；它可以计入 `MatchedCount`，但不会计入 `ChangedCount`。

## 7. C++ Builder 与 C++ Lambda

### 7.1 Where

```cpp
using Where = iCAX::Database::CEntityWhereBuilder;

auto where = Where::From([](Where::Entity& entity)
{
    return entity.Has<CSumComponent>()
        && entity.Field<CSumComponent>(
               CSumComponent::PropertyName_A)
               >= entity.Parameter("minimum");
});
```

Lambda 接收的是假 Entity 表达式代理。Lambda 只在构建时执行一次，成员调用和运算符只生成 AST；不会保存 Lambda，也不会逐 Entity 回调 C++ Lambda。

### 7.2 Update

```cpp
using Update = iCAX::Database::CEntityUpdateBuilder;

auto update = Update::From([](Update::Entity& entity)
{
    entity.Modify<CSumComponent>()
        .Set(
            CSumComponent::PropertyName_A,
            entity.Parameter("target"))
        .Enabled(false);

    entity.Add<CPolicyComponent>()
        .Set(CPolicyComponent::PropertyName_PersistentValue, 7)
        .Enabled(true);

    entity.Remove<CChainComponent>();
});
```

Update Lambda 返回 `void`。它同样只执行一次并记录结构，不访问真实 Repository。

## 8. Lambda 字符串

Lambda 字符串是受控 DSL，不是 C++ 编译器，也不执行任意脚本。

### 8.1 Where 字符串

```cpp
auto where = CEntityLambda::ParseWhere(R"(
    (entity) =>
        entity.Has("CSumComponent")
        && entity.Field("CSumComponent", "A")
            >= entity.Parameter("minimum")
)");
```

支持：

- `Has`、`Enabled`、`Constant`
- `Field(component, path)` 比较
- `Contains`、`StartsWith`、`EndsWith`、`In`
- `&&`、`||`、`!`
- `Parameter`
- `Ref` / `Reference` 的 `Any` / `All`

组件和字段在字符串中按名称表达，执行前仍由 Database Meta Schema 校验。

### 8.2 Update 字符串

```cpp
auto update = CEntityLambda::ParseUpdate(R"(
    (entity) => {
        entity.Modify("CSumComponent")
            .Set("A", entity.Parameter("target"))
            .Enabled(false);
        entity.Add("CPolicyComponent")
            .Set("PersistentValue", 7)
            .Enabled(true);
        entity.Remove("CChainComponent");
    }
)");
```

## 9. EntitySQL

EntitySQL 是面向 Entity/Component 的类 SQL 语言，不是关系型 SQL。

### 9.1 WHERE

```sql
WHERE
    HAS CSumComponent
    AND ENABLED CRenderInstanceComponent
    AND CSumComponent.A >= :minimum
```

参数使用 `:name` 或 `PARAMETER(name)`。

引用条件示例：

```sql
WHERE REF CChainComponent.ParentID ANY (
    CSumComponent.A >= :minimum
)
```

### 9.2 SELECT / QUERY

```sql
SELECT ENTITY
WHERE HAS CSumComponent
  AND CSumComponent.A >= :minimum;
```

`QUERY ENTITY` 是 `SELECT ENTITY` 的同义形式。没有 `WHERE` 时匹配全部 Entity。

### 9.3 UPDATE

```sql
UPDATE ENTITY
    MODIFY CSumComponent
        SET A = :target, ENABLED = FALSE
    ADD CPolicyComponent
        WITH PersistentValue = 7, ENABLED = TRUE
    REMOVE CChainComponent
WHERE HAS CSumComponent
  AND CSumComponent.A >= :minimum;
```

只使用组件默认值时可以省略 `WITH`：

```sql
UPDATE ENTITY
    ADD CPolicyComponent
WHERE HAS CSumComponent;
```

UPDATE 必须显式提供 WHERE，避免遗漏条件造成全库修改。

### 9.4 DELETE

```sql
DELETE ENTITY
WHERE HAS CObsoleteComponent;
```

DELETE 必须显式提供 WHERE。

### 9.5 执行入口

```cpp
auto statement = CEntitySql::Parse(text);
auto result = CEntitySql::Execute(repository, text, parameters);
```

`Parse` 只编译文本，不访问 Repository。`Execute` 先解析，再调用 Repository 的 `Query`、`Update` 或 `Delete`。

## 10. 等价性

以下入口生成同一种 Database IR：

- 手工 Builder。
- C++ Lambda 表达式代理。
- Lambda 字符串。
- EntitySQL。

因此它们共享：

- Where 规范化。
- 参数绑定规则。
- Meta Schema 校验。
- 求值语义。
- Update/Delete 原子事务语义。
- Undo/Redo 和 Repository 事件语义。

文本语言不得建立第二套独立执行器。

## 11. 明确不支持

当前版本不支持：

- 执行任意 C++、JavaScript 或脚本代码。
- 关系型表、列投影、Join、Group By、聚合和排序。
- 原地修改已经创建的 EntityView 条件或参数。
- 通过 Update 修改 Entity ID。
- 写 Derived 字段。
- `INSERT ENTITY`。Entity 创建涉及 ID 生成、必需组件和产品初始化策略，后续应单独设计。
- 部分成功模式。Update/Delete 当前固定为严格原子模式。

## 12. 验收要求

至少验证：

- 一次性 Query 不注册监听。
- 等价 Where 与参数共享 EntityView。
- Create/Release 创建计数正确。
- 释放最后一个使用权后停止增量维护。
- Modify/Add/Remove 在同一事务内执行。
- 多 Entity 预检失败时零部分写入。
- Update/Delete 可 Undo/Redo。
- 命令使用初始匹配快照。
- C++ Lambda、Lambda 字符串和 EntitySQL 生成等价结构。
- EntitySQL 的 Query、Update、Delete 实际执行。
- UPDATE/DELETE 缺少 WHERE 时拒绝解析。
- 非法组件、字段、参数和写 Derived 字段时拒绝执行。
