# Laser3DCAM

`Laser3DCAM` 是三维线条切割 CAM 产品的后端插件。它不保存插件私有项目状态，而是向 Database 注册组件，并向 CommandHandler 注册 `*.*` 项目命令。

当前插件只落地产品数据边界：

- 产品启动组件进入 Repository 的 MetaEntity：`CSceneBootstrapComponent`，仅用于触发场景初始化行为，不保存业务数据。
- 项目入口进入 Repository 的 MetaEntity：`CRootComponent`。
- 当前机器进入独立 Machine Entity：`CMachineComponent`，`Root.ActiveMachineID` 指向当前机器。
- 每个导入工件进入独立 Workpiece Entity：`CWorkpieceComponent`，`Root.ActiveWorkpieceID` 只表示当前激活工件。
- 切割系统工艺分组进入独立 CuttingLayer Entity：`CCuttingLayerComponent`。
- 前端显示分组进入独立 VisibleLayer Entity：`CVisibleLayerComponent`。
- 加工程序入口进入独立 Block Entity：`CBlockComponent`，由 `Root.ProgramRootBlockID` 指向。
- 已确认刀路进入独立 Path Entity：`CPathComponent`。
- `CProgramNodeComponent` 是 Block 和 Path 的共同基类，统一提供 `Name`、`PreInstructions[]` 和 `PostInstructions[]`。
- Block 的 `Children` 字段维护子 Block 或 Path 的顺序，刀路自身不保存全局排序。
- 当前选择进入 MetaEntity 上的运行时选择组件：`CSelectionComponent`。
- 导入模型通过 `Scene.Resources().Import()` 进入通用资源导入机制：外部 CAD 文件由资源导入插件转换成 `source` 与 `geometry.brep`，CAM 插件再基于 BRep 生成自己的拓扑索引资源。
- 显示是独立切面：需要显示的 Entity 同时挂 `CRenderInstanceComponent` 和 `CRenderTransformComponent`，业务组件不保存显示状态。
- 刀路曲线以 `CPathCurveResource` 进入 Scene.Resources，当前阶段先记录来源拓扑和资源壳。
- 产品业务算法以 service 形式注册：`FeatureRecognitionService`、`ToolpathGenerationService`、`ToolpathOrderService`。
- 前端只能通过 `*.*` 命令读取和修改项目状态；CommandHandler 只做入口、上下文解析和提交，不承载算法。
- 插件不在 framework 中写入任何产品专属逻辑。

STEP/STP、IGS/IGES 的具体导入由 `iCAX-Plugins/cad/OpenCascadeResourceImport` 提供。该插件通过 OCCT 8.0.0-p1 把 CAD 文件转换成 `iCAX::Resource::CBinaryResource` 和中立 `GeometryData::BRepModel`。Laser3DCAM 不直接依赖 OCCT；后续如果替换为 CGAL 或其他内核，只需要替换资源导入插件。

导入后的 H5 显示链路：

```text
Product startup
  -> SceneBootstrapBehaviour 确保 Root/Selection/默认相机存在

Workpiece.Import
  -> Scene.Resources().Import<BRepModel>()
  -> 写入 Workpiece Entity + Topology Resource
  -> 返回当前 scene 数据

Scene Tick
  -> 通用 RenderInteraction Behaviour 读取同 Entity 的 RenderInstanceComponent / TransformComponent
  -> RenderInstanceBehaviour 将 BRepModel.Triangulations3 派生成 RenderData.SRenderMeshData
  -> IRenderService.UpsertMesh / SetInstances / SetTransforms 写入当前 scene snapshot
  -> ProjectScene 在所有 Behaviour 之后调用 ServiceProvider.UpdateSceneServices()
  -> IRenderService.OnSceneTick() 调用 IRenderService.Update()
  -> PDORenderService 分配 RenderPDO slot，并通过 Scene mailbox 通知前端
  -> H5 ThreeRenderViewport 读取 PDO slot 并显示网格
```

三角化在后端完成，但不由 `Workpiece.Import` 直接发布；导入命令只负责资源与 Repository 数据，并给同 Entity 挂通用 `RenderInstanceComponent` 与 `RenderTransformComponent`。`CWorkpieceComponent` 只表达工件业务，RenderInstance 才表达显示切面。通用 `RenderInteraction` 行为读取 RenderInstance 的资源 ID；当资源是 `GeometryData::BRepModel` 时，RenderInteraction 从 BRep triangulation 派生成 `SRenderMeshData` 并发布到 `IRenderService`。前端只消费 `render.mesh`、`render.instance_list`、`render.transform` 和 `render.camera` 等 RenderPDO 数据。`render.mesh` 不携带 bounds，前端视口根据 position buffer 和 transform 自行计算运行时包围盒/包围球。Render PDO 的 slot 分配、写入、释放和移动由 `PDORenderService::Update` 统一负责，业务 Behaviour 不直接访问 PDOHub。

拓扑资源中的 face 会记录 `triangleStart/triangleCount`，该范围与导入时写入 RenderMesh 的三角面顺序一致。H5 Three 视口拾取到 `faceIndex` 后，可以映射回 `Selection.PickTopology(kind="face")`。edge/loop 精确拾取不复用 RenderPDO mesh，后续由拓扑拾取服务或 collider 数据继续补齐。

目录结构：

