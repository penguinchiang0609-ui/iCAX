# Laser3DCAM

`Laser3DCAM` 是三维线条切割 CAM 产品的后端插件。它不保存插件私有项目状态，而是向 Database 注册组件，并向 SDO 注册 `SDOName.MethodName` 项目方法。

当前插件只落地产品数据边界：

- 产品启动组件进入 Repository 的 MetaEntity：`CSceneBootstrapComponent`，仅用于触发场景初始化行为，不保存业务数据。
- 项目入口进入 Repository 的 MetaEntity：`CRootComponent`。
- 机床定义列表是产品级数据，保存在 `ProductSettings.machineDefinitions` 中；产品级导入只托管源文件目录，不解析 SDF/URDF，也不写项目 Database。
- 每个机床实例进入独立 Machine Entity：`CMachineInstanceComponent` 保存机床身份、来源、启用状态、`MachineDefinitionID` 和实例 `ResourceScopeID`；不提供全局机床选择，作业使用哪台机床由 `CJobComponent.MachineEntityID` 表达。
- 机床定义的启用状态只控制“是否允许继续创建实例”；机床实例的启用状态控制“是否能作为作业候选”。禁用实例仍保留在项目中，但不能写入 `CJobComponent.MachineEntityID`。
- 机床实例化时才低频解析托管源文件，得到中性的机床描述数据，并直接展开 Link/Joint/Visual/Collision 子 Entity；这些子 Entity 自己挂具体组件，不把机械结构数组塞回 Machine 根组件。Visual 节点只记录几何资源句柄和材质资源句柄，材质本体以 `SRenderMaterialData` 放在 `Scene.Resources`。
- 每个导入工件进入独立 Workpiece Entity：`CWorkpieceComponent`，`Root.ActiveWorkpieceID` 只表示当前激活工件；管材截面、尺寸、壁厚、长度、腔数和轴向也保存在该组件中。
- 切割系统工艺分组进入独立 CuttingLayer Entity：`CCuttingLayerComponent`。
- 前端显示分组进入独立 VisibleLayer Entity：`CVisibleLayerComponent`。
- 加工程序入口进入独立 Block Entity：`CBlockComponent`，由 `Root.ProgramRootBlockID` 指向。
- 已确认刀路进入独立 Path Entity：`CPathComponent`。
- `CProgramNodeComponent` 是 Block 和 Path 的共同基类，统一提供 `Name`、`PreInstructions[]` 和 `PostInstructions[]`。
- Block 的 `Children` 字段维护子 Block 或 Path 的顺序，刀路自身不保存全局排序。
- 当前选择进入 MetaEntity 上的运行时选择组件：`CSelectionComponent`。
- 导入模型通过 `Scene.Resources().Import()` 进入通用资源导入机制：外部 CAD 文件由资源导入插件转换成 `source` 与 `geometry.brep`，CAM 插件再基于 BRep 生成自己的拓扑索引资源。
- 显示是独立切面：需要显示的 Entity 同时挂 `CRenderInstanceComponent` 和通用 `Transform::CTransformComponent`，业务组件不保存显示状态。
- 刀路曲线以 `CPathCurveResource` 进入 Scene.Resources，当前阶段先记录来源拓扑和资源壳。
- 产品业务算法以 service 形式注册：`FeatureRecognitionService`、`ToolpathGenerationService`、`ToolpathOrderService`。
- 前端只能通过 `SDOName.MethodName` 调用读取和修改项目状态；SDO 只做入口、上下文解析和提交，不承载算法。
- 插件不在 framework 中写入任何产品专属逻辑。

STEP/STP、IGS/IGES 的具体导入由 `iCAX-Plugins/cad/OpenCascadeResourceImport` 提供。该插件通过 OCCT 8.0.0-p1 把 CAD 文件转换成 `iCAX::Resource::CBinaryResource` 和中立 `GeometryData::BRepModel`。Laser3DCAM 不直接依赖 OCCT；后续如果替换为 CGAL 或其他内核，只需要替换资源导入插件。

导入后的 H5 显示链路：

