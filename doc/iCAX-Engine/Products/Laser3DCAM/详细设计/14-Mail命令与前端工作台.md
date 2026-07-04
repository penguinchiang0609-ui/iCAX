# 14 Mail 命令与前端工作台详细设计

## 1. 模块定位

本模块定义产品前端工作台与后端 ProjectRuntime 的命令交互方式。

Mail 命令负责低频业务操作。PDO 负责高频数据。

## 2. 命令目标

产品命令按主命令 `LaserCam` 聚合。

```text
LaserCamCommandTarget : CommandTarget
```

子命令：

```text
GetScene
ImportTool
ImportModel
RecognizeFeatures
PickObject
GenerateToolpath
UpdateToolpathProcess
UpdateLayer
PlanOrder
GeneratePoseField
GenerateMotionPlan
RunSafetyCheck
StartSimulation
PauseSimulation
ResumeSimulation
StopSimulation
ResetSimulation
```

## 3. 命令消息

payload 使用 JSON 字符串。

请求：

```json
{
  "requestId": "uuid",
  "command": "LaserCam.ImportModel",
  "payload": {}
}
```

响应：

```json
{
  "requestId": "uuid",
  "success": true,
  "payload": {},
  "error": null
}
```

失败：

```json
{
  "requestId": "uuid",
  "success": false,
  "payload": null,
  "error": {
    "code": "InvalidModelFile",
    "message": "Only STEP/STP and IGS/IGES are supported."
  }
}
```

## 4. 命令到服务映射

```text
ImportTool             -> ToolAssemblyImporter
ImportModel            -> CadImportService
RecognizeFeatures      -> FeatureRecognitionService
PickObject             -> PickService
GenerateToolpath       -> ToolpathGenerationService
UpdateToolpathProcess  -> Repository
UpdateLayer            -> Repository
PlanOrder              -> ToolpathOrderService
GeneratePoseField      -> PoseFieldService
GenerateMotionPlan     -> MotionPlanningService
RunSafetyCheck         -> SafetyCheckService
Start/Pause/Resume/... -> CamSimulationService
```

## 5. 前端工作台布局

```text
TopToolbar
  Project commands
  Tool import
  Model import
  Recognize
  Generate toolpath
  Plan
  Safety check
  Simulation

LeftWorkflowPanel
  Tool
  Model
  Recognition/Pick
  Toolpath
  Layer
  Order
  Simulation

Viewport
  WebGL scene

RightPropertyPanel
  Selected object properties

BottomPanel
  Toolpath list
  Safety event list
  Simulation status
```

## 6. 前端状态

前端可本地维护：

- 面板展开状态。
- 当前 tab。
- 相机位置。
- 临时筛选条件。
- loading 状态。

不得本地维护：

- 已确认刀路主数据。
- 图层主数据。
- 工艺主数据。
- 运动规划主数据。
- 安全检查主数据。

## 7. Promise 支持

前端发送命令后返回 Promise。

```text
ProjectProxy.send("LaserCam.ImportModel", payload)
  -> requestId
  -> mailbox post
  -> response mail
  -> resolve/reject promise
```

后端不能假设前端发送后立即同步得到结果。

## 8. 后端主动通知

后端可主动发送：

- Render slot 变化。
- 项目数据变化摘要。
- 安全检查进度。
- 仿真状态变化。
- 错误通知。

主动通知不替代命令响应。

## 9. 测试点

- 每个命令都有 requestId。
- 命令响应能 resolve 对应 Promise。
- 命令失败能 reject 或返回 success=false。
- 大模型不通过 Mailbox 传输。
- 后端主动通知不会破坏命令响应链路。
