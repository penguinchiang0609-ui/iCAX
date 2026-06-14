# Database 规格文档

## 1. 定位

`Database` 是 iCAX Engine Framework 层的 EC 数据模型项目。

它提供一个进程内数据仓储，用于组织业务数据空间、实体、组件、组件字段、字段访问、变更事件、组件版本和派生字段。

核心模型是：

```text
Repository
  Domain
    Entity
      Component
        Property
```

`Database` 不规定 CAD、CAM、机器模型等业务对象如何拆分组件。组件设计由问题域抽象决定。

## 2. 核心概念

### 2.1 Repository

`Repository` 是一次工程运行中的数据仓储。

它负责：

- 管理多个 `Domain`。
- 提供仓储自身的 `MetaEntity`。
- 汇总 `Domain` 事件并向外发布 `Repository` 事件。
- 维护组件版本表。
- 维护派生字段缓存和依赖图。

创建入口：

```cpp
std::shared_ptr<IRepository> repo =
    iCAX::Database::GenerateRepository(iCAX::Data::GenerateNewUUID());
```

### 2.2 Domain

`Domain` 是一个业务 EC 数据空间。

它用于隔离不同业务集合，例如 CAD、CAM、机器模型、排样结果、加工仿真等。`Domain` 内部拥有自己的 `Entity` 集合。

`Domain` 负责：

- 创建、获取、删除 `Entity`。
- 按条件筛选实体。
- 提供 `EntitiesView`。
- 提供 `MetaEntity` 描述自身。
- 把 `Entity` 事件上抛为 `Domain` 事件。

当前接口中存在 `IsPersistent()`，用于表达该 `Domain` 是否需要参与保存入口筛选。它不改变 `Domain` 的核心语义：`Domain` 首先是业务数据空间隔离。

### 2.3 Entity

`Entity` 是身份句柄。

它只有 ID 和组件集合，不直接表达复杂业务数据。一个实体通过挂载不同 `Component` 描述自己的身份、能力和状态。

`Entity` 负责：

- 添加、移除、获取组件。
- 查询是否具备某类组件。
- 监听组件字段变化，并上抛为 `Entity` 事件。

### 2.4 Component

`Component` 是实体某个切面的描述。

组件由上层按问题域建模，例如：

```text
TransformComponent
HierarchyComponent
TagComponent
ProjectSettingComponent
```

组件字段通过 `MetaRegistry` 注册，随后可以用统一方式访问：

```cpp
component->SetProperty("Name", value);
auto value = component->GetProperty("Name");
```

### 2.5 Property

`Property` 是组件字段。

字段元信息由 `MetaRegistry` 维护。每个可通过 `Property` 系统访问的字段至少包含三类语义：

```text
EPropertyKind
  Value：值字段，可读取，可设置。
  Derived：派生字段，只读，按需计算。

EPropertyPersistence
  Persistent：参与持久化。
  NonPersistent：不参与持久化。

EPropertyChangePolicy
  Transactional：事件正常触发；订阅者通常将其视为事务型修改。
  Observable：事件正常触发；订阅者通常只观察，不记录撤销还原。
  Silent：事件正常触发；订阅者通常忽略版本、派生失效和撤销还原。
```

派生字段不参与直接写入。对派生字段调用 `SetProperty` 或 `SetProperties` 会抛出 `std::runtime_error`。

## 3. 公开能力

### 3.1 Repository 能力

主要接口：

```cpp
std::shared_ptr<IDomain> CreateDomain(const uuid& id, const bool& persistent);
bool HasDomain(const uuid& id) const;
std::shared_ptr<IDomain> GetDomain(const uuid& id);
void DeleteDomain(const uuid& id);
std::vector<uuid> GetDomainIDs() const;
std::shared_ptr<IEntity> GetMetaEntity();
void CleanUp(const bool& forced = false);
```

组件版本：

```cpp
size_t GetComponentVersion(const uuid& domainId,
                           const uuid& entityId,
                           const std::string& componentType) const;

bool IsComponentChanged(const uuid& domainId,
                        const uuid& entityId,
                        const std::string& componentType) const;

void ResetComponentChangedFlag();
```

批量、事务、撤销还原和快速保存日志：

```cpp
std::unique_ptr<IRepositoryChangeScope> BeginChangeScope(
    EChangeScopeKind kind,
    const std::string& name = {});

std::unique_ptr<IRepositoryChangeScope> BeginTransaction(
    const std::string& name = {});

bool CanUndo() const;
bool CanRedo() const;
bool Undo();
bool Redo();

void OpenOperationLog(const std::string& path, bool truncate = false);
void CloseOperationLog();
bool HasOperationLog() const;
void ReplayOperationLog(const std::string& path);
```

