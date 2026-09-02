# CAD 与 CSG 原语 EC 详细设计

> 状态：CAD 原语领域设计基线。
>
> 本文细化《01-EC原语模型总体设计》中 CAD 原语部分。若两份文档在 CAD 原语、识别生命周期或求值 BRep 的持久化策略上存在差异，以本文为准。

## 1. 核心结论

TubeOne 的权威 CAD 模型不是 BRep，也不是一个序列化 CSG 大对象，而是当前 Scene Repository 中一组具有稳定身份的 Entity 和正交 Component。

```text
Workpiece Entity
  + Stock Body Primitive Entity
  + Section Profile Primitive Entities
  + Ordered CAD Operator Entities
  + CAD Relation Entities
  + Semantic Result Entities
```

系统可以临时把这些 Entity 编译成 CSG 求值表达式，但不得持久化第二棵与 Entity 重复的 CSG 树。

必须同时满足：

1. 导入成功时必然存在一套精确可求值的 CAD 原语。
2. 完整参数化识别不阻塞导入，可以在后台逐步提升。
3. 主体、每个截面轮廓、每个腔体和每个刀具都能独立选择与编辑。
4. 所有实体刀具在用户模型中平铺，顺序可调。
5. 坡口等构造依赖使用 Relation Entity，不依靠树形嵌套。
6. 不能参数化的局部由 Composite/Residual 原语精确兜底。
7. BRep Face 不是稳定身份；语义结果区域必须 Entity 化。
8. 完整求值 BRep 是可清理缓存，不进入项目权威数据。

## 2. 术语

### 2.1 CAD 原语

用户能够理解、选择、修改、删除、排序或被其他原语引用的最小 CAD 构造单元。

### 2.2 Stock Body

管材零件的标准拉伸主体。它由一个二维材料区域、拉伸坐标架和有限长度定义。二维材料区域可以有任意数量腔体。

### 2.3 CAD Operator

作用于当前材料的几何生成体。它由“生成什么几何”和“如何作用于材料”两组 Component 组合而成。本文仍可在 UI 中称其为“CAD 刀具”，但它不是物理刀具。

### 2.4 Semantic Result

CAD 求值产生的稳定语义结果，例如 ResultBoundary、FeatureOpening 和 MaterialSpan。它们是下游制造目标的引用对象，但不是新的构造原语。

### 2.5 Evaluation Artifact

从 CAD 原语计算出的完整 BRep、拓扑映射、渲染网格和碰撞几何。它们可重建，不是权威 CAD 数据。

## 3. EC 建模规则

### 3.1 禁止中心化对象

不得建立以下权威对象：

```text
PartCadDocument
CadNode[]
SerializedCSGTree
GeometryFeatureVariant[]
```

交换、调试和算法内部可以构造快照，但快照不能成为编辑、持久化和 View 的权威数据源。

### 3.2 禁止 Kind 中心分派

不得用：

```cpp
CCadPrimitiveComponent {
    string Kind;
    ObjectMap Parameters;
}
```

正确表达：

```text
CCadOperatorComponent
CSubtractMaterialComponent
CExtrudedRegionComponent
CThroughExtentComponent
```

System 使用 EntityView 查询所需 Component 组合。增加新原语时注册新的 Component 和求值器，不修改一个全局 `switch(Kind)`。

### 3.3 Entity 化边界

以下对象必须 Entity 化：

- Workpiece。
- Stock Body。
- 每一个二维 Section Profile。
- 每一个 Side Atlas。
- 每一个 CAD Operator。
- 每一个 MaterialSpan。
- 每一个 FeatureOpening。
- 每一个 ResultBoundary。
- 每一个 AlternativeGroup 和 InterpretationCandidate。
- 每一条需要重复、排序或带角色的构造关系。
- 每一个识别任务和求值任务。

以下内容不逐项 Entity 化：

- NURBS 控制点、节点和权重。
- BRep 的瞬时 Vertex/Edge/Face。
- 网格顶点和三角形。
- 识别采样点。

这些高密度数据进入 Resource。

### 3.4 关系表达规则

- 不定义通用 `COwnedByComponent`、`CParentComponent` 或通用领域层级树。
- 一种关系属于哪个业务切面，就由该切面的 Component 保存语义明确的 Entity 引用。
- 空间父子关系属于 `CTransformComponent`，使用其 `ParentEntityID` 和 `ChildEntityIDs`。
- StockBody 与 Workpiece 的关系放在 `CStockBodyComponent.WorkpieceEntityID`。
- SectionProfile 与生成原语的关系放在 `CSectionProfileComponent.RegionGeneratorEntityID`。
- CadOperator 与 Workpiece 的关系放在 `CCadOperatorComponent.WorkpieceEntityID`。
- Wrapped、坡口、动态范围等依赖分别放在各自的几何或约束 Component 中。
- 一个候选可以包含多个 CAD Operator：每个成员 Operator 使用单值 CandidateMembership Component。
- System 和 EntityView 沿具体字段建立依赖，不通过含糊的 Owner 字段猜测关系类型。

