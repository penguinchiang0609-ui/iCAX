# 16 CAD for CAM 工件编辑与刀路关联详细设计

> 说明：本文继续负责 CAD 导入、修复、编辑提交和 CAD 特征重新关联。工件区刀路的领域名称、组织方式、工艺衍生、合并、打断、替代关系及有效输出规则，以 `17-工件区意图刀路衍生模型.md` 为准；本文早期使用的 `ToolpathDefinition`、`ToolpathResult` 仅表示分层方向，不再作为最终领域模型。

## 1. 模块定位

工件大区采用 CAD for CAM 定位，负责把外部 CAD 模型准备为可识别、可拾取、可编程的制造工件，并在同一工件上下文中完成机床无关的刀路设计。

工件大区包含两个工作模式：

- `CAD 准备`：导入、检查、自动修复、交互修复和几何版本提交。
- `刀路设计`：特征识别、拓扑拾取、刀路绘制、刀路定义编辑和刀路结果计算。

两种模式共享当前工件、中央三维视口、相机状态和选择上下文。模式切换不改变当前工件，也不自动提交 CAD 编辑。

本模块不负责：

- 通用参数化零件设计。
- 机床选择、装夹和工件实例摆放。
- 关节姿态、逆解和机床轴轨迹。
- 作业排序、仿真和 NC 输出。

## 2. 核心原则

1. 一个项目可以包含多个工件，连续导入不得覆盖已有工件。
2. CAD 编辑发生在候选版本中；提交前不影响当前正式几何和现有刀路。
3. 工件几何、制造特征、刀路定义和刀路计算结果分层保存。
4. 刀路定义保存加工意图，刀路结果是针对某个几何版本计算出的空间曲线。
5. CAD 修改后，能证明关联的刀路自动重算；存在歧义的刀路要求复核；无法关联的刀路明确失效。
6. 系统不得为追求“全部随动”而静默错绑拓扑。
7. 所有几何版本均使用稳定的工件局部坐标系；作业区通过工件实例变换放置工件。
8. 原始导入数据、已提交版本和候选编辑版本不得互相覆盖。

## 3. 领域对象

```text
WorkpieceEntity
  CWorkpieceComponent
    Name
    SourcePath
    WorkpieceCoordinateSystemID
    OriginalRevisionID
    CurrentRevisionID
    DraftRevisionID
    RevisionSerial
    EditState
```

工件 Entity 表达用户管理的工件身份，不直接保存唯一 BRep。所有几何引用进入修订对象。

```text
GeometryRevisionEntity
  CGeometryRevisionComponent
    WorkpieceEntityID
    RevisionNumber
    ParentRevisionID
    State                 // Draft / Current / Historical / Rejected
    RawCadResourceID
    BRepResourceID
    TopologyResourceID
    RenderResourceID
    CollisionResourceID
    InspectionReportResourceID
    RepairReportResourceID
    TopologyHistoryResourceID
    ContentHash
```

```text
ManufacturingFeatureEntity
  CManufacturingFeatureComponent
    WorkpieceEntityID
    GeometryRevisionID
    FeatureKind
    FeatureSignatureResourceID
    SourceTopologyRefs[]
    RecognitionRecipeResourceID
    State
```

```text
ToolpathDefinitionEntity
  CToolpathDefinitionComponent
    WorkpieceEntityID
    Name
    SourceKind            // Feature / Topology / Sketch
    SourceFeatureID
    SourceSelectionResourceID
    RecipeResourceID
    ProcessParametersResourceID
    Enabled
```

```text
ToolpathResultEntity
  CToolpathResultComponent
    DefinitionEntityID
    GeometryRevisionID
    CurveResourceID
    Status                // Current / Recomputed / NeedsReview / Ambiguous / Orphaned / Failed
    AssociationConfidence
    AssociationReportResourceID
```

当前 `CPathComponent` 可以在迁移期同时承担 Definition 和 Result 的部分职责，但目标结构必须将生成定义与计算结果分开。作业区只消费已确认的 `ToolpathResult`。