`BeginChangeScope(EChangeScopeKind::LoadBaseline)` 用于第一次打开项目文件：它静默导入基线数据，不发布仓储事件，不进入撤销还原，不写快速保存日志，并在提交后清理组件 dirty 标记。

`BeginChangeScope(EChangeScopeKind::UserCommand)` 用于把一组修改合并成一次用户命令：提交时发布一次 `kBatchChanged`，并将合并后的 `ChangeSet` 进入撤销还原和快速保存日志。该 scope 离开作用域默认提交，用于兼容批量修改场景。

`BeginTransaction()` 用于数据库式事务：事务内修改会立即作用到内存；`Commit()` 后成为一次用户提交；`Cancel()` 或未显式提交就析构时，会按反向日志静默回滚。

撤销还原基于提交后的 `ChangeSet`。只有 `EPropertyChangePolicy::Transactional` 字段进入撤销还原；`Observable` 和 `Silent` 字段仍正常发事件，但不会进入 undo/redo。

快速保存日志是追加式操作日志。每次用户提交、撤销、重做都会把可持久化部分写入日志文件；日志只记录持久化 Domain 内的结构性修改，以及 `Persistent + Transactional` 字段。软件崩溃后，上层应先加载原项目文件，再调用 `ReplayOperationLog()` 回放日志，恢复到最近一次已写入日志的提交状态。

### 3.2 Domain 能力

主要接口：

```cpp
std::shared_ptr<IEntity> CreateEntity(const uuid& id);
bool HasEntity(const uuid& id) const;
std::shared_ptr<IEntity> GetEntity(const uuid& id) const;
void DeleteEntity(const uuid& id);
std::vector<uuid> GetEntityIDs() const;
IEntitiesView& GetView() const;
std::shared_ptr<IRepository> GetRepository() const;
```

### 3.3 Entity 能力

主要接口：

```cpp
std::shared_ptr<CComponentBase> AddComponent(const std::string& className);
void RemoveComponent(const std::string& className);
std::shared_ptr<CComponentBase> GetComponent(const std::string& className) const;
std::vector<std::shared_ptr<CComponentBase>> GetComponents(const std::string& className) const;
bool HasComponent(const std::string& className) const;
```

模板便捷接口：

```cpp
auto transform = entity->AddComponent<TransformComponent>();
auto transform = entity->GetComponent<TransformComponent>();
bool ok = entity->HasComponent<TransformComponent>();
```

### 3.4 Component 能力

主要接口：

```cpp
void SetProperty(const std::string& propertyName, const PropertyValue& value);
void SetProperties(const PropertySet& properties);
PropertyValue GetProperty(const std::string& propertyName) const;
PropertySet GetProperties() const;
std::vector<std::string> GetPropertyNameArray() const;
size_t Version() const;
bool IsChanged() const;
```

组件启用、禁用、删除标记：

```cpp
void Enable();
void Disable();
bool IsEnable() const;
void Delete();
bool IsDeleted() const;
```

## 4. 字段声明

### 4.1 普通字段

组件通过宏注册普通字段：

```cpp
DECLARED_ICAX_FIELD(
    MyComponent,
    int,
    Count,
    0,
    [](const int& lhs, const int& rhs) { return lhs == rhs; },
    [](const int& value) { return iCAX::Data::PropertyValue(value); },
    [](const iCAX::Data::PropertyValue& value) { return value.To<int>(); })
```

宏会生成：

```cpp
const int& GetCount() const;
void SetCount(const int& value);
static constexpr const char* PropertyName_Count = "Count";
```

同时注册到全局 `MetaRegistry`。

默认语义：

```text
Kind        = Value
Persistence = Persistent
ChangePolicy = Transactional
```

### 4.2 非持久化可观察字段

运行期状态字段通过 `DECLARED_ICAX_OBSERVABLE_FIELD` 声明：

```cpp
DECLARED_ICAX_OBSERVABLE_FIELD(
    MyComponent,
    int,
    ViewWidth,
    0,
    [](const int& lhs, const int& rhs) { return lhs == rhs; },
    [](const int& value) { return iCAX::Data::PropertyValue(value); },
    [](const iCAX::Data::PropertyValue& value) { return value.To<int>(); })
```

默认语义：

```text
Kind        = Value
Persistence = NonPersistent
ChangePolicy = Observable
```

典型用途是渲染窗口尺寸、当前选中态、UI 展示用运行期状态等：它们需要通知和版本变化，但不应进入持久化和撤销还原。