## 4. Entity 总体结构

```text
Workpiece
├─ StockBody
│  ├─ OuterProfile
│  ├─ CavityProfile × N
│  └─ SideAtlas × (1 + N)
├─ CadOperator × N
│  └─ SectionProfile × M（拉伸或渐缩刀具需要）
├─ MaterialSpan × N
├─ FeatureOpening × N
├─ ResultBoundary × N
├─ AlternativeGroup × N
│  └─ InterpretationCandidate × N
└─ Recognition/Evaluation Task Entities
```

上图只是领域结构示意，不是一棵持久化通用树。每条连线都由对应业务 Component 中的具体引用字段表达。

## 5. 通用 CAD Component

### 5.1 原语身份

```cpp
class CCadPrimitiveComponent {
    bool Enabled = true;
    uint64 Revision = 1;
};
```

拥有该 Component 的 Entity 是可编辑 CAD 原语。Entity ID 是原语的稳定 Feature ID。

### 5.2 关系不进入通用 Component

`CCadPrimitiveComponent` 不保存 Parent、Owner 或 Workpiece。关系由同时挂在该 Entity 上的业务 Component 定义。例如同一个 Entity 的：

```text
CStockBodyComponent.WorkpieceEntityID
CCadOperatorComponent.WorkpieceEntityID
CSectionProfileComponent.RegionGeneratorEntityID
CInterpretationCandidateComponent.AlternativeGroupEntityID
```

这些字段名称明确说明关系含义，并可直接用于 `EntityWhere.Reference`。

### 5.3 顺序

```cpp
class CCadOrderComponent {
    int64 OrderKey;
};
```

StockBody 没有顺序。顶层实体刀具按 `OrderKey` 求值；SurfaceImprint 独立按顺序组织，不参与实体布尔。

移动刀具顺序必须通过依赖校验。若移动结果违反具体 Component 引用形成的依赖，命令整体失败，不允许 System 静默改变用户顺序。

### 5.4 名称

```cpp
class CDisplayNameComponent {
    string Name;
};
```

名称只用于 UI，不进入几何求值和身份匹配。

### 5.5 参数状态

```cpp
class CParameterStateComponent {
    string ParameterReportResourceID;
};
```

参数报告记录每个参数的：

```text
Observed
Reconstructed
Underconstrained
Minimum / Maximum
Unit
DisplayName
EditorKind
```

参数值仍位于具体几何 Component 中，报告不保存第二份参数值。

### 5.6 变换

CAD 原语直接复用现有 `CTransformComponent`：

```text
CTransformComponent
  ParentEntityID
  ChildEntityIDs
  Position / Yaw / Pitch / Roll / Scale
  LocalToWorldMatrix（Derived）
```

空间父子关系属于 Transform 切面，因此只由 Transform Component 表达；CAD 不再增加 `CCadPlacementComponent` 或通用 Parent Component。导入后工件拉伸轴已经统一为 Y，不保存 ImportTransform。

Workpiece 可以拥有根 `CTransformComponent`；StockBody 和需要空间定位的 CadOperator 通过自己的 `CTransformComponent.ParentEntityID` 接入该空间树，并由现有 Transform 服务维护双方 Parent/Child 一致性。

Transform 父子关系只代表空间变换链，不隐含 CAD 归属。例如 CadOperator 属于哪个工件仍以 `CCadOperatorComponent.WorkpieceEntityID` 为准，System 不从 Transform Parent 反推业务关系。

## 6. Stock Body 原语

### 6.1 StockBody Entity

```text
Entity StockBody
  CCadPrimitiveComponent
  CStockBodyComponent {
      uuid WorkpieceEntityID;
  }
  CExtrudedRegionComponent
  CTransformComponent
```

每个 Workpiece 当前必须且只能有一个启用的 `CStockBodyComponent`。

### 6.2 拉伸坐标约定

```cpp
class CExtrudedRegionComponent {
    double First;
    double Last;
};
```

`CTransformComponent.LocalToWorldMatrix` 定义当前原语的空间坐标架：

- 局部 Y 为拉伸方向。
- Section Profile 位于局部 XZ 平面。
- `First < Last`，单位为项目长度单位。
- 实体区域由该 Entity 拥有的全部 SectionProfile Entity 组成。

同一个 `CExtrudedRegionComponent` 同时用于 StockBody 和任意等截面拉伸刀具。

