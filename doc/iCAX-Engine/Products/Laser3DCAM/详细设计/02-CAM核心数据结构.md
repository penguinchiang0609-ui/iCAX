# 02 CAM 核心数据结构详细设计

## 1. 模块定位

本模块定义三维线条切割 CAM 的核心数据结构。核心数据分为 Database Component、Resource 和运行时缓存三类。

原则：

- 小而稳定、需要撤销还原的数据进入 Database。
- 大对象、二进制和可复原资源进入 Resources。
- 几何内核对象、渲染对象、碰撞世界对象只能作为运行时缓存。

## 2. 通用坐标、单位与精度约定

这些约定不是独立功能模块，而是所有 CAM 数据结构和功能模块共同遵守的基础契约。

单位：

- 长度单位为毫米。
- 角度单位为度。
- 时间单位为秒。
- 所有持久化数据、命令参数和 UI 显示默认使用上述单位。

核心坐标系：

```text
ProjectCS        项目公共基准坐标系
ModelCS          目标工件 CAD 原始坐标系
ToolAssemblyCS   切割头装配根坐标系
ComponentCS      切割头子部件局部坐标系
JointCS          关节坐标系
TCPCS            TCP 坐标系，Z 轴为激光束方向
RenderCS         前端渲染坐标系
```

坐标使用规则：

- 刀路曲线数据持久化在 `ProjectCS`。
- 五轴姿态场中的 TCP 位姿持久化在 `ProjectCS`。
- 目标工件导入模块负责建立 `ModelCS -> ProjectCS`。
- 切割头机械模型模块负责维护 `ComponentCS -> ParentComponentCS -> ToolAssemblyCS -> ProjectCS`。
- 运动规划模块使用 `ProjectCS` 中的 TCP 位姿求解切割头关节值。
- 渲染模块可以把 `ProjectCS` 转换为 `RenderCS`，但不得把渲染坐标反向作为 CAM 计算依据。

默认几何误差目标：

```text
0.001 mm
```

适用范围：

- edge/loop 转刀路曲线。
- 曲线拟合。
- 姿态场采样的几何位置误差。
- 运动规划采样的 TCP 位置误差。

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
  MailChannel
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
LayerEntity
ToolpathEntity
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

## 5. Workpiece

```text
CLaserCamWorkpieceComponent
  WorkpieceID
  Name
  SourcePath
  CadModelResourceID
  DisplayMeshResourceID
  CollisionGeometryResourceID
  ModelToProjectTransform
  Version
```

Database 只保存资源引用和变换，不保存 BRep 指针。

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

## 7. Layer

```text
CLaserCamLayerComponent
  LayerID
  Name
  Color
  Visible
  LockState
  Order
```

图层只组织刀路，不改变刀路几何。

## 8. Toolpath

```text
CLaserCamToolpathComponent
  ToolpathID
  Name
  LayerID
  CurveResourceID
  QualitySummary
  Process
  State
  DefaultLeadInParameter
  DefaultDirection
  Version
  OptionalSourceInfo
```

刀路主身份是 `ToolpathID`。CAD 拓扑来源只是可选信息。

## 9. ToolpathCurveResource

```text
ToolpathCurveResource
  CurveID
  IsClosed
  Segment[]
  Version
```

曲线段：

```text
LineSegment3
ArcSegment3
PolylineSegment3
EllipseSegment3
CubicBSplineSegment3
BezierSegment3
```

曲线资源是刀路的权威几何数据。

## 10. PoseFieldResource

```text
PoseFieldResource
  PoseFieldID
  ToolpathID
  Version
  Sample[]
```

```text
PoseSample
  CurveParameter
  TCPPositionInProjectCS
  TCPPoseInProjectCS
  BeamDirectionInProjectCS
  OptionalJointValues
```

## 11. MotionPlanResource

```text
MotionPlanResource
  MotionPlanID
  ToolpathID
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
- 创建项目后 Repository 中存在默认 Layer。
- 导入模型后 Workpiece 只保存 ResourceID 和 Transform。
- 生成刀路后 Toolpath entity 引用曲线资源。
- 删除图层前必须处理其下刀路归属。
- 保存重开后 ResourceID 可解析到内嵌资源。