## 4. 工件局部坐标系

CAD 模型、制造特征、选择引用和机床无关 Toolpath 统一使用工件局部坐标系。

```text
Workpiece local coordinates
  GeometryRevision
  ManufacturingFeature
  ToolpathDefinition
  ToolpathResult
```

工件在具体作业中的位置由 `WorkpieceInstanceTransform` 表达，不回写工件资源和 Toolpath。CAD 重新导入或修复不得静默改变工件单位、原点和轴方向。

## 5. 导入与自动修复

每次导入创建新工件及其第一个几何修订：

```text
1. 校验 STEP/STP/IGS/IGES
2. 读取并内嵌原始 CAD
3. 解析单位和源坐标
4. 生成原始 BRep
5. 执行模型检查
6. 执行安全的自动修复
7. 对修复结果复检
8. 生成 Topology、RenderMesh 和 CollisionGeometry
9. 创建 GeometryRevision v1
10. 创建 Workpiece 并设为当前工件
```

自动修复至少包括：

- Wire、Edge 和 pcurve 基础修复。
- Sewing 缝合。
- 退化拓扑清理。
- Shell 修复与闭合检查。
- 在条件满足时构造 Solid。
- 整体 ShapeFix 和有效性复检。

原始 BRep 与自动修复后的正式 BRep 必须分别保存。自动修复报告记录修复前后统计、使用容差、仍未解决的问题和是否允许进入刀路设计。

## 6. CAD 编辑会话

用户从当前正式修订进入 CAD 准备模式时创建 Draft：

```text
Current revision v3
  -> BeginEdit
Draft revision v4
```

Draft 拥有独立的运行时 BRep、撤销栈和修改历史。编辑操作不得原地修改 v3 的资源。

命令边界建议为：

```text
WorkpieceEdit.Begin
WorkpieceEdit.ApplyOperation
WorkpieceEdit.Undo
WorkpieceEdit.Redo
WorkpieceEdit.Inspect
WorkpieceEdit.PreviewImpact
WorkpieceEdit.Commit
WorkpieceEdit.Discard
```

第一批交互修复操作：

- 选择开放边并缝合。
- 删除坏面。
- 对闭合边界自动补面。
- 替换破面。
- 修复 Wire、Edge 和 pcurve。
- 调整局部修复容差。
- 执行局部或整体 ShapeFix。

提交条件：

- Draft 可以转换为可持久化 BRep。
- 单位和工件局部坐标系未发生未确认变化。
- 已生成检查报告。
- 已生成拓扑修改历史和刀路影响报告。
- 用户确认所有阻断性诊断。

提交后 Draft 变为 Current，旧 Current 变为 Historical。Discard 只删除 Draft，不影响旧几何、特征和刀路。

## 7. 拓扑历史

每个 CAD 操作都应尽可能记录：

```text
TopologyChange
  InputRefs[]
  OutputRefs[]
  ChangeKind            // Unchanged / Modified / Generated / Deleted / Split / Merge
  OperationID
  Confidence
```

OCCT 算法提供的 `Modified`、`Generated`、`IsDeleted` 信息应在算法边界内立即转换为产品自己的 `TopologyHistoryResource`，不得把 OCCT Shape 指针持久化。

多个编辑操作的历史在提交时合成为从父版本到新版本的变更图。历史缺失不等于拓扑未变化。

## 8. 刀路定义与结果

刀路定义保存可重新计算的加工意图：

```text
ToolpathRecipeResource
  SourceKind
  RecognitionOrSelectionRule
  CuttingDirection
  StartRule
  LeadIn
  LeadOut
  MicroJoints
  Compensation
  ProcessParameters
```

刀路结果保存针对特定几何版本生成的曲线：

```text
ToolpathResultResource
  GeometryRevisionID
  Curve
  SourceSnapshot
  GenerationDiagnostics
```

三种创建来源统一生成独立结果曲线：

- 特征识别。
- Edge、Loop、Face 拾取。
- 用户绘制。