### 6.3 SectionProfile Entity

```text
Entity SectionProfile
  CCadPrimitiveComponent
  CSectionProfileComponent {
      uuid RegionGeneratorEntityID;
      string ExactCurveLoopResourceID;
  }
  CCadOrderComponent
  COuterProfileComponent
    或 CCavityProfileComponent
```

`ExactCurveLoopResourceID` 保存有向闭合二维曲线环。约定材料位于曲线行进方向左侧：

- Outer 通常逆时针。
- Cavity 通常顺时针。
- 求值器以方向和填充规则为准，不以“外壁/内壁”CAD 面类型为准。

一个 RegionGenerator 至少拥有一个 OuterProfile，可以拥有任意数量 CavityProfile。第一版 StockBody 只允许一个 OuterProfile；多个不连通 OuterProfile 应拆为多个实体工件或进入后续扩展。

### 6.4 截面参数化 Component

ExactCurveLoop 是精确权威几何；参数化 Component 是可编辑解释：

```cpp
class CCircleProfileComponent {
    Point2 Center;
    double Radius;
};

class CRectangleProfileComponent {
    Point2 Center;
    double Width;
    double Height;
    double RotationRadians;
};

class CGeneralCurveLoopComponent {
    string CurveLoopResourceID;
};
```

同一个 SectionProfile 只能拥有一个权威参数化 Component。无法参数化时使用 `CGeneralCurveLoopComponent`，不会因为不是圆或矩形而导入失败。

截面管型名称不成为 CAD 原语 Component。可配置截面匹配服务查询截面参数并给 Workpiece 添加分类摘要，不改变权威 SectionProfile。

### 6.5 SideAtlas Entity

每个 StockBody SectionProfile 生成一个稳定侧壁参数域：

```text
Entity SideAtlas
  CSideAtlasComponent {
      uuid WorkpieceEntityID;
      uuid StockBodyEntityID;
      uuid ExtrudedRegionEntityID;
      uuid SectionProfileEntityID;
      double UPeriod;
      double VFirst;
      double VLast;
  }
  CMinimumXThenZSeamComponent
```

- `u` 从稳定缝线开始，沿截面有向曲线按物理弧长增长。
- `v` 是沿拉伸 Y 方向的物理距离。
- 圆管、方管、NURBS 轮廓和腔体壁统一使用该参数域。
- SideAtlas Entity ID 是 WrappedVolume 和 SurfaceImprint 的稳定支撑引用。

SideAtlas 是派生语义 Entity，但身份必须持久化；采样网格和三维映射缓存可以重建。

## 7. CAD Operator 公共表达

### 7.1 Operator Entity

```text
Entity CadOperator
  CCadPrimitiveComponent
  CCadOperatorComponent {
      uuid WorkpieceEntityID;
  }
  CCadOrderComponent
  CTransformComponent（按需）
  一个材料作用 Component
  一个几何生成 Component
```

### 7.2 材料作用 Component

实体刀具必须且只能拥有以下一个 Component：

```text
CSubtractMaterialComponent   Pi = Pi-1 - Gi
CAddMaterialComponent        Pi = Pi-1 union Gi
CKeepIntersectionComponent   Pi = Pi-1 intersect Gi
```

零深度表面信息使用 `CSurfaceImprintOperatorComponent`，不与实体作用 Component 共存。

材料作用 Component 只描述几何布尔事实，不描述激光、钻削、铣削或打标。

### 7.3 平铺求值

```text
P0 = Evaluate(StockBody)

for operator in EnabledCadOperatorView ordered by CCadOrderComponent:
    G = EvaluateGenerator(operator)
    P = ApplyMaterialEffect(P, G, operator)

FinalPart = P
```

CSG 表达是这次求值的临时编译结果。Repository 中没有 `RootNodeID`、`Children[]` 或持久化 Boolean Tree。

对全部减材刀具，几何上可以优化为并集后一次差集；但优化器必须保持每个 Operator 的 GeneratedBy 来源和 ResultBoundary 身份，不能为了性能丢失特征历史。

## 8. 实体几何生成 Component

### 8.1 ExtrudedRegion

Entity 组合：

```text
CCadOperatorComponent
CSubtractMaterialComponent（或其他材料作用）
CExtrudedRegionComponent { First, Last }
CTransformComponent
SectionProfile Entities
```

可表达：

- 任意截面的直向或斜向贯穿体。
- 盲减材体。
- 完全位于材料内部的气泡体。
- 由固定方向生成的截断体。

圆柱只是 Circle SectionProfile 的 ExtrudedRegion，不是单独的圆孔类型。

### 8.2 TaperedRegion

```cpp
class CTaperedRegionComponent {
    double First;
    double Last;
    double Reference;
    Point2 ScaleCenter;
    double FirstScale;
    double LastScale;
};
```