### 4.3 非持久化静默字段

纯缓存字段通过 `DECLARED_ICAX_SILENT_FIELD` 声明：

```cpp
DECLARED_ICAX_SILENT_FIELD(
    MyComponent,
    int,
    CachedValue,
    0,
    [](const int& lhs, const int& rhs) { return lhs == rhs; },
    [](const int& value) { return iCAX::Data::PropertyValue(value); },
    [](const iCAX::Data::PropertyValue& value) { return value.To<int>(); })
```

默认语义：

```text
Kind        = Value
Persistence = NonPersistent
ChangePolicy = Silent
```

静默字段适合纯派生缓存、内部临时计算结果等。事件仍然正常触发；需要忽略它的订阅者应通过 `MetaRegistry` 查询该字段的 `ChangePolicy`。

### 4.4 派生字段

派生字段通过 `DECLARED_ICAX_DERIVED_FIELD` 声明：

```cpp
DECLARED_ICAX_DERIVED_FIELD(
    MyComponent,
    int,
    Sum,
    [](const int& value) { return iCAX::Data::PropertyValue(value); },
    [](const MyComponent& self, iCAX::Database::CDerivedPropertyContext& ctx) {
        return ctx.GetProperty(self, MyComponent::PropertyName_A).To<int>()
             + ctx.GetProperty(self, MyComponent::PropertyName_B).To<int>();
    })
```

宏会生成：

```cpp
int GetSum() const;
static constexpr const char* PropertyName_Sum = "Sum";
```

派生字段没有 setter。

默认语义：

```text
Kind        = Derived
Persistence = NonPersistent
ChangePolicy = Silent
```

### 4.5 运行时裸字段

运行时裸字段通过 `DECLARED_ICAX_RUNTIME_FIELD` 声明：

```cpp
DECLARED_ICAX_RUNTIME_FIELD(
    std::weak_ptr<MyComponent>,
    RuntimePeer,
    {})
```

它不是正式 `Property`：

- 不注册到 `MetaRegistry`。
- 不生成 `PropertyName_xxx`。
- 不能通过 `GetProperty` / `SetProperty` 访问。
- 修改时不触发事件。
- 不参与版本、持久化、撤销还原和派生字段失效。

典型用途是组件内部运行期引用、系统计算缓存、无法自然转换成 `PropertyValue` 的 C++ 对象等。

## 5. 派生字段行为

派生字段用于表达由其他字段计算得到的只读结果。

典型例子：

```text
WorldMatrix = Parent.WorldMatrix * LocalMatrix
BoundingBox = Geometry.Points 计算得到
Total       = Local + Parent.Total
```

派生字段规则：

- 初始状态为 Dirty。
- 第一次读取时调用 Evaluator 计算。
- 计算结果缓存为 Clean。
- 后续读取若仍为 Clean，直接返回缓存。
- 依赖字段变化后，派生字段被标记为 Dirty。
- Dirty 后再次读取会重新计算。
- Evaluator 计算期间如果再次读取自己，会识别为循环依赖并抛出异常。

Evaluator 必须通过 `CDerivedPropertyContext` 读取依赖字段：

```cpp
auto value = ctx.GetProperty(entityId, componentClass, propertyName);
```

这样 Database 才能自动记录依赖关系。

## 6. 事件模型

事件按以下链路向上传播：

```text
Component
  -> Entity
    -> Domain
      -> Repository
```

值字段修改时：

```text
Component::SetProperty
  -> ComponentChanging
  -> 写入字段
  -> ComponentChanged
  -> EntityChanged
  -> DomainChanged
  -> RepositoryChanged
```

`Repository` 在 `DomainChanged` 后执行：

- 组件版本更新。
- 派生字段失效传播。
- 仓储事件发布。

事件参数不携带字段策略。`Repository`、撤销还原、持久化等订阅者需要用事件中的组件类型和字段名查询 `MetaRegistry`，再决定是否处理。

派生字段缓存失效不是普通字段修改事件，不会伪装成一次真实字段写入。

`SetProperties` 仍然按一次普通字段修改事件发布。事件中的 `PropertySet` 可能同时包含不同策略的字段，订阅者需要逐字段查询 meta 并过滤自己关心的部分。

## 7. 上层使用样例

### 7.1 创建 Repository、Domain、Entity

```cpp
#include <Database/IRepository.h>
#include <Database/IDomain.h>
#include <Database/IEntity.h>
#include <Data/uuid.h>

using namespace iCAX::Database;
using namespace iCAX::Data;

auto repo = GenerateRepository(GenerateNewUUID());

auto cadDomain = repo->CreateDomain(GenerateNewUUID(), true);
auto entity = cadDomain->CreateEntity(GenerateNewUUID());
```

