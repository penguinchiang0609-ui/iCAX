# 平面激光 CAM 产品设计文档

## 1. 文档定位

本文设计一个基于 iCAX Engine 的平面激光 CAM 产品，目标是覆盖板材激光切割从图纸导入、图形清理、工艺设置、排样、路径规划、仿真到 NC 输出的核心流程。

本文参考柏楚公开资料中 CypCut/CypNest 的能力边界，但不是复刻某个具体软件。参考信息包括：

- 柏楚 CypCut 官方页面说明其面向激光切割行业，包含导入图形自动优化、引线/微连/补偿等工艺设置、加工控制、统计、寻边辅助等能力。[柏楚 CypCut](https://www.bochu.com/ch/soft/cypcut/)
- 柏楚 CypNest 官方页面说明其面向平面套料，包含图纸处理、套料、刀路编辑、生成表单，以及自动共边排样、智能排序、批量添加工艺、报价等能力。[柏楚 CypNest](https://www.bochu.com/ch/soft/CypNest/)
- CypCut 用户手册将软件主功能概括为图形处理、参数设置、自定义切割过程编辑、排样、路径规划、模拟和切割加工控制，并说明实际加工控制需要配套硬件。[CypCut 用户手册 V6.5](https://d.fscut.com/wordpress-fscut/2022/12/CypCut%E7%94%A8%E6%88%B7%E6%89%8B%E5%86%8CV6.5.pdf)

## 2. 产品定位

产品名称建议：

```text
FlatLaserCAM
```

中文名：

```text
平面激光 CAM
```

产品目标：

- 面向二维板材激光切割。
- 以 Project 为工程边界。
- 支持导入 CAD 图纸并转化为可加工零件。
- 支持板材、零件、排样、工艺、刀路、仿真、NC 输出。
- 前端 H5 提供交互式 CAD/CAM 工作台。
- 后端 C++ 提供几何处理、工艺计算、排样、路径规划和资源管理。
- 机床控制先设计接口，MVP 阶段只做仿真和 NC 输出，不直接控制真实设备。

非目标：

- 不做管材切割。
- 不做三维五轴切割。
- 不绑定柏楚控制卡或某一家控制器。
- 不在第一阶段实现云排样、MES、报价系统。
- 不把大量几何点、刀路点塞进 Mailbox。

## 3. 产品定义

建议产品定义：

```cpp
iCAX::Product::CProductDefinition product;
product.ProductID = "icax.flat-laser-cam";
product.ProductName = "Flat Laser CAM";
product.ProductVersion = "0.1.0";
product.FrontendEntry = "apps/flat-laser-cam/webpage/entry.mjs";
product.DefaultProjectStartupComponent = "CFlatLaserProjectComponent";

product.ProjectFile.Magic = "ICAX_FLAT_LASER_CAM";
product.ProjectFile.FormatVersion = "1.0";
product.ProjectFile.FileExtensions.push_back(".ilcam");
product.ProjectFile.MagicOffset = 0;
product.ProjectFile.ProbeBytes = 64;

product.Modules.ComponentModules.push_back("bin/FlatLaserCamComponent.dll");
product.Modules.BehaviourModules.push_back("bin/FlatLaserCamBehaviour.dll");
product.Modules.ServiceModules.push_back("bin/FlatLaserCamService.dll");
product.Modules.FacadeModules.push_back("bin/FlatLaserCamCommand.dll");
```

文件类型建议：

```text
.ilcam       iCAX Laser CAM project
.iljob       加工任务包，可选
.nc/.cnc     后处理输出
```

项目文件必须写入 `ICAX_FLAT_LASER_CAM` magic。双击项目文件时，ApplicationHost 只根据 magic 识别产品，不根据扩展名识别。

## 4. 总体业务流程

```text
新建/打开工程
  -> 创建板材
  -> 导入图纸
  -> 图纸检测与修复
  -> 零件识别
  -> 图层/颜色映射工艺
  -> 设置引线、穿孔、微连、补偿等工艺
  -> 自动/手动排样
  -> 切割排序
  -> 刀路生成
  -> 仿真检查
  -> 生成报表/加工单
  -> 导出 NC
  -> 真实设备加工控制（后续阶段）
```

核心原则：

- 图纸导入和清理是 CAM 的入口。
- 工艺设置绑定到零件、轮廓、图层或路径段。
- 排样结果是零件实例在板材上的布局。
- 刀路是从排样和工艺派生出来的可加工路径。
- 仿真和 NC 输出依赖刀路。
- 工程保存的是可复现输入和关键结果，大型中间结果可作为 Resource 保存。

## 5. Backend 模块划分

建议 backend 放在：

```text
src/apps/flat-laser-cam/backend/
  component/FlatLaserCamComponent/
  behaviour/FlatLaserCamBehaviour/
  service/FlatLaserCamService/
  command/FlatLaserCamCommand/
```

### 5.1 Component 模块

负责声明 EC 数据结构和字段 meta。

当前骨架已实现：

- `CFlatLaserProjectComponent`：工程元信息。
- `CMaterialComponent`：材料。
- `CSheetComponent`：板材。
- `CDrawingComponent`：CAD 图纸引用。
- `CPartDefinitionComponent`：零件定义。
- `CPartInstanceComponent`：排样实例。
- `CCutProcessComponent`：切割工艺。
- `CNestingPlanComponent`：排样结果。
- `CToolpathComponent`：刀路摘要。
- `CSimulationComponent`：仿真摘要和运行进度。
- `CNCProgramComponent`：NC 输出记录。

### 5.2 Behaviour 模块

负责由数据变化触发的状态维护和派生失效。

建议覆盖：

- 图纸导入后标记几何检测状态。
- 零件几何变化后使排样结果过期。
- 工艺变化后使刀路过期。
- 排样变化后使切割排序和刀路过期。
- 刀路变化后使仿真和 NC 输出过期。
- 项目打开后绑定默认行为。

Behaviour 不保存业务状态，只按当前 Project 显式传入的 ApplicationContext、ProductContext、ProjectContext 工作。

### 5.3 Service 模块

Service 提供算法和外部能力入口。Service 是 Application 级共享，不能保存某个 Project 的运行状态。

建议接口：

```text
IGeometryImportService       导入 DXF/DWG/SVG/JSON 等图形
IGeometryCleanService        去重、合并线段、闭合轮廓、曲线离散/拟合
IPartRecognitionService      从图纸识别零件、内外轮廓、文字/标记
IProcessLibraryService       材料/厚度/气体/功率工艺库
INestingService              自动排样、共边、余料利用
IPathPlanningService         引线、微连、补偿、穿孔、切割排序、刀路生成
ISimulationService           刀路仿真、碰撞/越界/空移统计
IPostProcessorService        后处理输出 NC 或控制器任务包
IReportService               加工单、报价单、材料利用率报表
IMachineProfileService       机床能力、控制器、坐标系、限位、喷嘴/气体配置
IControllerAdapterService    机床控制适配，MVP 只提供 mock 或离线接口
```

### 5.4 Command 模块

Command 把前端操作映射到后端用例。

当前框架已有 `FacadeRegistry`、`FacadeInvoker` 和 `FacadeRegistrationCatalog`。产品命令模块通过 `ICAX_REGISTER_FACADE` 自动注册，ProductRuntime 加载产品 DLL 后按模块路径回放注册。

### 5.5 ResourceLoader 模块

ResourceLoader 负责大型对象的加载：

```text
DXF/DWG 原始图纸
标准工艺库
机床配置
刀路大数组
仿真缓存
NC 文件
报表文件
```

资源实例归属 Project 的 ResourceLibrary，loader 定义应用级注册。

## 6. Project 数据模型

平面激光 CAM 的 Project 是一个完整加工工程。它包含：

```text
Project
  Repository
    ProjectRoot entity
    Sheet entities
    Drawing entities
    PartDefinition entities
    PartInstance entities
    Process entities
    Toolpath entities
    NCProgram entities

  ResourceLibrary
    imported drawings
    geometry cache
    toolpath cache
    simulation cache
    reports

  Universe
    behaviours

  MailChannel
    project commands

  PDO
    progress, preview, simulation, machine state
```

## 7. EC 组件设计

### 7.1 ProjectRootComponent

挂在工程根 Entity。

持久化字段：

```text
projectName
projectPath
unit
createdAt
modifiedAt
author
defaultMaterialId
defaultMachineProfileId
```

用途：

- 工程元信息。
- 默认单位和默认材料。
- 默认机床配置。

### 7.2 MaterialComponent

表示材料定义。

持久化字段：

```text
materialId
name
category              CarbonSteel / StainlessSteel / Aluminum / Copper ...
density
thickness
surface
```

用途：

- 排样按材料和厚度分组。
- 工艺库按材料、厚度、气体匹配。
- 报价和重量统计。

### 7.3 SheetComponent

表示一张板材。

持久化字段：

```text
sheetId
name
materialId
width
height
thickness
origin
margin
grainDirection
isRemnant
```

派生字段：

```text
usableArea
usedArea
utilizationRate
bounds
```

用途：

- 排样容器。
- 材料利用率计算。
- 加工坐标系基础。

### 7.4 DrawingComponent

表示导入图纸。

持久化字段：

```text
drawingId
sourceResourceKey
fileName
importTime
unit
scale
layerCount
status
```

用途：

- 保存原始图纸资源引用。
- 记录图纸导入状态。
- 支持重新导入或溯源。

### 7.5 GeometryComponent

表示经过清理和标准化后的几何。

持久化字段：

```text
geometryResourceKey
geometryVersion
sourceDrawingId
tolerance
```

派生字段：

```text
bounds
loopCount
openCurveCount
selfIntersectionCount
```

设计建议：

- 少量几何摘要放 Repository。
- 大量曲线点、轮廓数组放 Resource。
- bounds、错误数量这类摘要用派生字段。

### 7.6 PartDefinitionComponent

表示零件定义。

持久化字段：

```text
partId
name
geometryResourceKey
quantity
materialId
priority
allowRotate
allowMirror
processPresetId
```

派生字段：

```text
bounds
area
outerLoopCount
innerLoopCount
estimatedCutLength
```

用途：

- 一个零件定义可以有多个排样实例。
- 零件几何和数量是排样输入。

### 7.7 PartInstanceComponent

表示零件在某张板材上的一个实例。

持久化字段：

```text
instanceId
partId
sheetId
transform
rotation
mirror
nestingGroupId
sequenceIndex
isLocked
```

派生字段：

```text
worldBounds
occupiedArea
```

用途：

- 排样布局。
- 手动拖拽、旋转、锁定。
- 刀路生成的空间输入。

### 7.8 LayerMappingComponent

表示图层、颜色、线型到工艺的映射。

持久化字段：

```text
sourceLayer
sourceColor
operationType          Cut / Mark / PierceOnly / Ignore
processId
priority
```

用途：

- 导入图纸后自动识别切割、标刻、辅助线。
- 批量应用工艺。

### 7.9 CutProcessComponent

表示切割工艺。

持久化字段：

```text
processId
name
materialId
thickness
gas
gasPressure
laserPower
frequency
dutyCycle
cutSpeed
cutHeight
pierceMode
pierceTime
leadInType
leadInLength
leadOutType
leadOutLength
kerfWidth
cornerStrategy
microJointEnabled
microJointLength
```

用途：

- 工艺库匹配。
- 刀路生成。
- NC 后处理。

### 7.10 NestingPlanComponent

表示排样结果。

持久化字段：

```text
nestingPlanId
sheetIds
partInstanceIds
algorithm
spacing
edgeMargin
commonCutEnabled
status
```

派生字段：

```text
utilizationRate
partPlacedCount
partUnplacedCount
```

用途：

- 自动排样结果。
- 手动排样修改。
- 余料和材料利用率统计。

### 7.11 CuttingOrderComponent

表示切割排序。

持久化字段：

```text
orderId
sheetId
orderedContourIds
strategy
startCorner
insideBeforeOutside
shortTravelPreferred
```

派生字段：

```text
rapidMoveLength
estimatedTime
```

用途：

- 路径规划输入。
- 用户手动排序。

### 7.12 ToolpathComponent

表示刀路结果。

持久化字段：

```text
toolpathId
sheetId
toolpathResourceKey
sourceNestingPlanId
sourceOrderId
status
```

派生字段：

```text
cutLength
rapidMoveLength
pierceCount
estimatedTime
warningCount
```

设计建议：

- 刀路大数组放 Resource。
- Repository 只保存刀路资源引用和摘要。

### 7.13 SimulationComponent

表示仿真结果摘要。

持久化字段：

```text
simulationId
toolpathId
status
lastRunTime
warningSummary
```

运行时字段：

```text
currentFrame
currentPosition
playbackSpeed
```

用途：

- 保存仿真摘要。
- 运行中帧数据通过 PDO 推送。

### 7.14 NCProgramComponent

表示后处理输出。

持久化字段：

```text
programId
toolpathId
postProcessorId
outputResourceKey
controllerType
createdAt
status
```

用途：

- 记录 NC 文件。
- 支持重新导出。
- 支持加工任务包。

## 8. 字段策略

建议：

- 工程、板材、材料、零件、工艺、排样、排序、刀路摘要、NC 输出记录使用持久化字段。
- 当前选中、视图矩阵、鼠标位置、播放帧等只属于前端 UI 或运行时字段。
- bounds、面积、利用率、切割长度、预计加工时间等用派生字段。
- 大数组和外部文件用 Resource，不直接存 Component 字段。

## 9. Resource 设计

建议资源类型：

```text
DrawingResource          原始 DXF/DWG/SVG 文件
NormalizedGeometry       标准化几何数据
PartGeometry             零件轮廓
NestingCache             排样中间结果
ToolpathResource         刀路段、工艺段、运动段
SimulationCache          仿真帧缓存
NCProgramResource        NC 输出文件
ReportResource           PDF/Excel/HTML 报表
MachineProfileResource   机床配置
ProcessLibraryResource   工艺库
```

资源保存策略：

- 原始图纸：`Embedded` 或 `External`，取决于用户导入选项。
- 标准化几何：建议 `Embedded`，保证工程可复现。
- ToolpathResource：可 `Embedded`，也可根据体积选择重新生成。
- SimulationCache：通常 `RuntimeOnly`。
- NCProgramResource：通常 `Embedded` 或 `External`。
- MachineProfileResource：应用级配置可 `External`，项目使用的快照可 `Embedded`。

## 10. Command 设计

### 10.1 Product 级命令

发往 product mailbox。

```text
FlatLaser.Product.GetState
FlatLaser.Product.ListRecentProjects
FlatLaser.Product.ListProcessLibraries
FlatLaser.Product.ListMachineProfiles
FlatLaser.Product.CreateProject
```

### 10.2 Project 级命令

发往 project mailbox。

```text
flatLaser.importDrawing
flatLaser.createSheet
flatLaser.generateNesting
flatLaser.generateToolpath
flatLaser.runSimulation
flatLaser.generateNc
```

当前命令 type code：

| 命令 | TypeCode |
| --- | --- |
| `flatLaser.importDrawing` | `0x464C43414D000001` |
| `flatLaser.createSheet` | `0x464C43414D000002` |
| `flatLaser.generateNesting` | `0x464C43414D000003` |
| `flatLaser.generateToolpath` | `0x464C43414D000004` |
| `flatLaser.runSimulation` | `0x464C43414D000005` |
| `flatLaser.generateNc` | `0x464C43414D000006` |

当前 handler 是产品骨架实现：完成命令注册、服务解析和成功响应。真实导入、排样、刀路、仿真和后处理算法后续在 Service 中补齐。

### 10.3 命令响应原则

普通命令响应只返回摘要：

```json
{
  "projectId": "...",
  "changedEntityIds": ["..."],
  "resourceKeys": ["..."],
  "warnings": [],
  "state": "Ok"
}
```

大型结果不走 Mailbox：

- 导入后的几何放 Resource。
- 刀路放 Resource。
- 仿真帧走 PDO 或 SimulationCache。
- 报表和 NC 文件放 Resource。

### 10.4 长耗时命令

排样、图纸修复、刀路生成可能耗时较长。当前不引入全局 task/workshop。

建议：

- MVP 阶段在 Project 线程串行执行。
- 命令立即返回一个 operationId。
- 进度通过 PDO 推送。
- 完成后通过 Mailbox 事件或下一次状态查询获得结果。

```text
Command response:
  operationId
  progressPdoId

PDO:
  FlatLaser.OperationProgress
```

## 11. PDO 设计

PDO 的协议提前定义，实例运行时创建。

### 11.1 OperationProgress PDO

用途：

- 图纸导入。
- 自动排样。
- 刀路生成。
- 仿真。
- NC 导出。

Payload：

```json
{
  "operationId": "...",
  "stage": "Nesting",
  "progress": 0.42,
  "message": "placing parts",
  "canCancel": false
}
```

策略：

- latest-only。
- 10 到 30 Hz。
- 可丢弃。

### 11.2 ToolpathPreview PDO

用途：

- 刀路生成过程中增量预览。
- 排序变化后局部刷新。

Payload：

```json
{
  "toolpathId": "...",
  "version": 12,
  "previewResourceKey": "..."
}
```

策略：

- latest-only。
- 大数据通过 Resource。

### 11.3 SimulationFrame PDO

用途：

- 仿真播放帧。

Payload：

```json
{
  "simulationId": "...",
  "frame": 128,
  "time": 3.2,
  "headPosition": [120.0, 80.0],
  "laserOn": true,
  "currentContourId": "..."
}
```

策略：

- 30 到 60 Hz。
- latest-only。
- 可丢弃。

### 11.4 MachineRealtimeState PDO

用途：

- 后续真实设备控制。
- 显示轴位置、激光状态、报警状态、加工进度。

Payload：

```json
{
  "machineId": "...",
  "state": "Running",
  "x": 120.0,
  "y": 80.0,
  "z": 15.0,
  "feed": 12000,
  "laserOn": true,
  "gasPressure": 0.8,
  "alarmLevel": "None"
}
```

策略：

- 30 到 60 Hz。
- latest-only。
- 报警明细用 Mailbox 事件，实时状态用 PDO。

## 12. Frontend 设计

### 12.1 产品首页

ProductWorkspace 首页显示：

- 最近打开工程。
- 新建工程。
- 导入图纸快速入口。
- 工艺库。
- 机床配置。
- 模板和示例工程。

### 12.2 ProjectWorkspace 布局

建议主界面：

```text
┌──────────────────────────────────────────────────────────────┐
│ AppBar: 工程  保存  撤销  重做  导入  排样  刀路  仿真  导出 │
├───────────────┬───────────────────────────────┬──────────────┤
│ Project Tree  │ CAD/CAM Canvas                │ Property     │
│ 板材/零件/工艺 │ 图形、排样、刀路、仿真          │ 工艺/属性     │
├───────────────┴───────────────────────────────┴──────────────┤
│ Bottom Dock: 错误检查 / 操作日志 / 加工统计 / NC 输出         │
└──────────────────────────────────────────────────────────────┘
```

### 12.3 工作区 Tab

建议按业务阶段组织：

```text
Drawing      图纸导入、清理、零件识别
Nesting      板材、排样、手动调整
Technology   工艺、图层映射、引线、微连、补偿
Toolpath     排序、刀路、过切、共边
Simulation   仿真、报警、加工时间估算
Output       NC、报表、任务包
Machine      后续真实加工控制
```

### 12.4 前端状态

前端只保存 UI 需要的轻量状态：

```text
activeSheetId
activePartId
activeToolpathId
selection
viewport
visibleLayers
panelState
operationProgress
pdoSnapshots
```

几何、刀路、仿真帧等大数据通过 Resource 或 PDO 访问。

## 13. 后处理与机床控制

MVP 阶段：

- 输出 NC 文件。
- 输出加工单。
- 提供仿真和时间估计。
- 不直接控制真实机床。

后续阶段：

- 增加 `IControllerAdapterService`。
- 增加机床连接状态。
- 增加坐标系、寻边、回零、暂停、继续、停止等命令。
- 增加安全确认和权限控制。
- 将实时机床状态通过 PDO 推送。
- 将报警、急停、加工完成通过 Mailbox 事件推送。

安全原则：

- 所有真实运动命令必须经过后端权限和状态检查。
- 前端按钮不能直接映射硬件 API。
- 控制器适配层必须能进入 mock/simulation 模式。
- 加工控制应有独立的硬件能力描述和安全互锁。

## 14. 与 iCAX 框架的对应关系

```text
ApplicationHost
  产品列表、项目文件 magic 识别、启动 FlatLaserCAM

ProductRuntime
  FlatLaserCAM 产品定义、最近项目、产品命令、ProjectCatalog

Project
  一个平面激光 CAM 工程

Repository
  板材、零件、工艺、排样、刀路摘要、NC 记录

ResourceLibrary
  原始图纸、几何缓存、刀路大数组、NC 文件、报表

Universe
  派生失效、状态维护、项目行为

Services
  几何导入、排样、路径规划、后处理、报表、控制器适配

Mailbox
  导入、排样、生成刀路、导出 NC 等命令

PDO
  进度、仿真帧、刀路预览、机床实时状态
```

## 15. 阶段计划

### M0 产品壳

目标：

- 新增 `src/apps/flat-laser-cam` 目录。
- 定义 ProductDefinition。
- H5 产品首页可启动。
- 可新建/打开 `.ilcam` 空工程。

验收：

- `App.StartProduct("icax.flat-laser-cam")` 返回 productChannelId。
- H5 加载产品工作区。
- Project 创建后返回 projectChannelId。

### M1 图纸导入和清理

目标：

- 导入 DXF。
- 基础图形标准化。
- 检测重复线、断线、开放轮廓、自交。
- 显示图纸错误列表。

验收：

- `ImportDrawing` 生成 Drawing、Geometry、PartDefinition。
- 图形能在 H5 canvas 中显示。
- 错误列表可定位到对应几何。

### M2 工艺设置

目标：

- 材料、厚度、气体、功率、速度等工艺参数。
- 图层映射工艺。
- 引线、微连、补偿、穿孔基础参数。

验收：

- 工艺可批量应用到零件或轮廓。
- 修改工艺后刀路状态变为过期。

### M3 排样

目标：

- 创建板材。
- 零件数量管理。
- 基础自动排样。
- 手动移动、旋转、锁定。

验收：

- 排样结果生成 PartInstance。
- 材料利用率可计算。
- 排样进度通过 PDO 推送。

### M4 刀路和排序

目标：

- 内外轮廓排序。
- 引线和补偿生成。
- 微连。
- 空移优化。
- 刀路预览。

验收：

- `GenerateToolpath` 生成 ToolpathResource。
- 预估切割长度、空移长度、穿孔数量。
- H5 可显示刀路方向和顺序。

### M5 仿真和 NC 输出

目标：

- 仿真播放。
- 越界、缺工艺、开放轮廓等报警。
- 后处理输出 NC。
- 输出加工单。

验收：

- 仿真帧通过 PDO 更新。
- `ExportNC` 生成 NCProgramResource。
- 加工单可导出。

### M6 机床控制接口

目标：

- 定义控制器适配接口。
- mock 控制器。
- 机床实时状态 PDO。
- 安全确认机制。

验收：

- mock 模式可开始、暂停、继续、停止。
- 前端控制栏显示实时状态。
- 所有真实设备命令可以被权限和状态机拦截。

## 16. 关键风险

### 16.1 几何鲁棒性

CAD 图纸质量不可控。断线、重线、极短线、自交、单位错误、块引用、文字、样条都会影响后续加工。

应对：

- 几何导入和几何清理独立成服务。
- 每个修复动作必须可记录和可撤销。
- 错误检测结果要能定位到图形。

### 16.2 排样算法复杂度

自动排样直接影响材料利用率和用户体验。

应对：

- MVP 先实现稳定基础算法。
- 高级共边、异形板、余料利用分阶段。
- 排样算法输入输出资源化，便于替换算法。

### 16.3 刀路与工艺强耦合

工艺参数、引线、穿孔、微连、补偿会影响刀路合法性。

应对：

- 工艺变化必须使刀路过期。
- 刀路生成保留 warning。
- 后处理前必须跑校验。

### 16.4 大数据性能

图纸、排样、刀路、仿真帧数据量大。

应对：

- Repository 只放摘要和引用。
- 大数组放 Resource。
- 高频播放走 PDO。
- H5 canvas/WebGL 分层渲染。

### 16.5 真实加工安全

机床控制涉及运动、激光、气体和安全互锁。

应对：

- MVP 不直接接真实设备。
- 控制器适配层先 mock。
- 真实设备命令必须经过状态机、权限、互锁和二次确认。

## 17. MVP 最小闭环

最小可演示闭环：

```text
启动 FlatLaserCAM
  -> 新建工程
  -> 创建板材
  -> 导入 DXF
  -> 自动识别零件
  -> 应用默认工艺
  -> 自动排样
  -> 生成刀路
  -> 仿真
  -> 导出 NC
```

该闭环不需要真实机床，也不需要高级共边。它能验证：

- iCAX Product/Project 生命周期。
- Repository/Resource 分工。
- Mailbox Promise 命令。
- PDO 进度和仿真帧。
- H5 工作台的产品扩展方式。

## 18. 后续建议

优先实现顺序建议：

1. FlatLaserCAM 产品壳和 H5 产品入口。
2. Project 数据模型和空工程保存/打开。
3. DXF 导入 ResourceLoader。
4. 几何清理和图形显示。
5. 工艺参数模型。
6. 基础排样。
7. 基础刀路生成。
8. 仿真 PDO。
9. NC 后处理。
10. 控制器 mock。

当 MVP 跑通后，再考虑：

- 共边排样。
- 飞切。
- 复杂穿孔策略。
- 余料管理。
- 报价。
- MES/云排样对接。
- 真实控制器适配。