Entity 同样拥有 SectionProfile Entities 和 Placement。任意参考截面沿构造方向线性一致缩放：

- 圆形截面得到锥体或锥台。
- 任意截面得到相似截面渐缩体。
- 不硬编码“锥孔”。

### 8.3 HalfSpace

```cpp
class CHalfSpaceComponent {
    Plane3 Boundary;
};

CKeepNegativeHalfSpaceComponent
  或 CKeepPositiveHalfSpaceComponent
```

HalfSpace 是最简单的截断生成体。它只适用于存在单一平面边界的情形；不同侧壁具有不同截断规律时使用其他生成原语。

### 8.4 WrappedVolume

```cpp
class CWrappedVolumeComponent {
    uuid SupportSideAtlasEntityID;
    string UVRegionResourceID;
    string LowerNormalOffsetFieldResourceID;
    string UpperNormalOffsetFieldResourceID;
};
```

数学定义：

```text
G = { S(u,v) + t*N(u,v) |
      (u,v) in UVRegion,
      Lower(u,v) <= t <= Upper(u,v) }
```

- `S` 和 `N` 由 SideAtlas 唯一导出，不在 Component 中另存方向场。
- Offset 是有符号物理距离场。
- 可表达圆管连续变化法向、方管分片侧壁和 UV 法向截断。
- 固定世界方向生成体必须使用 ExtrudedRegion，而不是滥用 WrappedVolume。

### 8.5 OpeningProfileSweep

```cpp
class COpeningProfileSweepComponent {
    uuid SourceOpeningEntityID;
    string ProfileFieldResourceID;
    string EvaluatedVolumeWitnessResourceID;
};

CSourceCutNormalFrameComponent
  或 CObservedLocalFrameComponent
```

ProfileField 以开口物理弧长为参数，保存局部法截面中的二维壁厚剖面。它可以表达：

- 整圈坡口。
- 任意局部坡口。
- 沿轮廓变化的变坡。
- X/K/J/U 形及非直线剖面。

角度、钝边和深度是派生属性，不是权威几何参数。

### 8.6 CompositeVolume

```cpp
class CCompositeVolumeComponent {
    string ExactBRepResourceID;
    string AnalyticPatchResourceID;
    string EditCapabilityResourceID;
};
```

用于自动拆解失败但仍能提取解析面、邻接关系和部分编辑能力的局部封闭体。它是权威 CAD 原语，因此其局部 BRep Resource 必须随项目保存。

### 8.7 ResidualBRep

```cpp
class CResidualBRepComponent {
    string ExactBRepResourceID;
};
```

用于完全无法参数化的精确局部兜底。Residual 必须满足：

- 是当前缺失或增加材料的一部分，而不是无边界的大型源模型引用。
- 与已识别原语共同求值后精确恢复导入零件。
- 后续识别提升时可以缩小或被参数化 Operator 替代。

不得因为存在 Residual 就把整个工件标记为导入失败。

## 9. 零深度 Surface Primitive

零深度信息不进入实体 CSG Boolean，但仍属于 CAD 原语。

### 9.1 UV Vector

```text
Entity SurfacePrimitive
  CCadPrimitiveComponent
  CCadOperatorComponent {
      uuid WorkpieceEntityID;
  }
  CSurfaceImprintOperatorComponent
  CUVVectorGeometryComponent {
      uuid SupportSideAtlasEntityID;
      string UVVectorResourceID;
  }
  CCadOrderComponent
```

### 9.2 UV Image

```cpp
class CUVImageGeometryComponent {
    uuid SupportSideAtlasEntityID;
    string ImageResourceID;
    string UVPlacementResourceID;
    double PhysicalWidth;
    double PhysicalHeight;
};
```

Surface Primitive 只说明内容位于表面。它不说明内容以后是激光标记、划线、喷印还是其他工艺。

深度大于零的浅刻必须建模为实体减材 Operator，不能继续使用 Surface Primitive。

## 10. 构造约束与关系

### 10.1 范围 Component

`CExtrudedRegionComponent.First/Last` 或 `CTaperedRegionComponent.First/Last` 始终保存当前有限求值范围。没有动态范围 Component 时，该有限范围就是权威定义。

需要随宿主变化的贯穿范围附加：

```cpp
class CThroughHostExtentComponent {
    uuid HostBodyEntityID;
    string WitnessResourceID;
};

CPositiveExtentDirectionComponent
  或 CNegativeExtentDirectionComponent
  或 CBidirectionalExtentComponent
```

从宿主边界开始的盲范围：

```cpp
class CBlindFromBoundaryExtentComponent {
    uuid HostBodyEntityID;
    uuid SourceOpeningEntityID;
    double Depth;
};
```