- `ToolpathComponents.h`：toolpath root、machine、workpiece、selection、program node、block、path 组件。
- `ToolpathResources.h`：拓扑资源、刀路曲线资源和姿态场资源的数据契约。资源结构体只保存数据，不提供行为，也不反向关联 Repository 或 Entity。
- `ToolpathResourceKeys.h`：资源 key 生成工具。
- `CPoseFieldResource` 使用 `segmentIndex + segmentU` 绑定复合曲线采样位置，只保存世界坐标下的 `beamDirection`；位置由曲线资源按参数求值获得。
- `FeatureRecognitionService.h/.cpp`：从 BRep 模型和特征定义识别参数化特征。
- `SceneBootstrapBehaviour.cpp`：Scene 启动行为，确保 `CRootComponent`、`CSelectionComponent` 和默认相机 Entity 存在。
- `ToolpathGenerationService.h/.cpp`：把参数化特征转换成可落库的刀路曲线数据。
- `ToolpathOrderService.h/.cpp`：根据 Block 和排序策略生成/应用加工顺序计划。
- `CommandHandlers.*`：命令处理函数集合，承载当前产品命令的具体执行逻辑；后续稳定后继续把领域逻辑下沉到 service。
- `MachineCommandTarget.cpp`：机床命令目标，注册 `Machine.*`。
- `WorkpieceCommandTarget.cpp`：工件命令目标，注册 `Workpiece.*`。
- `SelectionCommandTarget.cpp`：选择命令目标，注册 `Selection.*`。
- `ToolpathCommandTarget.cpp`：刀路命令目标，注册 `Toolpath.*`。
- `SceneCommandTarget.cpp` / `CameraViewCommandTarget.cpp`：场景查询和视图命令目标，注册 `Scene.*` 与 `CameraView.*`。
- `Laser3DCAM.h/.cpp`：插件契约版本入口。
- `Laser3DCAM.vcxproj`：插件 DLL 工程，不直接链接具体 CAD 内核。

命令：

- `Scene.Get`：返回项目 root、机器列表、当前机器、工件列表、当前激活工件、拓扑状态、选择、program 树和从 program 树展开的刀路列表。
- `Machine.Import`：导入 SDF/XML 机器描述文件，写入或更新当前 Machine Entity，并初始化默认工位、轴、TCP 和机器参数。
  导入时会把 SDF visual/collision 展开为普通 MachinePart Entity；这些 Entity 自己挂通用 RenderInstance/Transform 组件，前端不理解“机床整体”，只播放渲染对象流。
- `Machine.SetParameters`：更新当前机器的速度、加速度和轴限位等持久化参数。
- `Machine.SetTCP`：更新当前机器 TCP 坐标和光束方向，光束方向会归一化。
- `Machine.Jog`：按轴名点动当前机器运行态轴位置；该运行态字段不进入项目文件和撤销还原。
- `Machine.Home`：把当前机器运行态轴位置回零。
- `Machine.Reset`：复位当前机器运行态状态。
- `Machine.CheckLimits`：检查当前运行态轴位置是否在轴限位内。
- `Machine.CheckReach`：检查行程规划入口状态；当前阶段只确认机器和工件是否就绪，后续接入 IK、奇异点和碰撞检查。
- `Workpiece.Import`：调用 `Scene.Resources().Import()` 导入资源，生成拓扑资源，追加 Workpiece Entity，并设置 `Root.ActiveWorkpieceID`。
- `Workpiece.SetActive`：切换当前激活工件，并清空当前拓扑选择。
- `Selection.PickTopology`：选择一个已存在的拓扑对象。
- `Toolpath.RecognizeLoops`：从拓扑资源中筛选 `role=hole/cut` 的 loop，创建 Path Entity，并追加到根 Block。
- `Toolpath.AddSelectionPath`：把当前选择创建为 Path Entity，并追加到根 Block。
- `Toolpath.SetPoseField`：为指定 Path 写入姿态场采样，采样点使用 `segmentIndex + segmentU`。
- `Toolpath.ClearProgram`：删除当前项目中的 Path Entity，清空根 Block 的 children。

Repository 数据形态：

```text
MetaEntity
  CSceneBootstrapComponent
  CRootComponent
  CSelectionComponent

MachineEntity
  CMachineComponent

MachinePartEntity
  CMachinePartComponent
  CRenderInstanceComponent
  CRenderTransformComponent

WorkpieceEntity
  CWorkpieceComponent
  CRenderInstanceComponent
  CRenderTransformComponent

CuttingLayerEntity
  CCuttingLayerComponent

VisibleLayerEntity
  CVisibleLayerComponent

ProgramRootBlockEntity
  CBlockComponent

PathEntity
  CPathComponent
```

Program 树关系：

```text
CRootComponent.ProgramRootBlockID -> ProgramRootBlockEntity

CBlockComponent
  Name
  PreInstructions[]
  PostInstructions[]
  Children[]

CPathComponent
  Name
  PreInstructions[]
  PostInstructions[]
  CuttingLayerID
  VisibleLayerID
  CurveResourceID
  PoseFieldResourceID
```

`PreInstructions[]` 和 `PostInstructions[]` 的元素是 `Instruction`：

```text
Instruction
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

当前基础算法状态：

- 导入生成 topology 时，会把 BRep face 的第一个 wire 标为 `cut`，其余 wire 标为 `hole`；`Toolpath.RecognizeLoops` 会基于这些角色生成 Path。
- `FeatureRecognitionService` 已提供 BRep wire 拓扑启发式识别：按 `Hole/BevelHole/OuterLoop/Loop` 等定义筛选闭合 wire，输出参数化特征、来源 face/edge/wire 引用和 `polyline3` 预览曲线。
- `ToolpathOrderService` 已提供基础 `NearestNeighbor` 排序：只重排同一 Block 内连续、且能读取几何起止点的 Path；Block 节点和无几何信息的 Path 保持原位置。`Original/Manual/Preserve` 策略保持当前顺序。
