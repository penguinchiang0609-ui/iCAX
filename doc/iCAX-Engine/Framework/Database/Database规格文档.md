# Database 规格文档

## 1. 定位

`Database` 是 iCAX 的 EC 数据模型项目。

它向上层提供一个 `Repository`。`Repository` 是项目内唯一的数据容器，直接管理 `Entity`、`Component`、字段元数据、事件、组件版本、派生字段、批量变更、事务、撤销还原和快速保存日志。

临时编辑、预览和导入不再通过 Database 内的多数据空间隔离表达；这些场景应使用轻量项目会话、Repository 快照或导入事务表达。

项目文件格式识别、schema/version 维护和版本迁移属于 File 模块职责。Database 只提供加载基线、操作日志回放和 EC 写入能力，不内置项目文件升级链。快速保存日志不做新旧版本识别或迁移，必须由当前程序按当前日志格式写入并回放。日志 `magic/version` 由调用方在打开和回放时传入，Database 只负责写入头部并严格匹配。

## 2. 核心对象

### 2.1 Repository

`Repository` 是项目数据的顶级容器。

它负责：

- 创建、查询、删除实体。
- 维护项目级 meta entity。
- 发布结构和字段变更事件。
- 维护组件版本和 changed 标记。
- 管理字段元数据和派生字段缓存。
- 提供批量变更、事务、撤销还原和快速保存日志。

典型创建方式：

```cpp
auto repo = iCAX::Database::GenerateRepository(projectId);
```

### 2.2 Entity

`Entity` 是组件容器。它只表达身份，不直接承载业务字段。

```cpp
auto entity = repo->CreateEntity(iCAX::Data::GenerateNewUUID());
auto transform = entity->AddComponent<TransformComponent>();
```

### 2.3 Component

`Component` 描述实体的身份、状态或能力。字段类别由 component 的 meta 声明决定。

字段类别包括：

- `Persistent + Transactional`：持久化字段，参与事务、撤销还原和快速保存。
- `Persistent + Observable`：持久化字段，正常发事件，但不进入撤销还原。
- `Transient + Observable`：运行态可观察字段，发事件，不持久化。
- `Silent`：内部缓存字段，不触发版本和派生失效。
- `Derived`：派生字段，由 evaluator 计算并缓存。
- 运行时裸字段：普通 C++ 成员，不进入 meta、事件、版本和持久化系统。

组件启用状态是组件基础状态，不是字段类别。`Enable()` / `Disable()` 会发布组件状态事件、影响组件版本，并作为 `EnableComponent` / `DisableComponent` 操作进入事务、撤销还原和快速保存日志。它不会出现在 `GetProperties()` 返回的 `PropertySet` 中，也不通过字段 meta 控制。

组件版本是 `uint64_t`，由 Repository 的版本表维护。脱离 Repository 的组件版本返回 0；进入 Repository 后，新增、字段修改、启用状态变化等会按字段 meta 和写入管线递增版本。该版本和 PDO 的 `dataVersion` 使用同一类型，可直接作为高频数据写入 PDO 时的源数据版本。

```cpp
auto component = entity->AddComponent<CVisibleComponent>();
component->Disable();
component->Enable();
```

## 3. Repository API

### 3.1 实体管理

```cpp
auto entity = repo->CreateEntity(entityId);
std::shared_ptr<IEntity> created;
std::string error;
if (!repo->CreateEntity(entityId, created, error))
{
    // error 描述失败原因
}
auto exists = repo->HasEntity(entityId);
auto same = repo->GetEntity(entityId);
auto ids = repo->GetEntityIDs();
repo->DeleteEntity(entityId);
```

`Repository::GetView()` 提供按组件类型构建和读取实体视图，主要供 Behaviour 调度器使用。

### 3.2 Meta Entity

`GetMetaEntity()` 返回仓储自身的描述实体，用于项目级配置或启动组件。

```cpp
repo->GetMetaEntity()->AddComponent<ProjectSettingComponent>();
```

## 4. 写入过滤

组件修改、组件添加和组件移除在真正落地前会先经过 `ModifyFilter`。

`ModifyFilter` 不依赖 `Changing` 事件，也不修改数据；它统一调用当前 Repository 所属 `MetaRegistry` 中的 checker：

- `AllowAttachByName`：组件添加前调用。
- `AllowRemoveByName`：组件移除前调用。
- `AllowModify`：组件字段修改前调用。

正式写入 API 使用 `bool + out string` 表达业务失败：

```cpp
std::string error;
if (!component->SetProperties({
    { CSizeComponent::PropertyName_Length, PropertyValue(200) },
    { CSizeComponent::PropertyName_Width, PropertyValue(100) }
}, error))
{
    // 修改未落地，不发布事件，不写日志，不进入撤销历史
}
```

多字段修改是一个操作，只触发一次 `AllowModify`。checker 应基于“组件当前状态 + 本次 Properties 覆盖”判断预期状态是否合法。

## 5. 事件

Repository 发布两类事件：

- `OnRepositoryChanging`：修改前。
- `OnRepositoryChanged`：修改后。

事件包含操作类型、实体 ID、组件类型、旧值、新值、组件指针、实体指针和 Repository 指针。

批量提交时，Repository 会发布一次 `kBatchChanged`，上层可以把它作为刷新和 UI 合并更新的边界。

`ChangeSet` 是提交后的内部净变更摘要，会合并同一字段的多次修改。撤销、重做、事务回滚和快速保存不以 ChangeSet 为事实来源，而是使用内部有序 `OperationBatch`。