编辑主体尺寸后，动态范围 System 重新计算有限求值 First/Last，不要求用户手工拉长贯穿刀具。

### 10.2 几何穿越结果 Component

识别到的几何事实使用互斥 marker：

```text
CThroughMaterialComponent
CBlindMaterialComponent
CBubbleMaterialComponent
CUnresolvedMaterialExtentComponent
```

这些 Component 是识别结果，不代替动态范围约束。

### 10.3 材料角色 Component

```cpp
class CPenetrationRoleComponent {
    uuid HostBodyEntityID;
};

class CTruncationRoleComponent {
    uuid HostBodyEntityID;
};

class CBoundaryProfileModifierRoleComponent {
    uuid SourceOpeningEntityID;
};

CUnclassifiedMaterialRoleComponent
```

角色只用于理解、UI 和后续目标派生。几何求值仍由生成 Component 和材料作用 Component 决定。

### 10.4 从业务 Component 建立依赖图

不创建通用 `CadDependencyRelation Entity`。依赖图由具体 Component 的引用字段建立：

| Component | 依赖字段 |
|---|---|
| `CThroughHostExtentComponent` | `HostBodyEntityID` |
| `CBlindFromBoundaryExtentComponent` | `HostBodyEntityID`、`SourceOpeningEntityID` |
| `CWrappedVolumeComponent` | `SupportSideAtlasEntityID` |
| `COpeningProfileSweepComponent` | `SourceOpeningEntityID` |
| `CSideAtlasComponent` | `StockBodyEntityID`、`ExtrudedRegionEntityID`、`SectionProfileEntityID` |
| `CBoundaryProfileModifierRoleComponent` | `SourceOpeningEntityID` |
| `CTransformComponent` | `ParentEntityID`、`ChildEntityIDs` |

各 Component 注册自己哪些 UUID 字段属于硬依赖、软引用或空间父子关系。依赖校验 System 根据这些明确语义建立 DAG，不解析通用 RoleKey。

## 11. Semantic Result Entities

### 11.1 MaterialSpan

```cpp
Entity MaterialSpan
  CMaterialSpanComponent {
      uuid WorkpieceEntityID;
      uuid SourceOperatorEntityID;
      uuid HostBodyEntityID;
      uint32 TravelOrdinal;
      string EndpointWitnessResourceID;
  }
```

同一 CadOperator 可以拥有多个 MaterialSpan。MaterialSpan 不决定外壁、内壁或腔间隔板，也不决定加工方式。

### 11.2 FeatureOpening

```cpp
Entity FeatureOpening
  CFeatureOpeningComponent {
      uuid WorkpieceEntityID;
      uuid SourceOperatorEntityID;
      uuid HostBodyEntityID;
      string RoleKey;
      string BoundaryWitnessResourceID;
  }
```

Opening 是坡口、局部剖面和制造目标的稳定引用。

### 11.3 ResultBoundary

```cpp
Entity ResultBoundary
  CResultBoundaryComponent {
      uuid WorkpieceEntityID;
      uuid GeneratorEntityID;
      string OutputRoleKey;
      uint64 GeneratorRevision;
  }
```

持久化内容只有语义身份和来源。当前 BRep 碎面映射是可重建 Component：

```cpp
class CResultBoundaryGeometryComponent {
    uuid GeometryEvaluationEntityID;
    string FragmentMappingResourceID;
};
```

完整求值 BRep 缓存被清理时，可以移除 `CResultBoundaryGeometryComponent`，但不得删除 ResultBoundary Entity。

### 11.4 稳定输出身份

系统生成的 MaterialSpan、FeatureOpening、ResultBoundary 和 SideAtlas 使用以下键匹配重算前后的既有 Entity：

```text
GeneratorEntityID + OutputRoleKey
```

`OutputRoleKey` 来自构造语义，例如截面 Loop 身份、生成边界角色、材料旅行次序或开口角色，不得来自 OCC Face 枚举序号。

- 同一语义结果在新 BRep 中裂成多个碎面：保留一个结果 Entity，更新 Fragment Mapping。
- 一个语义结果真正拆成多个可独立制造区域：创建新的结果 Entity，并建立 Split Lineage。
- 结果消失：先添加 `COrphanedComponent` 并执行下游影响分析，再决定是否删除。
- 参数修改但输出角色仍存在：保留 Entity ID，只递增 Revision。

用户创建的 CadOperator 从创建命令开始持有稳定 Entity ID；参数修改、Apply、Undo 和 Redo 均不得重新分配身份。

## 12. Optional 多解释模型

### 12.1 AlternativeGroup

