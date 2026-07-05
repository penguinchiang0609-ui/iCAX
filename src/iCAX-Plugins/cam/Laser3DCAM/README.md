# Laser3DCAM

`Laser3DCAM` 是三维线条切割 CAM 产品的后端插件。它不保存插件私有项目状态，而是向 Database 注册 CAM 组件，并向 CommandHandler 注册 `Cam.*` 项目命令。

当前插件只落地产品数据边界：

- 项目入口进入 Repository 的 MetaEntity：`CLaserCamRootComponent`。
- 当前主工件进入独立 Workpiece Entity：`CLaserWorkpieceComponent`。
- 切割系统工艺分组进入独立 CuttingLayer Entity：`CLaserCamCuttingLayerComponent`。
- 前端显示分组进入独立 VisibleLayer Entity：`CLaserCamVisibleLayerComponent`。
- 加工程序入口进入独立 Block Entity：`CCAMBlockComponent`，由 `Root.ProgramRootBlockID` 指向。
- 已确认刀路进入独立 Path Entity：`CCAMPathComponent`。
- `CCAMProgramNodeComponent` 是 Block 和 Path 的共同基类，统一提供 `Name`、`PreInstructions[]` 和 `PostInstructions[]`。
- Block 的 `Children` 字段维护子 Block 或 Path 的顺序，刀路自身不保存全局排序。
- 当前选择进入 MetaEntity 上的运行时选择组件：`CCAMSelectionComponent`。
- 导入模型、拓扑索引和显示数据的对象进入 Project.Resources。
- 刀路曲线以 `CCAMPathCurveResource` 进入 Project.Resources，当前阶段先记录来源拓扑和资源壳。
- 产品业务算法以 service 形式注册：`FeatureRecognitionService`、`ToolpathGenerationService`、`ToolpathOrderService`。
- 前端只能通过 `Cam.*` 命令读取和修改项目状态；CommandHandler 只做入口、上下文解析和提交，不承载算法。
- 插件不在 framework 中写入任何产品专属逻辑。

当前没有内置 STEP/OCC 解析器，也不再硬编码示例孔、边或面。`Cam.ImportModel` 只登记模型资源和空拓扑资源；后续模型导入/特征识别插件负责填充 `CCAMTopologyResource`。

目录结构：

- `ToolpathComponents.h`：toolpath root、workpiece、selection、program node、block、path 组件。
- `ToolpathResources.h`：toolpath 模型资源、拓扑资源、刀路曲线资源和姿态场资源的数据契约。资源结构体只保存数据，不提供行为，也不反向关联 Repository 或 Entity。
- `ToolpathResourceKeys.h`：CAM 资源 key 生成工具。
- `CCAMPoseFieldResource` 使用 `segmentIndex + segmentU` 绑定复合曲线采样位置，只保存世界坐标下的 `beamDirection`；位置由曲线资源按参数求值获得。
- `FeatureRecognitionService.h/.cpp`：从 BRep 模型和特征定义识别参数化特征。
- `ToolpathGenerationService.h/.cpp`：把参数化特征转换成可落库的刀路曲线数据。
- `ToolpathOrderService.h/.cpp`：根据 Block 和排序策略生成/应用加工顺序计划。
- `CamCommands.cpp`：项目级 `Cam` 命令目标，负责模型导入登记、拓扑拾取、识别结果入刀路实体、维护刀路实体。
- `Laser3DCAM.h/.cpp`：插件契约版本入口。
- `Laser3DCAM.vcxproj`：插件 DLL 工程。

命令：

- `Cam.GetScene`：返回项目 root、当前主工件、拓扑状态、选择、program 树和从 program 树展开的刀路列表。
- `Cam.ImportModel`：登记模型来源，创建 Workpiece Entity，并设置 `Root.ActiveWorkpieceID`。
- `Cam.PickTopology`：选择一个已存在的拓扑对象。
- `Cam.RecognizeLoops`：从拓扑资源中筛选 `role=hole/cut` 的 loop，创建 Path Entity，并追加到根 Block。
- `Cam.AddSelectionPath`：把当前选择创建为 Path Entity，并追加到根 Block。
- `Cam.SetPoseField`：为指定 Path 写入姿态场采样，采样点使用 `segmentIndex + segmentU`。
- `Cam.ClearProgram`：删除当前项目中的 Path Entity，清空根 Block 的 children。

Repository 数据形态：

```text
MetaEntity
  CLaserCamRootComponent
  CCAMSelectionComponent

WorkpieceEntity
  CLaserWorkpieceComponent

CuttingLayerEntity
  CLaserCamCuttingLayerComponent

VisibleLayerEntity
  CLaserCamVisibleLayerComponent

ProgramRootBlockEntity
  CCAMBlockComponent

PathEntity
  CCAMPathComponent
```

Program 树关系：

```text
CLaserCamRootComponent.ProgramRootBlockID -> ProgramRootBlockEntity

CCAMBlockComponent
  Name
  PreInstructions[]
  PostInstructions[]
  Children[]

CCAMPathComponent
  Name
  PreInstructions[]
  PostInstructions[]
  CuttingLayerID
  VisibleLayerID
  CurveResourceID
  PoseFieldResourceID
```

`PreInstructions[]` 和 `PostInstructions[]` 的元素是 `CamInstruction`：

```text
CamInstruction
  Type        // Comment / RawNC / MCode / GCode / Process / Dwell / Custom
  Code        // 指令码或产品内部动作码，如 M03、G04、LaserOn
  Text        // 注释文本或原始 NC 文本
  Parameters  // ObjectMap，保存功率、延时、气体等结构化参数
  Enabled
```

业务 service 主链路：

```text
BRepResource + FeatureDefinition[]
  -> FeatureRecognitionService
  -> RecognizedFeature[]
  -> ToolpathGenerationService
  -> GeneratedToolpath
  -> CommandHandler 写入 PathEntity + PathCurveResource + Block.Children[]
  -> ToolpathOrderService
  -> Block.Children[] 排序计划
```

`RecognizedFeature` 是参数化特征，例如孔可以表达为 path、depth、axis、radius、bevelType、bevelAngle 等。它不是 Entity，也不是 Resource 对 Data 的反向引用。
