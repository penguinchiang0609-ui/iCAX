# 02 CAM 核心数据结构详细设计

## 1. 模块定位

本模块定义三维线条切割 CAM 的核心数据结构。核心数据分为 Database Component、Resource 和运行时缓存三类。

原则：

- 小而稳定、需要撤销还原的数据进入 Database。
- 大对象、二进制和可复原资源进入 Resources。
- 几何内核对象、渲染对象、碰撞世界对象只能作为运行时缓存。

## 2. 通用单位、世界坐标与精度约定

这些约定不是独立功能模块，而是所有 CAM 数据结构和功能模块共同遵守的基础契约。

单位：

- 长度单位为毫米。
- 角度单位为度。
- 时间单位为秒。
- 所有持久化数据、命令参数和 UI 显示默认使用上述单位。

世界坐标规则：

- 除切割头机械结构内部数据外，Database、Resources、PDO 和命令参数中的 CAM 业务数据统一使用世界坐标系。
- 目标工件导入时将源 CAD 几何转换到世界坐标系；导入完成后，刀路、姿态场、碰撞、仿真都不再暴露源模型局部坐标。
- 刀路曲线、五轴姿态场、运动规划目标姿态和仿真状态都按世界坐标表达。
- TCP、关节坐标、切割头子部件局部坐标只属于切割头机械模型模块。
- 机床坐标系、工件零点等只影响后处理和输出转换，不进入当前 CAM 主数据结构。

默认几何误差目标：

```text
0.001 mm
```

适用范围：

- edge/loop 转刀路曲线。
- 曲线拟合。
- 姿态场采样的几何位置误差。
- 运动规划采样的目标位置误差。

不适用范围：

- 渲染网格视觉误差。
- 碰撞安全距离。
- 工艺参数误差。
- 真实机床控制精度。

## 3. ProjectContext 设置扩展

项目级 setting 不应设计成 Repository 里的 Entity。

原因：

- setting 是项目上下文参数，不是用户在画面中反复选择、增删、排序的业务对象。
- setting 的生命周期与 ProjectContext 一致，不需要独立 Entity 生命周期。
- 所有项目级 setting 都应从 ProjectContext 的统一 settings 入口访问。
- 通用 setting 和项目级扩展 setting 只是统一 settings 内部的命名空间，不是两个使用入口。
- 产品级应用参数属于 ProductContext / ProductData，不属于 ProjectContext。
- Repository 应聚焦业务对象，如工件、切割头、图层、刀路、顺序计划和安全检查结果。

ProjectContext 应提供统一 settings 入口：

```text
ProjectContext
  ProjectID
  ProjectName
  ProjectPath
  Settings
  Repository
  Resources
  PDOHub
  FacadeChannel
ServiceProvider
```

Settings 内部结构：

```text
ProjectContext.Settings
  PropertyBag
    key -> Variant
    path/key -> Variant
```

目标接口形态：

```text
ProjectContext.Settings()
ProjectContext.Settings().Set(key, value)
ProjectContext.Settings().Set(key, path, value)
ProjectContext.Settings().Get(key, defaultValue)
ProjectContext.Settings().Get(key, path, defaultValue)
```

对上层调用来说，项目级 setting 的唯一入口是 `ProjectContext.Settings()`。framework 不知道 Laser3DCAM 的具体字段，只负责保存通用数据。

扩展规则：

- framework 只定义统一 project settings 容器，不写入 Laser3DCAM 专属字段。
- Laser3DCAM 可以在 `Settings()` 中保存需要跟随项目文件走的项目级参数。
- Laser3DCAM 的产品级应用参数写入 ProductContext / ProductData，不写入 ProjectContext。
- 如果某个数据会作为业务对象被选择、编辑、删除或参与撤销还原，应进入 Repository，而不是 Settings。
- 如果某个数据只是项目打开后的上下文参数，应进入 ProjectContext settings，而不是 Entity。

## 4. Repository 对象粒度

建议作为 Repository 独立对象管理的类型：

```text
WorkpieceEntity
ToolAssemblyEntity
CuttingLayerEntity
VisibleLayerEntity
BlockEntity
PathEntity
OrderPlanEntity
SafetyCheckEntity
SimulationEntity
```

判断标准：

- 有独立身份。
- 有独立生命周期。
- 会被用户单独选择、增删、排序或引用。
- 会参与撤销还原。
- 需要独立版本或状态。

Root 不作为独立业务 Entity。RootComponent 直接挂在 Repository 的 MetaEntity 上：

```text
MetaEntity
  CLaserCamRootComponent
```

RootComponent 只保存当前主对象引用，不保存业务集合：