```text
Entity AlternativeGroup
  CCadPrimitiveComponent
  CAlternativeGroupComponent {
      uuid WorkpieceEntityID;
  }
  CCadOrderComponent
  CRecommendedCandidateReferenceComponent { CandidateEntityID }
  CSelectedCandidateReferenceComponent { CandidateEntityID } // 可以缺失
```

AlternativeGroup 占据顶层刀具序列中的一个位置。

### 12.2 InterpretationCandidate

```text
Entity InterpretationCandidate
  CInterpretationCandidateComponent {
      uuid AlternativeGroupEntityID;
      string EvaluationResourceID;
      string EvidenceResourceID;
  }
  CCadOrderComponent
  COptionalComponent
```

### 12.3 Candidate Membership

一个候选可以包含一项或多项平级 CadOperator。一个 Operator 最多属于一个候选，因此使用直接引用 Component：

```cpp
Entity CandidateMemberOperator
  CCadOperatorComponent
  CCandidateMembershipComponent {
      uuid CandidateEntityID;
  }
  CCadOrderComponent
```

候选成员的 `CCadOperatorComponent.WorkpieceEntityID` 仍指向 Workpiece，但不进入顶层正式求值序列；AlternativeGroup 自身占据顶层 `CCadOrderComponent` 位置。求值器只展开 Selected Candidate 的成员。

例如同一锥面局部可以有：

```text
Candidate A
  TaperedRegion

Candidate B
  ExtrudedRegion
  OpeningProfileSweep
```

只有 Selected Candidate 的成员参与正式求值。未选择时可以使用 Recommended Candidate 做预览，但正式 CAD 状态增加 `CNeedsReviewComponent`，依赖该局部的制造目标不能冒充已确认。

## 13. 导入后的识别生命周期

### 13.1 同步基础识别

导入提交前必须完成：

```text
临时源 BRep P
  -> 验证标准拉伸主体
  -> 识别 Y 拉伸轴
  -> 建立 StockBody 和 SectionProfile Entities
  -> 重建毛坯 B
  -> 计算精确差异 R = B - P
  -> 创建一个或多个 Composite/Residual CadOperator
  -> 验证 Evaluate(Body + Operators) 与 P 等价
```

只有验证成功才创建正式 Workpiece。此时即使没有参数化拆解，也已经存在精确可求值的 EC CAD 模型。

### 13.2 后台参数化提升

导入成功后自动创建任务 Entity：

```text
Entity CadRecognitionTask
  CCadRecognitionTaskComponent {
      uuid WorkpieceEntityID;
      uint64 SourceCadRevision;
      string RequestedCapability;
  }
  CRecognitionPendingComponent
```

状态通过 marker Component 转换：

```text
Pending -> Running -> Complete
                   -> NeedsReview
                   -> Partial
                   -> Failed
```

识别器使用 B、P、R 和当前证据，把 Residual 的子集提升为参数化 CadOperator。识别任务运行期间，其输入 BRep 必须被任务临时固定；也可以使用已经证明与 P 等价的当前 GeometryEvaluation BRep。候选组合必须通过对称差、误删和漏删校验。

打开 CAD 编辑器不是首次识别入口，只会提高当前 Workpiece 识别任务优先级并显示已有结果。

### 13.3 原子提升

```text
旧：Residual R

新：Extruded A + HalfSpace B + OpeningProfileSweep C + Residual R2
```

只有满足：

```text
Union(A, B, C, R2) 与 R 在容差内等价
```

才在一个 Repository Transaction 中提交新 Entity、Relation、Lineage 和剩余 Residual。失败时继续保留旧 R。

## 14. 求值任务与缓存

### 14.1 GeometryEvaluation Entity

```text
Entity GeometryEvaluation
  CGeometryEvaluationComponent {
      uuid WorkpieceEntityID;
      uint64 SourceCadRevision;
      string EvaluatorSignature;
  }
  CBRepGeometryComponent { ResourceID }
  CTopologyGeometryComponent { ResourceID }
  CRenderGeometryComponent { ResourceID }
  CDerivedCacheComponent
  CCurrentComponent
```

GeometryEvaluation 是运行期或本机缓存 Entity，不属于项目权威数据。

项目持久化器必须排除拥有 `CDerivedCacheComponent` 的 Entity，以及明确注册为 Derived 的 Component。缓存命中时重新物化这些 Entity；缓存未命中时由求值 System 重建。

### 14.2 三类资源

权威资源，随项目保存：

```text
Section Profile 精确二维曲线
WrappedVolume UV 区域和距离场
OpeningProfileSweep 剖面场
Surface Primitive 的矢量和图片
Composite/Residual 的局部精确 BRep
无法由 Component 字段恢复的用户编辑数据
```

派生缓存，默认不进入项目文件：