## 6. 批量变更

`BeginBatch()` / `EndBatch()` 用于合并大量修改。

```cpp
repo->BeginBatch();
// 多次创建实体、添加组件、修改字段
repo->EndBatch();
```

提交后：

- 内存中所有修改已经完成。
- Repository 只对外发布一次批量变更事件。
- 快速保存日志追加有序 OperationBatch。
- 如果该批量变更发生在撤销记录边界内，会被纳入当前 undo step。
- 如果批量内的操作互相抵消，最终 ChangeSet 为空，则不发布批量事件、不进入撤销历史、不写快速保存日志。

## 7. 事务

事务用于数据库式提交和放弃。事务对象只记录操作意图，不负责提交或取消；生命周期由 Repository 的 `BeginTransaction()`、`CommitTransaction(transaction)`、`CancelTransaction(transaction)` 三个接口统一管理。

```cpp
auto& tx = repo->BeginTransaction("Move entities");
tx.ModifyComponent(entityID, CTransformComponent::S_ClassName, {
    { CTransformComponent::PropertyName_X, PropertyValue(120.0) }
});
tx.DisableComponent(entityID, CToolPathComponent::S_ClassName);

if (!repo->CommitTransaction(tx))
{
    // 提交失败，Repository 已回滚提交阶段已经应用的部分修改。
}
```

`CommitTransaction(tx)` 后写入快速保存日志，但不会自动进入撤销还原栈。事务如果被撤销记录边界包含，则其修改会被撤销记录捕获。

`CancelTransaction(tx)` 会丢弃尚未提交的操作意图，不产生事件、撤销记录或快速保存日志。

事务回滚按 OperationBatch 的反向顺序执行。这样可以保证字段之间存在顺序约束时，回滚过程仍遵守原始约束。

## 8. 撤销还原

撤销还原独立于事务和批量变更。它只提供记录边界：

```cpp
auto undo = repo->BeginUndoCommand("Move");
// 普通修改、批量修改或已提交事务
undo->End();

repo->Undo();
repo->Redo();
```

规则：

- `BeginUndoCommand(name)` 开始监听。
- `End()` 结束监听并生成一个 undo step。
- `Cancel()` 不作为公开命令；需要放弃修改时使用事务回滚或业务层自行处理。
- 只有 `Transactional` 字段进入撤销还原。
- 组件启用状态不是字段，但视为组件基础状态，进入撤销还原。
- `Observable` 字段仍正常发事件，但不进入 undo/redo。
- Undo 使用反向 OperationBatch，Redo 使用原 OperationBatch，因此同一个 step 内的字段修改顺序会被严格保留。
- 撤销还原历史由 Repository 外挂维护，`Entity` 和 `Component` 不保存历史栈。

## 9. 快速保存

快速保存日志是追加式操作日志。

```cpp
const std::string quickSaveMagic = "PRODUCT_QUICK_SAVE_LOG";
const uint32_t quickSaveVersion = 1;

repo->OpenOperationLog("project.icax.log", true, quickSaveMagic, quickSaveVersion);
// 后续提交会自动追加日志
repo->CloseOperationLog();
```

Repository 只提供当前格式的日志读写和回放能力，不负责项目文件生命周期，也不负责日志格式迁移。日志第一条非空记录是头部，包含调用方传入的 `magic` 和 `version`；回放或追加打开时不匹配则拒绝。完整闭环由 Project 层组织：

```cpp
// 项目文件基线加载完成后
project.ReplayQuickSaveLog(quickSaveMagic, quickSaveVersion);
project.OpenQuickSaveLog(false, quickSaveMagic, quickSaveVersion);

// 项目文件完整保存成功后
project.MarkProjectFileSaved("", quickSaveMagic, quickSaveVersion);
```

崩溃恢复流程：

```cpp
auto repo = LoadProjectFile("project.icax");
repo->ReplayOperationLog("project.icax.log", quickSaveMagic, quickSaveVersion);
```

`ReplayOperationLog` 只能在 Repository 空闲态调用：不能处于 active batch、active transaction、undo command recording、history replay，也不能已经打开 operation log 追加写入。违反这些前置条件会直接抛出异常，避免恢复流程和用户编辑流程交叉。

快速保存记录：

- 结构性修改。
- 组件启用状态修改。
- `Persistent + Transactional` 字段修改。
- 普通修改、批量提交、事务提交、撤销和重做产生的有序操作。

快速保存不记录：

- 非持久化字段。
- `Observable`、`Silent`、`Derived` 和运行时裸字段。

## 10. 派生字段

派生字段由 meta 声明 evaluator。

```cpp
DECLARED_ICAX_DERIVED_FIELD(CSumComponent, int, Sum, ToIntVariant,
    [](const CSumComponent& self, CDerivedPropertyContext& context) -> int
    {
        return context.GetProperty(self, CSumComponent::PropertyName_A).To<int>()
            + context.GetProperty(self, CSumComponent::PropertyName_B).To<int>();
    })
```

派生字段规则：

- 首次读取时计算。
- 依赖字段变化后失效。
- 下次读取时重新计算。
- 支持依赖同一实体或其他实体的字段。
- 检测循环依赖并抛出异常。
- 不允许依赖 `Silent` 值字段。

## 11. 线程模型

当前 `Repository` 面向单后台线程使用，不提供完整并发读写保护。

前端交互应通过 MailChannel 进入后台线程；高频可丢弃状态通过 PDO 传递。
