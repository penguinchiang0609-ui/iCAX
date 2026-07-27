# View 运行时与输出会话规格

## 1. 目标

Framework View 表达“从同一个 Scene 数据现场出发，以不同关注范围和表现方式观察数据”。

典型例子包括机床视图、工件视图、加工视图和综合视图。它们共享同一个 Scene Repository 和 ResourceLibrary，但拥有独立的内容集合、表现覆盖和输出资源。

View 不等于临时 Scene：

- 临时导入、预览等需要独立数据生命周期的现场继续使用独立 Scene 和独立 Database。
- 机床、工件、作业等共享一份业务数据的观察维度使用 View。

## 2. 核心对象

### 2.1 ViewDefinition

`SViewDefinition` 是产品注册的静态定义，包含：

- `ID`：Framework 不解释的不透明字符串。
- `EntityWhere`：决定 View 关注哪些 Entity。
- `IViewPresentationProvider`：可选的 View 局部表现计算器。
- `IViewOutputProvider[]`：可选的输出会话工厂。

Framework 不包含 `Machine`、`Workpiece` 等产品枚举，也不硬编码任何产品规则。

### 2.2 ViewInstance

`OpenView` 每调用一次都创建一个独立 ViewInstance。即使 Definition 和 Context 相同，也不会共享实例。

每个实例拥有：

- 独立 `ViewInstanceID`；
- 一次 Database `IEntityView` 使用权；
- View 内容 revision；
- 一组输出会话；
- 自己的输出资源生命周期。

不共享实例是为了允许同一 Definition 的两个视口在以后拥有不同相机、选择、表现参数或输出目标。

### 2.3 ViewContent

`SViewContent` 是可丢弃、可重建的派生快照：

- `InstanceID`
- `DefinitionID`
- `Revision`
- `Objects[]`
- `Outputs[]`

`SViewObject::EntityID` 是 Scene Entity 与输出对象的关联键。View 归属不会写入 Entity Component。

`Presentation` 只在当前 ViewInstance 中生效，不写回 Database。因此同一 Entity 可以在 A View 使用红色材质，在 B View 使用蓝色材质。

### 2.4 OutputSession

Framework 只定义通用 `IViewOutputProvider/IViewOutputSession`，不依赖 PDO 或 RenderService。

输出会话负责：

- `Open`：创建该实例独占的输出资源；
- `GetDescriptor`：向调用方公开输出类型和不透明参数；
- `Synchronize`：根据 ViewContent 和输出侧 revision 同步派生数据；
- `Close`：整体释放输出资源。

一个 Definition 可以同时挂载多个输出，例如 `render`、`table`、`tree`。

## 3. 生命周期

### 3.1 创建

```text
View.Open
  -> 查找 ViewDefinition
  -> Database.CreateEntityView(where, context)
  -> 订阅 EntityView 变化
  -> 为每个 OutputProvider 创建 OutputSession
  -> 构建首个 ViewContent
  -> 首次同步输出
  -> 返回 InstanceID、Objects、Outputs
```

### 3.2 更新

View 不在每帧查询全部 Entity。

- Entity 是否进入或离开 View：由 Database EntityView 增量维护并通过事件标脏。
- View Presentation：存在 PresentationProvider 时，Repository 事件只负责标脏，实际重建在 Scene Tick 中合并执行。
- 输出数据变化：输出实现使用自己的轻量 revision/version 判断；revision 未变化时不得复制完整快照或重写输出。
- 内容变化后，Framework 发送 `View.ContentChanged` 事件；前端按 `viewInstanceId` 重新调用 `View.GetContent`。

### 3.3 释放

`View.Close` 必须：

1. 从运行实例表移除实例；
2. 关闭全部 OutputSession；
3. 移除 EntityView/Repository 监听；
4. 调用 `ReleaseEntityView` 归还 Database 使用权。

关闭操作是幂等的：不存在的实例返回 `closed=false`。

## 4. SDO 协议

### 4.1 View.Open

请求：

```json
{
  "viewDefinitionId": "icax.laser-3d-cam.view.machine",
  "context": {}
}
```

响应：

```json
{
  "viewInstanceId": "uuid",
  "viewDefinitionId": "icax.laser-3d-cam.view.machine",
  "revision": 1,
  "objects": [
    { "entityId": "uuid", "presentation": {} }
  ],
  "outputs": [
    {
      "type": "render",
      "properties": { "renderSceneId": "14533068960222087123" }
    }
  ]
}
```

`renderSceneId` 使用字符串传输，避免 JavaScript 64 位整数精度损失。

### 4.2 View.GetContent

运行实例请求：

```json
{ "viewInstanceId": "uuid" }
```

旧的 `{ "viewDefinitionId": "...", "context": {} }` 入口仅保留为无输出会话的兼容快照接口。

### 4.3 View.Close

请求：

```json
{ "viewInstanceId": "uuid" }
```

响应：

```json
{ "viewInstanceId": "uuid", "closed": true }
```

## 5. Render 输出实现

Laser3DCAM 为每个 ViewInstance 创建一个独立 RenderScene：

```text
Database Scene
  ├─ 基础 RenderScene：Behaviour 发布完整语义渲染数据
  ├─ Machine View RenderScene：只投影机床对象
  ├─ Workpiece View RenderScene：只投影工件对象
  └─ Machining View RenderScene：按 Definition 投影对象
```

View RenderScene 从基础 RenderScene 投影：

- View 命中的 render objects；
- 这些 object 引用的 geometry；
- object 和 camera 所需的 transform；
- camera；
- View 局部 Presentation 覆盖。

RenderScene 通过 `OutputSceneID` 绑定到拥有 Database/PDOHub 的 Scene。`PDORenderService::Update` 只处理当前 Scene 的基础 RenderScene 和绑定到它的 View RenderScene。

PDOID 包含 `ProjectID + RenderSceneID + 对象身份`，所以同一个 Entity 出现在多个 View 中时拥有互不冲突的 PDO slot。

View/投影层不调用 `AllocateSlot/FreeSlot`。slot 分配、版本写入、事件发送、销毁回收仍由 PDORenderService 统一负责。

## 6. 与 LayerMask 的边界

View membership 和 LayerMask 是两个正交维度：

- View membership：决定某个 Entity 是否进入当前 View 的输出集合。
- LayerMask：在某个 View 内，由相机决定某类已进入 View 的对象是否可见。

不得使用 LayerMask 代替 ViewInstance，也不得把 ViewID 写入 Entity Component。

## 7. 不变量

- 一个 ViewInstance 只属于一个 Database Scene。
- ViewDefinition 不持有运行期 EntityID。
- Entity 不保存 View 归属。
- 每次成功 `OpenView` 必须最终对应一次 `CloseView` 或服务卸载清理。
- 输出会话必须整体拥有、整体释放自己的输出资源。
- 前端必须按输出 descriptor 选择 RenderScene，不能把不同 RenderScene 的 PDO 混在同一可见对象表中。