```text
当前完整求值 BRep
当前拓扑索引和碎面映射
渲染 Mesh
碰撞 Mesh
刀具预览 Mesh
```

临时资源，编辑 Scene 关闭后释放：

```text
导入时源 BRep
识别采样数据
未 Apply 的刀具灵体
失败求值产生的中间 Shape
```

缓存键至少包含：

```text
WorkpieceEntityID
CadRevision
EvaluatorSignature
ParameterFingerprint
```

### 14.3 源 BRep 释放条件

只有同时满足以下条件才能释放导入源 BRep：

1. 权威 CAD 原语及其必要局部 BRep 已写入资源池。
2. 从 CAD 原语重新求值的结果与源 BRep 通过几何等价验证。
3. 识别证据已转换成不依赖源 Shape 生命周期的语义 Entity、签名或持久化局部资源。
4. Repository Transaction 已提交成功。
5. 没有仍在运行的识别任务依赖该源 BRep，或任务已经切换到等价的 GeometryEvaluation BRep。

不保存原始方向和 ImportTransform。

## 15. 参数编辑与 Apply

### 15.1 实时预览

用户修改一个 CadOperator 参数时：

```text
修改编辑 Scene 中具体几何 Component
  -> PrimitivePreviewSystem 只重建该 Operator
  -> 更新半透明灵体缓存
  -> 正式零件 GeometryEvaluation 保持不变
```

### 15.2 Apply

```text
1. 校验 Component 组合和参数范围
2. 校验依赖 DAG 与 OrderKey
3. 查询 StockBody 和所有启用 Operator
4. 编译临时求值表达式
5. 生成候选完整 BRep
6. 验证 BRep 有效性和材料约束
7. 更新 ResultBoundary、Opening、MaterialSpan
8. 更新 CAD Revision
9. 写入新的本机 GeometryEvaluation 缓存
10. 发布 CAD ChangeSet
```

1 至 8 属于一次业务 Apply。第 9 步失败不回滚权威 CAD 数据，因为缓存可以稍后重建；但第 5 至 7 步所需的临时求值必须成功，才能证明本次参数修改有效。

### 15.3 Commit

从 Transient CAD Scene 提交到 Main Scene 时，仅提交：

- 新增、修改、删除的权威 Entity/Component。
- 必要的权威 Resource。
- ResultBoundary、Opening、MaterialSpan 的语义变化。
- Lineage 和制造目标影响状态。

不把完整求值 BRep 和预览 Mesh 打进项目保存事务。

## 16. View 定义

CAD 编辑产品 View 组合以下 EntityView：

```text
StockBodyView
  HAS CStockBodyComponent
  AND CStockBodyComponent.WorkpieceEntityID == :workpiece

SectionProfileView
  HAS CSectionProfileComponent
  AND Reference(CSectionProfileComponent.RegionGeneratorEntityID)
      属于当前 Workpiece 的 Body 或 Operator

CadOperatorView
  HAS CCadOperatorComponent
  AND CCadOperatorComponent.WorkpieceEntityID == :workpiece

SemanticResultView
  HAS CResultBoundaryComponent
   OR HAS CFeatureOpeningComponent
   OR HAS CMaterialSpanComponent

AlternativeView
  HAS CAlternativeGroupComponent
   OR HAS CInterpretationCandidateComponent
```

左侧“CAD 原语”只是 View 投影：

```text
主体
  外轮廓
  腔体

刀具
  按 CCadOrderComponent 排序的 CadOperator

候选解释
  只在存在 AlternativeGroup 时显示
```

添加或删除 Component 后，Entity 自动进入或退出相应 EntityView。前端不维护第二套历史树状态。

## 17. 不变量

### 17.1 Workpiece

- 恰好一个启用的 StockBody。
- 所有顶层 CadOperator 的 `CCadOperatorComponent.WorkpieceEntityID` 都指向当前 Workpiece。
- Workpiece 不保存原语 Entity ID 数组。

### 17.2 Section Region

- 至少一个 OuterProfile。
- 第一版最多一个 OuterProfile。
- 任意数量 CavityProfile。
- 每个 Profile 的精确曲线必须闭合且有向。
- Profile 参数化 Component 的求值必须与精确曲线在容差内一致。

### 17.3 CadOperator

- 实体 Operator 恰好一个材料作用 Component。
- 实体 Operator 恰好一个实体几何生成 Component。
- SurfaceImprint Operator 不拥有实体材料作用 Component。
- Enabled Operator 的全部硬依赖必须有效。
- OrderKey 必须满足依赖先后约束。

### 17.4 Alternative

- 每个 AlternativeGroup 至少两个 Candidate。
- 每个 Candidate 至少一个成员 Operator。
- 所有 Candidate 求值必须在容差内几何等价。
- Recommended 不等于 Selected。

