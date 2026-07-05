# Laser3DCAM

`Laser3DCAM` 是三维线条切割 CAM 产品的后端插件。它不保存插件私有项目状态，而是向 Database 注册 CAM 组件，并向 CommandHandler 注册 `Cam.*` 项目命令。

当前插件只落地产品数据边界：

- 项目入口进入 Repository 的 MetaEntity：`CLaserCamRootComponent`。
- 每个导入工件进入独立 Workpiece Entity：`CLaserWorkpieceComponent`，`Root.ActiveWorkpieceID` 只表示当前激活工件。
- 切割系统工艺分组进入独立 CuttingLayer Entity：`CLaserCamCuttingLayerComponent`。
- 前端显示分组进入独立 VisibleLayer Entity：`CLaserCamVisibleLayerComponent`。
- 加工程序入口进入独立 Block Entity：`CCAMBlockComponent`，由 `Root.ProgramRootBlockID` 指向。
- 已确认刀路进入独立 Path Entity：`CCAMPathComponent`。
- `CCAMProgramNodeComponent` 是 Block 和 Path 的共同基类，统一提供 `Name`、`PreInstructions[]` 和 `PostInstructions[]`。
- Block 的 `Children` 字段维护子 Block 或 Path 的顺序，刀路自身不保存全局排序。
- 当前选择进入 MetaEntity 上的运行时选择组件：`CCAMSelectionComponent`。
- 导入模型通过 `Scene.Resources().Import()` 进入通用资源导入机制：外部 CAD 文件由资源导入插件转换成 `source` 与 `geometry.brep`，CAM 插件再基于 BRep 生成自己的拓扑索引资源。
- 显示数据由 render 插件或 PDO 渲染服务管理；CAM 只保存可选显示对象 ID。
- 刀路曲线以 `CCAMPathCurveResource` 进入 Scene.Resources，当前阶段先记录来源拓扑和资源壳。
- 产品业务算法以 service 形式注册：`FeatureRecognitionService`、`ToolpathGenerationService`、`ToolpathOrderService`。
- 前端只能通过 `Cam.*` 命令读取和修改项目状态；CommandHandler 只做入口、上下文解析和提交，不承载算法。
- 插件不在 framework 中写入任何产品专属逻辑。

STEP/STP、IGS/IGES 的具体导入由 `iCAX-Plugins/cad/OpenCascadeResourceImport` 提供。该插件通过 OCCT 8.0.0-p1 把 CAD 文件转换成 `iCAX::Resource::CBinaryResource` 和中立 `GeometryData::BRepModel`。Laser3DCAM 不直接依赖 OCCT；后续如果替换为 CGAL 或其他内核，只需要替换资源导入插件。

目录结构：

- `ToolpathComponents.h`：toolpath root、workpiece、selection、program node、block、path 组件。
- `ToolpathResources.h`：CAM 拓扑资源、刀路曲线资源和姿态场资源的数据契约。资源结构体只保存数据，不提供行为，也不反向关联 Repository 或 Entity。
- `ToolpathResourceKeys.h`：CAM 资源 key 生成工具。
- `CCAMPoseFieldResource` 使用 `segmentIndex + segmentU` 绑定复合曲线采样位置，只保存世界坐标下的 `beamDirection`；位置由曲线资源按参数求值获得。
- `FeatureRecognitionService.h/.cpp`：从 BRep 模型和特征定义识别参数化特征。
- `ToolpathGenerationService.h/.cpp`：把参数化特征转换成可落库的刀路曲线数据。
- `ToolpathOrderService.h/.cpp`：根据 Block 和排序策略生成/应用加工顺序计划。
- `CamCommands.cpp`：Scene 级 `Cam` 命令目标，负责调用资源导入、生成 CAM 拓扑、拓扑拾取、识别结果入刀路实体、维护刀路实体。
- `Laser3DCAM.h/.cpp`：插件契约版本入口。
- `Laser3DCAM.vcxproj`：插件 DLL 工程，不直接链接具体 CAD 内核。

命令：

- `Cam.GetScene`：返回项目 root、工件列表、当前激活工件、拓扑状态、选择、program 树和从 program 树展开的刀路列表。
- `Cam.ImportModel`：调用 `Scene.Resources().Import()` 导入资源，生成 CAM 拓扑资源，追加 Workpiece Entity，并设置 `Root.ActiveWorkpieceID`。
- `Cam.SetActiveWorkpiece`：切换当前激活工件，并清空当前拓扑选择。
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
Workpiece.BRepResourceID + FeatureDefinition[]
  -> FeatureRecognitionService
  -> RecognizedFeature[]
  -> ToolpathGenerationService
  -> GeneratedToolpath
  -> CommandHandler 写入 PathEntity + PathCurveResource + Block.Children[]
  -> ToolpathOrderService
  -> Block.Children[] 排序计划
```

`RecognizedFeature` 是参数化特征，例如孔可以表达为 path、depth、axis、radius、bevelType、bevelAngle 等。它不是 Entity，也不是 Resource 对 Data 的反向引用。