```text
CLaserCamRootComponent
  ActiveWorkpieceID
  ActiveToolAssemblyID
  DefaultCuttingLayerID
  DefaultVisibleLayerID
  ActiveOrderPlanID
  LatestSafetyCheckID
  ActiveSimulationID
  ProgramRootBlockID
```

## 5. Workpiece

一个项目可以包含多个 Workpiece Entity。RootComponent 不保存工件集合，只保存 `ActiveWorkpieceID`；所有工件通过 Repository 中的 `CLaserWorkpieceComponent` 查询。新增刀路时，`CCAMPathComponent.WorkpieceEntityID` 指向当时的当前工件。

```text
WorkpieceEntity
  CLaserWorkpieceComponent

CLaserWorkpieceComponent
  Name
  SourcePath
  ModelResourceID
  BRepResourceID
  TopologyResourceID
  DisplayResourceID
  TopologyVersion
```

Database 只保存资源引用和变换，不保存 BRep 指针。`ModelResourceID` 指向原始 CAD 文件资源，`BRepResourceID` 指向 `GeometryData::BRepModel` 中立几何资源。`DisplayResourceID` 是可选显示对象标识，真实显示数据由 render 插件或 PDO 渲染服务管理。

## 6. ToolAssembly

```text
CLaserCamToolAssemblyComponent
  ToolID
  ToolName
  ToolDescriptionResourceID
  ToolAssemblyResourceID
  ToolAssemblyToProjectTransform
  ActiveTCPID
  Version
```

`ToolAssemblyResourceID` 指向解析后的内部统一结构，便于快速打开项目。

## 7. CuttingLayer 与 VisibleLayer

```text
CuttingLayerEntity
  CLaserCamCuttingLayerComponent

CLaserCamCuttingLayerComponent
  Name
  CuttingProcessID
  CuttingProcessName
  Enabled
  OutputOrder
```

CuttingLayer 是给切割系统使用的加工工艺分组。不同 CuttingLayer 可以对应切割系统中的不同加工工艺，如功率、速度、气体、焦点等。当前项目只保存切割系统可识别的工艺 ID 和名称，不在 iCAX 内展开具体工艺参数。

```text
VisibleLayerEntity
  CLaserCamVisibleLayerComponent

CLaserCamVisibleLayerComponent
  Name
  Color
  Visible
  Locked
  Order
```

VisibleLayer 只服务前端显示、隐藏、颜色和选择过滤，不表达切割系统工艺。

## 8. ProgramNode / Block / Toolpath

加工程序按一棵树表达。Block 是容器节点，Toolpath 是运动叶子节点。二者都继承共同的程序节点字段：

```text
CCAMProgramNodeComponent
  Name
  PreInstructions[]
  PostInstructions[]
```

共同字段的含义：

- `PreInstructions[]`：节点执行前按数组顺序执行的指令。
- `PostInstructions[]`：节点执行后按数组顺序执行的指令。

`CamInstruction` 是指令的结构化表达：

```text
CamInstruction
  Type        // Comment / RawNC / MCode / GCode / Process / Dwell / Custom
  Code        // 指令码或产品内部动作码，如 M03、G04、LaserOn
  Text        // 注释文本或原始 NC 文本
  Parameters  // ObjectMap，保存功率、延时、气体等结构化参数
  Enabled
```

说明：

- 注释不单独占字段，统一表达为 `Type=Comment` 的 CamInstruction。
- 前置或后置由所在数组表达，CamInstruction 内部不重复保存 stage。
- 指令执行顺序由数组顺序表达，CamInstruction 内部不保存 order。
- `Parameters` 只保存指令参数，不回指 Entity、Resource 或运行时服务。

RootComponent 通过 `ProgramRootBlockID` 指向根 Block。一个完整加工文件就是根 Block：

```text
ProgramRootBlockEntity
  CCAMBlockComponent

CCAMBlockComponent : CCAMProgramNodeComponent
  Children[]
```

`Children[]` 是有序数组，每个元素只保存两项：

```text
ProgramChild
  Kind      // block 或 path
  EntityID  // 子 Block 或 Toolpath 的 EntityID
```

执行语义：

1. 按顺序执行 Block 的 `PreInstructions[]`。
2. 按 `Children[]` 顺序执行子 Block 或 Toolpath。
3. 按顺序执行 Block 的 `PostInstructions[]`。

Toolpath 是真正的运动节点：

```text
PathEntity
  CCAMPathComponent

CCAMPathComponent : CCAMProgramNodeComponent
  WorkpieceEntityID
  CuttingLayerID
  VisibleLayerID
  PathKind
  TopologyKind
  TopologyID
  Source
  Operation
  Role
  CurveResourceID
  PoseFieldResourceID
```

