# Database 规格文档

## 1. 定位

`Database` 是 iCAX 的 EC 数据模型项目。

它向上层提供一个 `Repository`。`Repository` 是项目内唯一的数据容器，直接管理 `Entity`、`Component`、字段元数据、事件、组件版本、派生字段、批量变更、事务、撤销还原和快速保存日志。

临时编辑、预览和导入不再通过 Database 内的多数据空间隔离表达；这些场景应使用轻量项目会话、Repository 快照或导入事务表达。

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

## 3. Repository API

### 3.1 实体管理

```cpp
auto entity = repo->CreateEntity(entityId);
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

## 4. 事件

Repository 发布两类事件：

- `OnRepositoryChanging`：修改前。
- `OnRepositoryChanged`：修改后。

事件包含操作类型、实体 ID、组件类型、旧值、新值、组件指针、实体指针和批量变更信息。

批量提交时，Repository 会发布一次 `kBatchChanged`，上层可以根据 `ChangeSet` 做日志、刷新和 UI 合并更新。

## 5. 批量变更

`BeginChangeScope(EChangeScopeKind::UserCommand)` 用于合并大量修改。

```cpp
auto scope = repo->BeginChangeScope(iCAX::Database::EChangeScopeKind::UserCommand, "Import");
// 多次创建实体、添加组件、修改字段
scope->Commit();
```

提交后：

- 内存中所有修改已经完成。
- Repository 只对外发布一次批量变更事件。
- 快速保存日志追加合并后的 ChangeSet。
- 如果该批量变更发生在撤销记录边界内，会被纳入当前 undo step。

## 6. 事务

事务用于数据库式提交和回滚。事务内修改立即作用到内存。

```cpp
auto tx = repo->BeginTransaction("Move entities");
// 修改数据
tx->Commit();
```

`Commit()` 后写入快速保存日志，但不会自动进入撤销还原栈。事务如果被撤销记录边界包含，则其修改会被撤销记录捕获。

`Cancel()` 或未显式提交即析构时，会按反向日志静默回滚。

## 7. 撤销还原

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
- `Observable` 字段仍正常发事件，但不进入 undo/redo。
- 撤销还原历史由 Repository 外挂维护，`Entity` 和 `Component` 不保存历史栈。

## 8. 快速保存

快速保存日志是追加式操作日志。

```cpp
repo->OpenOperationLog("project.icax.log", true);
// 后续提交会自动追加日志
repo->CloseOperationLog();
```

崩溃恢复流程：

```cpp
auto repo = LoadProjectFile("project.icax");
repo->ReplayOperationLog("project.icax.log");
```

快速保存记录：

- 结构性修改。
- `Persistent + Transactional` 字段修改。
- 普通修改、批量提交、事务提交、撤销和重做产生的最终变更。

快速保存不记录：

- 非持久化字段。
- `Observable`、`Silent`、`Derived` 和运行时裸字段。

## 9. 派生字段

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

## 10. 线程模型

当前 `Repository` 面向单后台线程使用，不提供完整并发读写保护。

前端交互应通过 MailChannel 进入后台线程；高频可丢弃状态通过 PDO 传递。
