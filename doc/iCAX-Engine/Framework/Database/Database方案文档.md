# Database 方案文档

## 1. 设计目标

`Database` 以 `Repository` 作为唯一上层数据入口。上层不再在 Database 内拆分多个数据空间来表达 CAD、CAM、临时编辑或预览数据；这些场景通过轻量项目会话、Repository 快照、导入事务或业务对象组合来表达。

核心目标：

- Repository 直接承载 EC 数据。
- Entity 只表达身份。
- Component 只表达数据、状态或能力。
- Behaviour、Project、ApplicationHost 不依赖内部数据空间实现。
- 日志、事务、撤销还原和快速保存统一基于有序 OperationBatch；ChangeSet 只作为提交后的净变更摘要。

## 2. 当前代码结构

```text
Database/
  IRepository.h / Repository.h / Repository.cpp
    -> Repository 对外接口和默认实现

  IEntity.h / Entity.h / Entity.cpp
    -> Entity 接口和实现

  ComponentBase.*
  ComponentHelper.h
    -> Component 基类、字段声明宏和属性事件

  MetaRegistry.*
    -> Component 类型和字段 meta 注册表

  ChangeSet.*
    -> 变更集合和合并摘要，用于批量事件、版本和派生字段失效

  RepositoryHistory.*
    -> 撤销还原历史，外挂维护

  OperationLog.*
    -> 有序操作批次、字段过滤、反向批次生成和快速保存日志读写

  DerivedProperty.*
    -> 派生字段缓存、依赖和失效

  VersionTable.*
    -> 组件版本和 changed 标记
```

实现内部由 Repository 直接持有 Entity 表、EntityView、事件汇总、版本表和派生字段管理器。外部代码只通过 `Repository` 访问 EC 数据。

## 3. Repository 门面

Repository 新增了直接的 EC 容器接口：

```cpp
std::shared_ptr<IEntity> CreateEntity(const uuid& id);
bool HasEntity(const uuid& id) const;
std::shared_ptr<IEntity> GetEntity(const uuid& id) const;
void DeleteEntity(const uuid& id);
std::vector<uuid> GetEntityIDs() const;
IEntitiesView& GetView() const;
```

Behaviour 调度器使用 `Repository::GetView()` 枚举组件；插件行为使用 `Repository::GetEntity()` 和 `Repository::DeleteEntity()` 处理实体关系。

## 4. 事件链路

```mermaid
sequenceDiagram
    participant C as Component
    participant E as Entity
    participant R as Repository
    participant B as Behaviour

    C->>E: OnComponentChanging/Changed
    E->>R: OnEntityChanging/Changed
    R->>R: version / derived invalidation / changeset
    R->>B: OnRepositoryChanging/Changed
```

Repository 是对外事件发布者。Project 订阅 Repository 事件，再携带 UniverseContext 转发给 Universe。

## 5. OperationBatch 与 ChangeSet

Database 内部同时维护两种变更视图：

- `OperationBatch`：按实际发生顺序追加每一条操作，不合并字段。它是事务回滚、撤销还原、重做和快速保存回放的事实来源。
- `ChangeSet`：由 OperationBatch 派生出来的净变更摘要，会合并同一字段的多次修改，主要用于 `kBatchChanged` 事件、版本更新和派生字段失效。

OperationBatch 记录：

- 创建实体。
- 删除实体。
- 添加组件。
- 删除组件。
- 修改字段。

OperationBatch 支持：

- 按原始顺序回放。
- 按反向顺序生成回滚批次。
- 按字段 meta 过滤事务型字段和持久化字段。
- 序列化为追加式快速保存日志。

ChangeSet 支持：

- 合并多次字段修改为净结果。
- 抵消批量内的临时创建、删除和字段恢复。
- 批量事件携带。

## 6. 批量变更

批量变更由 `BeginChangeScope(EChangeScopeKind::UserCommand)` 创建。

变更期间，普通细粒度事件会被记录到 builder；提交时统一：

1. 冻结有序 OperationBatch。
2. 从 OperationBatch 派生 ChangeSet 净摘要。
3. 如果 ChangeSet 非空，应用版本和派生字段失效。
4. 发布 `kBatchChanged`，事件携带 ChangeSet。
5. OperationBatch 进入撤销记录器。
6. OperationBatch 追加快速保存日志。

`LoadBaseline` 是静默加载基线模式。它用于首次打开项目文件，提交后清空版本脏标记、派生缓存和历史，不发布普通用户修改语义。

## 7. 事务

事务由 `BeginTransaction()` 创建。

事务内修改直接落到内存；取消时基于 OperationBatch 生成反向批次，并按倒序静默回滚。提交时写入快速保存日志，但不自动进入 undo 栈。

事务和撤销还原不绑定。只有事务提交发生在 `BeginUndoCommand(name)` / `End()` 之间时，才会被当前 undo step 捕获。

## 8. 撤销还原

撤销还原由 `CRepositoryHistory` 外挂维护。

Repository 公开上层友好的单数据容器接口：

```cpp
auto cmd = repo->BeginUndoCommand("Rename");
cmd->End();

repo->Undo();
repo->Redo();
```

内部规则：

- Begin/End 只定义监听边界。
- End 时把期间捕获的 OperationBatch 汇总为一个 step，保留真实操作顺序。
- Undo 使用反向 OperationBatch 回放，内部按原操作的反向顺序恢复。
- Redo 使用原 OperationBatch 回放，保持用户原始修改顺序。
- 事务、批量变更和普通修改是独立概念，只是在日志底座上被 undo recorder 监听。

## 9. 快速保存

快速保存由 `COperationBatchJournal` 实现。

每次提交后，Repository 会把可持久化 OperationBatch 追加到日志。正常保存完整项目文件后，上层可以截断或重建日志。

崩溃恢复时：

1. 加载完整项目文件。
2. 创建 Repository。
3. 回放操作日志。
4. 恢复到最后一次写入日志的状态。

## 10. 派生字段

派生字段由 `CDerivedPropertyManager` 管理。

核心数据结构：

```text
target property -> cached value/state
source property -> dependent properties
derived property -> source properties
```

读取派生字段时：

1. 如果缓存 clean，直接返回。
2. 如果 dirty，调用 evaluator。
3. evaluator 通过 `CDerivedPropertyContext` 读取依赖字段。
4. context 记录依赖关系。
5. 计算完成后写入缓存。

字段修改时，Repository 根据 meta 决定是否失效派生字段：

- `Transactional` 和 `Observable` 值字段会触发失效。
- `Silent` 字段不触发默认失效。
- `Derived` 字段自身不写入日志。

## 11. 与其他 Framework 项目的关系

Behaviour 通过 `Repository::GetView()` 枚举组件并执行行为。

Project 负责项目会话、资源库和 Repository 生命周期；Database 不关心项目打开方式。

CommandHandler 通过 `ICommandContext` 获取 `IRepository`，执行命令时显式创建撤销记录边界。