刀路主身份是 Path EntityID。当前阶段会把来源拓扑记录到组件和曲线资源中；真实空间曲线生成后，刀路仍然以 `CurveResourceID` 指向的曲线资源为权威几何，以 `PoseFieldResourceID` 指向姿态场资源。曲线资源和姿态场资源都统一使用世界坐标。`CuttingLayerID` 决定切割系统工艺分组，`VisibleLayerID` 决定前端显示分组。刀路顺序不写在 Path 自身，而由所属 Block 的 `Children[]` 决定。

## 9. PathCurveResource

```text
PathCurveResource
  Version
  TopologyKind
  TopologyID
  SourceTopology
  Curve: CompositeCurve3
```

`CompositeCurve3` 来自 `GeometryData`，只保存数据：

```text
CompositeCurve3
  Closed
  Segments[]

CurveSegment3
  Curve(Line3/Ray3/Segment3/Circle3/Arc3/Ellipse3/EllipseArc3/Polyline3/Bezier3/BSpline3/NURBS3/Clothoid3)
  Range
```

曲线资源是刀路的权威几何数据。资源本身只保存曲线数据和可选来源拓扑快照，不反向保存所属 Workpiece 或 Path。工件归属由 `CCAMPathComponent.WorkpieceEntityID` 表达，刀路到曲线资源的引用由 `CCAMPathComponent.CurveResourceID` 表达。所有点、向量和曲线段参数都使用世界坐标，不使用模型局部坐标、渲染坐标或切割头局部坐标。

## 10. PoseFieldResource

```text
PoseFieldResource
  Version
  Sample[]
```

```text
PoseSample
  SegmentIndex
  SegmentU
  BeamDirection
```

姿态场资源只保存姿态场本体，不反向保存 ToolpathID、CurveResourceID、CurveVersion 或 PoseFieldID。资源身份由 ResourcePool key 表达，归属关系由 `CCAMPathComponent.PoseFieldResourceID` 表达，曲线与姿态场的对应关系由同一个 `CCAMPathComponent` 同时持有 `CurveResourceID` 和 `PoseFieldResourceID` 来维护。

采样点使用 `SegmentIndex + SegmentU` 绑定复合曲线中的某一段及该段原生参数，不使用整条刀路归一化参数，也不使用累计弧长作为主定位。`BeamDirection` 是世界坐标下的激光束方向。采样点位置不持久化在 PoseFieldResource 中，而是通过 `PathCurveResource.Curve.Segments[SegmentIndex]` 按 `SegmentU` 求值得到。姿态场不保存 TCP 字段、完整机械姿态或关节值；TCP、关节链和逆解结果属于切割头机械模型与运动规划阶段。

## 11. MotionPlanResource

```text
MotionPlanResource
  Version
  LeadInSelection
  CuttingDirection
  CuttingSamples[]
  AirMoveSegments[]
  FrogJumpSegments[]
  IKCheckResult
  SingularityCheckResult
  JointLimitCheckResult
  ContinuityCheckResult
  CollisionCheckResult
```

MotionPlanResource 同样是纯数据资源，不保存自己的 ResourceID，也不反向保存 ToolpathID。后续由拥有者组件保存 `MotionPlanResourceID`，资源内部只保存规划结果本体。若运动样本需要定位来源刀路，优先使用资源内部的路径序号、段序号或样本序号，不直接回指 Repository Entity。

## 12. SafetyCheckResult

```text
SafetyCheckResult
  Passed
  IKFailure[]
  SingularityRisk[]
  JointLimitViolation[]
  MotionDiscontinuity[]
  CollisionEvent[]
```

## 13. 资源基类

所有资源建议统一元信息：

```text
ResourceInfo
  ResourceID
  ResourceType
  OriginalPath
  ContentHash
  BinaryContent
  Metadata
  Version
```

## 14. 修改规则

- Add/Remove/Modify Database 数据必须经过 Repository。
- ModifyFilter 负责合法性检查。
- Observer 只做通知，不做拦截。
- ResourceID 被 Database 引用前，资源必须已写入 ResourcePool。
- 删除被引用资源前必须先解除 Database 引用。

## 15. 测试点

- 创建项目后 ProjectContext 存在统一 settings 入口。
- 创建项目后 Repository 中存在默认 CuttingLayer 和默认 VisibleLayer。
- 导入模型后 Workpiece 只保存 ResourceID 和 Transform。
- 生成刀路后 Path entity 引用曲线资源。
- 删除 CuttingLayer 或 VisibleLayer 前必须处理其下刀路归属。
- 保存重开后 ResourceID 可解析到内嵌资源。