### 17.5 精确性

- 基础导入模型必须精确恢复导入 P。
- 参数化提升后必须继续精确恢复提升前的局部结果。
- 无法证明精确时保留 Composite/Residual，不得为了树更漂亮而丢失几何。

## 18. 与旧中性模型的映射

| 旧 `TubeNeutralGeometry` 内容 | EC 模型 |
|---|---|
| `CTubeNeutralGeometry` | 不再作为权威对象 |
| `BaseNodeID` | `CStockBodyComponent` Entity |
| `RootNodeID` | 不保存；求值时从 View 编译 |
| `SSolidNode` | `CCadOperatorComponent` Entity |
| `SExtrudedRegionNode` | `CExtrudedRegionComponent` + SectionProfile Entities |
| `STaperedRegionNode` | `CTaperedRegionComponent` + SectionProfile Entities |
| `SHalfSpaceNode` | `CHalfSpaceComponent` + KeepSide marker |
| `STransformNode` | `CTransformComponent`，父子关系由 Transform 自己表达 |
| `SBooleanNode` | 不持久化；由材料作用和顺序临时编译 |
| `SWrappedVolumeNode` | `CWrappedVolumeComponent` |
| `SOpeningProfileSweepNode` | `COpeningProfileSweepComponent.SourceOpeningEntityID` |
| `SCompositeVolumeNode` | `CCompositeVolumeComponent` |
| `SResidualBRepNode` | `CResidualBRepComponent` |
| `SAlternativeNode` | AlternativeGroup/Candidate/Member Entities |
| `SSectionPrimitive` | SectionProfile Entity + 参数化 Component |
| `SSideAtlasDefinition` | SideAtlas Entity |
| `SMaterialSpan` | MaterialSpan Entity |
| `SFeatureOpeningRef` | FeatureOpening Entity |
| `SRemovalSemantics` | Through/Blind/Bubble marker Component |
| `SUVFeature` | SurfaceImprint Operator Entity |

旧资源可以作为识别算法的临时适配输出；适配器必须把结果实体化后才能进入正式 Repository。

## 19. 首批实现范围

第一批必须实现：

1. `CCadPrimitiveComponent`、各业务 Component 的明确引用、顺序和参数状态。
2. StockBody、SectionProfile、Outer/Cavity 和 SideAtlas Entities。
3. Subtract/Add/KeepIntersection 材料作用 Component。
4. ExtrudedRegion、TaperedRegion、HalfSpace、WrappedVolume。
5. OpeningProfileSweep、CompositeVolume、ResidualBRep。
6. SurfaceImprint 的 UV Vector 和 UV Image。
7. Alternative、Candidate 和 CandidateMembership Components。
8. MaterialSpan、FeatureOpening、ResultBoundary Entities。
9. 基础导入保底模型和后台识别任务 Entity。
10. 临时 CSG 编译、完整求值验证和 Apply。
11. GeometryEvaluation 派生缓存，不进入项目权威保存。
12. CAD View 和属性编辑所需 EntityView。

第二批再实现：

- 更多参数化 SectionProfile Component。
- 识别候选评价与人工提升。
- Split/Merge Lineage 的完整迁移。
- 更激进的布尔批处理优化。
- 用户自定义 CAD 原语插件和声明式参数面板。

## 20. 验收示例

### 20.1 普通圆管贯穿孔

```text
StockBody
  ExtrudedRegion
  Outer CircleProfile
  Cavity CircleProfile

CadOperator
  SubtractMaterial
  ExtrudedRegion
  Outer CircleProfile
  ThroughHostExtent
```

模型中不出现 `Hole`、`Drill` 或 `LaserCut` 类型。

### 20.2 双腔管贯穿

一个 CadOperator 产生多个 MaterialSpan 和多个 ResultBoundary，但仍保持一个 Operator Entity。

### 20.3 全圈变坡

基础贯穿 CadOperator 产生 FeatureOpening；OpeningProfileSweep CadOperator 通过 Dependency Relation 引用该 Opening，并使用周期 ProfileField。

### 20.4 无法拆解的局部

精确局部进入 CompositeVolume 或 ResidualBRep。保存重开后不依赖导入源文件，仍能精确求值完整零件。

### 20.5 清理本机缓存

删除完整 BRep、Topology 和 Render Mesh 缓存后：

- CAD 原语 Entity 不丢失。
- ResultBoundary 语义身份不丢失。
- 系统可以重新生成 GeometryEvaluation。
- 生成后 ResultBoundary 重新取得当前碎面映射。

以上条件全部满足，才说明 CSG 已经真正成为 EC 表达，而不是把旧 CSG 大对象换了一个存放位置。