### 7.2 声明组件

```cpp
class SizeComponent final : public CComponentBase
{
    DECLARE_ICAX_COMPONENT(SizeComponent, CComponentBase)
    DECLARE_ICAX_COMPONENT_CREATOR(SizeComponent)

    DECLARED_ICAX_FIELD(
        SizeComponent,
        int,
        Width,
        0,
        [](const int& lhs, const int& rhs) { return lhs == rhs; },
        [](const int& value) { return PropertyValue(value); },
        [](const PropertyValue& value) { return value.To<int>(); })

    DECLARED_ICAX_FIELD(
        SizeComponent,
        int,
        Height,
        0,
        [](const int& lhs, const int& rhs) { return lhs == rhs; },
        [](const int& value) { return PropertyValue(value); },
        [](const PropertyValue& value) { return value.To<int>(); })
};
```

### 7.3 添加和访问组件

```cpp
auto size = entity->AddComponent<SizeComponent>();

size->SetWidth(100);
size->SetHeight(40);

int width = size->GetWidth();
auto height = size->GetProperty(SizeComponent::PropertyName_Height).To<int>();
```

### 7.4 声明本 Entity 内部派生字段

```cpp
class AreaComponent final : public CComponentBase
{
    DECLARE_ICAX_COMPONENT(AreaComponent, CComponentBase)
    DECLARE_ICAX_COMPONENT_CREATOR(AreaComponent)

    DECLARED_ICAX_FIELD(
        AreaComponent,
        int,
        Width,
        0,
        [](const int& lhs, const int& rhs) { return lhs == rhs; },
        [](const int& value) { return PropertyValue(value); },
        [](const PropertyValue& value) { return value.To<int>(); })

    DECLARED_ICAX_FIELD(
        AreaComponent,
        int,
        Height,
        0,
        [](const int& lhs, const int& rhs) { return lhs == rhs; },
        [](const int& value) { return PropertyValue(value); },
        [](const PropertyValue& value) { return value.To<int>(); })

    DECLARED_ICAX_DERIVED_FIELD(
        AreaComponent,
        int,
        Area,
        [](const int& value) { return PropertyValue(value); },
        [](const AreaComponent& self, CDerivedPropertyContext& ctx) {
            int width = ctx.GetProperty(self, AreaComponent::PropertyName_Width).To<int>();
            int height = ctx.GetProperty(self, AreaComponent::PropertyName_Height).To<int>();
            return width * height;
        })
};
```

使用：

```cpp
auto area = entity->AddComponent<AreaComponent>();

area->SetWidth(10);
area->SetHeight(20);

int value1 = area->GetArea(); // 计算并缓存，200
int value2 = area->GetArea(); // 直接返回缓存

area->SetWidth(30);           // Area 被标记 Dirty
int value3 = area->GetArea(); // 重新计算，600
```

### 7.5 声明跨 Entity 派生字段

下面例子表达树形累计值：

```cpp
class ChainComponent final : public CComponentBase
{
    DECLARE_ICAX_COMPONENT(ChainComponent, CComponentBase)
    DECLARE_ICAX_COMPONENT_CREATOR(ChainComponent)

    DECLARED_ICAX_FIELD(
        ChainComponent,
        int,
        Local,
        0,
        [](const int& lhs, const int& rhs) { return lhs == rhs; },
        [](const int& value) { return PropertyValue(value); },
        [](const PropertyValue& value) { return value.To<int>(); })

    DECLARED_ICAX_FIELD(
        ChainComponent,
        uuid,
        ParentID,
        uuid(),
        [](const uuid& lhs, const uuid& rhs) { return lhs == rhs; },
        [](const uuid& value) { return PropertyValue(value); },
        [](const PropertyValue& value) { return value.To<uuid>(); })

    DECLARED_ICAX_DERIVED_FIELD(
        ChainComponent,
        int,
        Total,
        [](const int& value) { return PropertyValue(value); },
        [](const ChainComponent& self, CDerivedPropertyContext& ctx) {
            int total = ctx.GetProperty(self, ChainComponent::PropertyName_Local).To<int>();
            auto parentId = ctx.GetProperty(self, ChainComponent::PropertyName_ParentID).To<uuid>();

            if (parentId.is_nil())
            {
                return total;
            }

            auto entity = self.GetEntity();
            auto domain = entity->GetDomain();
            auto parentTotal = ctx.GetProperty(
                domain->GetID(),
                parentId,
                ChainComponent::S_ClassName,
                ChainComponent::PropertyName_Total);

            return total + parentTotal.To<int>();
        })
};
```