```text
Product startup
  -> SceneBootstrapBehaviour 确保 Root/Selection 存在

WorkpieceModel.Import
  -> Scene.Resources().Import<BRepModel>()
  -> 写入 BRep Resource + Topology Resource
  -> 返回模型、BRep、Topology 资源句柄

Workpiece.Instantiate
  -> 使用资源句柄创建 Workpiece Entity
  -> 返回当前 scene 数据

Scene Tick
  -> View 组合一个或多个 EntityView Source
  -> 按统一契约投影 RenderInstanceComponent / TransformComponent
  -> View 快照发布到 Scene.Resources
  -> H5 ViewClient 通过 Scene channel 管理 View 生命周期
  -> ViewClient 按 View URL 读取最新快照
  -> ThreeRenderViewport 按快照中的 URL 读取几何和材质资源并显示
```

三角化在后端完成，但不创建后端渲染现场。`Workpiece.Instantiate` 创建 Workpiece Entity，并给同 Entity 挂通用 `RenderInstanceComponent` 与 `Transform::CTransformComponent`；同时把 BRep triangulation 转成资源池中的前端几何资源。`CWorkpieceComponent` 只表达工件业务，RenderInstance 只保存几何与材质资源引用。前端必须先通过 View 得到 Entity 与资源 URL，不能自行遍历资源池拼装场景。前端视口根据几何和 transform 自行计算运行时包围盒/包围球。

机床导入链路：

```text
MachineDefinition.Import
  -> 校验当前产品 capabilities.machineDefinition.supportedFormats
  -> 把源文件所在目录托管到产品数据区
  -> ProductSettings.machineDefinitions 追加或更新产品级定义摘要

Machine.Instantiate
  -> 根据 machineDefinitionId 读取产品级托管源文件路径
  -> LoadMachineDescription(managedPath)
  -> Machine Entity 写入 CMachineInstanceComponent.MachineDefinitionID / ResourceScopeID
  -> 直接从机床描述数据展开 Link/Joint/Visual/Collision 子 Entity
  -> Visual 几何和材质生成前端可读的版本化资源
  -> 可显示节点挂 RenderInstanceComponent + Transform::CTransformComponent
  -> EntityView 统一投影到 View 快照
```

拓扑资源中的 face 会记录 `triangleStart/triangleCount`，该范围与几何资源的三角面顺序一致。H5 Three 视口拾取到 `faceIndex` 后，可以映射回 `Selection.PickTopology(kind="face")`。edge/loop 精确拾取由拓扑拾取服务或 collider 数据补齐。

目录结构：

- `SceneComponents.h`：产品场景启动组件和项目根组件。
- `MachineInstanceComponents.h`：机床实例根身份、运动参数、状态、Link、Joint、Visual、Collision 等机床领域组件。
- `WorkpieceComponents.h`：工件领域组件。
- `LayerComponents.h`：切割图层和显示图层组件。
- `SelectionComponents.h`：当前拓扑选择组件。
- `ToolpathComponents.h`：程序节点、Block 和 Path 组件。
- `ToolpathResources.h`：拓扑资源、刀路曲线资源和姿态场资源的数据契约。资源结构体只保存数据，不提供行为，也不反向关联 Repository 或 Entity。
- `MachineResourceKeys.h`：机床实例资源命名空间、机床 visual 材质和贴图资源 key 生成工具。
- `WorkpieceResourceKeys.h`：工件拓扑资源和默认材质资源 key 生成工具。
- `ToolpathResourceKeys.h`：刀路曲线和姿态场资源 key 生成工具。
- `CPoseFieldResource` 使用 `segmentIndex + segmentU` 绑定复合曲线采样位置，只保存世界坐标下的 `beamDirection`；位置由曲线资源按参数求值获得。
- `FeatureRecognitionService.h/.cpp`：从 BRep 模型和特征定义识别参数化特征。
- `SceneBootstrapBehaviour.cpp`：Scene 启动行为，确保 `CRootComponent` 和 `CSelectionComponent` 存在。
- `ToolpathGenerationService.h/.cpp`：把参数化特征转换成可落库的刀路曲线数据。
- `ToolpathOrderService.h/.cpp`：根据 Block 和排序策略生成/应用加工顺序计划。
- `*SDO.cpp`：各 SDO 的声明、静态注册和方法入口。
- `*SDOImplement.cpp/.h`：SDO 方法背后的私有实现细节，不对产品外暴露。
- `Laser3DCAM.h/.cpp`：插件契约版本入口。
- `Laser3DCAM.vcxproj`：插件 DLL 工程，不直接链接具体 CAD 内核。

SDO 方法：