来源拓扑用于重新计算和诊断，不作为结果曲线存活的唯一条件。

## 9. CAD 修改后的关联与重算

提交 Draft 时按以下顺序处理每个 ToolpathDefinition：

1. 使用拓扑修改历史进行精确映射。
2. 对 Feature 来源重新执行特征识别并匹配特征签名。
3. 使用曲线/曲面类型、尺寸、位置、邻接关系和采样点进行几何签名匹配。
4. 匹配唯一且超过安全阈值时重新计算 ToolpathResult。
5. 存在多个候选时标记 `Ambiguous`。
6. 来源被删除或无法找到候选时标记 `Orphaned`。
7. 手工绘制刀路默认保留独立曲线，但几何改变影响其安全性时标记 `NeedsReview`。

系统不得简单复制旧结果曲线并将其标记为新版本有效。关联成功后必须使用原 ToolpathDefinition 在新几何上重新计算结果。

## 10. 影响预览和用户确认

提交 CAD 修改前必须提供影响摘要：

```text
未受影响
可自动重新计算
需要复核
存在多个候选
来源已删除
```

用户可以在视口中同时查看旧结果、新几何和候选拓扑。`Ambiguous` 和 `Orphaned` 不允许进入作业规划。用户重新拾取、确认候选或删除刀路定义后才能解除阻断。

## 11. 前端工作台

左侧保持统一工件树：

```text
工件 A
  几何
  检查与修复
  特征
  刀路
工件 B
```

中央视口共享相机和工件上下文：

- CAD 准备模式显示问题拓扑、修复预览和版本差异。
- 刀路设计模式显示候选特征、拾取对象、刀路定义和计算结果。

右侧属性面板按当前模式和选择对象切换。CAD 编辑未提交时切换工件、关闭项目或进入作业区必须提示提交或放弃 Draft。

## 12. 作业区消费规则

作业只能引用：

- 工件的 Current GeometryRevision。
- 状态为 `Current`、`Recomputed` 或已人工确认的 ToolpathResult。

当工件提交新几何版本后，引用旧版本的作业进入 `NeedsReplan`。系统不得静默替换作业中的几何和刀路结果。

## 13. 分阶段实现

### 阶段一：工件和几何版本骨架

- 多工件导入、列表和当前工件切换。
- GeometryRevision 与 Current/Draft/Historical 状态。
- Begin/Commit/Discard 编辑会话。
- 检查、自动修复和报告。

### 阶段二：高频 CAD for CAM 修复

- 开放边诊断和选择。
- 缝合、坏面删除、闭合边界补面。
- 局部 ShapeFix。
- 撤销、重做和修复预览。
- TopologyHistoryResource。

### 阶段三：刀路定义和结果分离

- 将现有 `CPathComponent` 的来源、配方和结果拆分。
- 特征、拾取和绘制统一生成 ToolpathDefinition。
- ToolpathResult 绑定 GeometryRevision。
- 失效状态和作业区准入检查。

### 阶段四：关联增强

- 特征签名。
- 几何签名。
- Split/Merge 路径迁移。
- 影响预览和人工重绑定。

## 14. 测试点

- 连续导入多个 STEP/IGES 后，每个文件拥有独立 Workpiece 和 GeometryRevision。
- BeginEdit 后修改 Draft 不改变 Current BRep、Topology 和 ToolpathResult。
- Discard 后 Current 和已有刀路保持不变。
- Commit 后旧 Current 进入 Historical，新版本成为 Current。
- 缝合产生的 Modified/Split/Merge 历史能够持久化为内核无关资源。
- 拓扑历史唯一映射时，刀路定义在新版本上重新计算。
- 多候选映射不得自动选择，结果状态为 Ambiguous。
- 来源删除后结果状态为 Orphaned，作业区拒绝使用。
- CAD 修改提交前可以预览受影响刀路数量和状态。
- 保存重开后 Current、Historical、Draft 清理状态、刀路定义和结果关系可恢复。