这里 `Total` 会依赖：

```text
self.Local
self.ParentID
parent.Total
```

如果 `ParentID` 变化，下次计算时旧依赖会被移除，新依赖会被建立。

### 7.6 查询组件版本

```cpp
size_t version = size->Version();
bool changed = size->IsChanged();

repo->ResetComponentChangedFlag();
```

组件字段修改后是否递增版本由 `Repository` 订阅事件并查询字段 meta 后决定。派生字段被标记 Dirty 本身不是一次真实字段写入。

非持久化可观察字段修改会递增版本。静默字段修改事件仍然发布，但 `Repository` 默认不把它计入版本变化。

### 7.7 事务提交与回滚

```cpp
auto tx = repo->BeginTransaction("InsertPart");

auto part = cadDomain->CreateEntity(partId);
auto size = part->AddComponent<SizeComponent>();
size->SetWidth(100);
size->SetHeight(40);

tx->Commit();
```

取消事务：

```cpp
auto tx = repo->BeginTransaction("TryModify");

size->SetWidth(200);

tx->Cancel(); // Width 恢复为事务开始前的值，不发布提交事件
```

如果事务对象析构前没有调用 `Commit()`，默认等价于 `Cancel()`。

### 7.8 撤销与重做

```cpp
if (repo->CanUndo())
{
    repo->Undo();
}

if (repo->CanRedo())
{
    repo->Redo();
}
```

撤销还原以一次提交的 `ChangeSet` 为单位。一次事务内多次修改同一个字段，只保留最早旧值和最终新值；删除后用同一 EntityID 或同一 ComponentClass 重建时，按替换处理，撤销后应恢复原内容。

### 7.9 快速保存与崩溃恢复

打开项目后挂接快速保存日志：

```cpp
repo->OpenOperationLog("project.icax.log", true);
```

之后每次用户提交都会追加一条操作日志。非持久化 Domain、非持久化字段和非事务字段不会写入快速保存日志。正常保存完整项目文件后，上层可以截断或重新创建日志。

崩溃恢复流程：

```cpp
auto repo = GenerateRepository(repositoryId);

LoadProjectFile(repo, "project.icax");     // 上层项目文件读取
repo->ReplayOperationLog("project.icax.log");
```

`ReplayOperationLog()` 按日志顺序正向回放。回放过程不进入撤销栈，也不会再次写入快速保存日志。

## 8. 线程模型

当前 `Database` 按单线程数据仓储使用设计。

推荐用法：

- 在仓库所属线程创建和修改 `Repository`。
- 不在多个线程中同时修改同一个 `Repository`。
- 如需跨线程传递命令，由上层通信机制把命令投递回数据所属线程执行。

当前实现没有为 `Repository`、`Domain`、`Entity`、`Component` 提供完整并发读写保护。

## 9. 生命周期

- `Repository` 拥有 `Domain`。
- `Domain` 拥有 `Entity`。
- `Entity` 拥有 `Component`。
- `Component` 通过弱引用访问所在 `Entity`。
- `Domain`、`Entity`、`Component` 事件监听者使用弱引用保存。
- 全局 `MetaRegistry` 只注册，不提供注销。

如果组件类型来自动态模块，该模块注册组件后不应在仍可能创建或访问该组件时卸载。

## 10. 测试要求

Database 单元测试位于：

```text
src/tests/icax/framework/Database/DatabaseTest/
```

当前派生字段测试覆盖：

- 派生字段懒计算和缓存。
- 普通字段变化后派生字段过期并重算。
- 派生字段不可写。
- 跨 Entity 动态依赖替换。
- 循环派生依赖检测。
- 字段声明注册持久化语义和修改传播策略。
- 可观察字段发布普通事件并更新版本，订阅者可通过 meta 识别其非事务语义。
- 静默字段发布普通事件，但默认不更新版本。
- 批量 `ChangeSet` 合并。
- 事务取消时静默回滚。
- 撤销还原按提交后的 `ChangeSet` 正反向应用。
- 删除实体后撤销可恢复组件和字段值。
- 同一 EntityID 或同一 ComponentClass 替换后撤销可恢复原内容。
- 快速保存日志可回放持久化修改。
- 非持久可观察字段不会写入快速保存日志。
- 非持久化 Domain 不会写入快速保存日志。

测试命令：

```powershell
pwsh -ExecutionPolicy Bypass -File src\tools\build\run_tests_debug_x64.ps1
```