- `MachineDefinition.GetSupportedFormats`：返回当前产品 manifest 中声明的机床定义文件格式能力。
- `MachineDefinition.Import`：导入产品级机床定义源文件；只托管源文件目录并更新 `ProductSettings.machineDefinitions`，不解析、不写项目 Database、不创建机床描述资源。
- `MachineDefinition.List`：返回当前产品级机床定义摘要；该列表只表达“可实例化的机床定义”，不表达当前激活机床。
- `MachineDefinition.SetEnabled`：启用或禁用某个机床定义；禁用定义仍留在项目中，但不能被实例化。
- `Machine.Instantiate`：用 `machineDefinitionId` 读取产品级托管源文件，低频解析为中性机床描述数据，并实例化为新的 Machine Entity。每个部件 Entity 都有 `Transform`，运动约束挂在该部件上，Visual/Collision 作为该部件的附件数据保存；有显示几何的部件再挂通用 `RenderInstance`，由 View 决定前端看到哪些 Entity。
- `Machine.SetEnabled`：启用或禁用指定机床实例；禁用实例会从作业候选中移除，如果它正被作业引用，会清空该作业的机床引用。
- `Job.SetMachine`：把指定且已启用的机床实例写入 `CJobComponent.MachineEntityID`，表达本作业使用哪台机床。
- `Machine.SetParameters`：更新指定机床实例的速度、加速度和轴限位等持久化参数。
- `Machine.SetTCP`：更新指定机床实例 TCP 坐标和光束方向，光束方向会归一化。
- `Machine.Jog`：按轴名点动指定机床实例的运行态轴位置；该运行态字段不进入项目文件和撤销还原。
- `Machine.Home`：把指定机床实例运行态轴位置回零。
- `Machine.Reset`：复位指定机床实例运行态状态。
- `Machine.CheckLimits`：检查指定机床实例的运行态轴位置是否在轴限位内。
- `Machine.CheckReach`：检查指定机床实例的行程规划入口状态；当前阶段只确认机器和工件是否就绪，后续接入 IK、奇异点和碰撞检查。
- `WorkpieceModel.Import`：调用 `Scene.Resources().Import()` 导入 CAD 资源，生成 BRep 和拓扑资源，只返回资源句柄。
- `Workpiece.Instantiate`：使用模型、BRep、Topology 资源句柄追加 Workpiece Entity，并设置 `Root.ActiveWorkpieceID`。
- `Workpiece.SetActive`：切换当前激活工件，并清空当前拓扑选择。
- `WorkpieceModel.Import`：导入 BRep，同时生成 `tube.neutral_geometry`；其二维材料区域支持任意数量内腔，三维差异由 CSG 节点或精确 residual BRep 表达。
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
  CMachineInstanceComponent
  CMachineKinematicsComponent
  CMachineStatusComponent

MachineElementEntity
  CMachineElementComponent
  CMachineLinkComponent
  CMachineJointComponent
  CMachineVisualComponent      // GeometryResourceID + MaterialResourceID
  CRenderInstanceComponent     // GeometryResourceID + MaterialResourceID
  CMachineCollisionComponent
  CTransformComponent

WorkpieceEntity
  CWorkpieceComponent
  CRenderInstanceComponent
  CTransformComponent

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
  -> SDO 写入 PathEntity + PathCurveResource + Block.Children[]
  -> ToolpathOrderService
  -> Block.Children[] 排序计划
```

`RecognizedFeature` 是参数化特征，例如孔可以表达为 path、depth、axis、radius、bevelType、bevelAngle 等。它不是 Entity，也不是 Resource 对 Data 的反向引用。

当前基础算法状态：

- 导入生成 topology 时，会把 BRep face 的第一个 wire 标为 `cut`，其余 wire 标为 `hole`；`Toolpath.RecognizeLoops` 会基于这些角色生成 Path。
- `FeatureRecognitionService` 已提供 BRep wire 拓扑启发式识别：按 `Hole/BevelHole/OuterLoop/Loop` 等定义筛选闭合 wire，输出参数化特征、来源 face/edge/wire 引用和 `polyline3` 预览曲线。
- `ToolpathOrderService` 已提供基础 `NearestNeighbor` 排序：只重排同一 Block 内连续、且能读取几何起止点的 Path；Block 节点和无几何信息的 Path 保持原位置。`Original/Manual/Preserve` 策略保持当前顺序。
