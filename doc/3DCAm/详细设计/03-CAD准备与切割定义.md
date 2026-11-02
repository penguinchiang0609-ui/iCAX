# 03 CAD 准备与切割定义

版本 1.1 · 2026-10-13。覆盖 F10—F17，领域 M02/M04；F18 的几何类型合同在此衔接，识别算法见 [11](11-加工特征识别算法与数据契约.md)，姿态策略见 04。已有模型可直接加工，CAD 编辑不构成必经阶段。

## 1 核心判断：保留生成原因，不能只保留一条线

自动提取、手动选面、草图投影最终都产生 Intent，但来源不同。统一保存 `SourceRecipe`，其类别分别为 SelectedTopology、BoundaryRecognition、SketchProjection、CADFeature、DerivedSplit。不能为了统一入口把草图投影压成一条无来源 polyline，否则改孔距、目标面或工件模型时只能重做。

一条切割线至少同时回答：加工哪个 Placement；依附哪份几何；曲线位于哪个坐标系；哪一侧是保留材料；在哪里开始并沿哪个方向走；设计尺寸在哪个平面/截面定义。工具方向不放进中性 CAD 资源。

## 2 中性几何服务与 CAD 可选编辑

新接口放在中性 CAD 能力边界，输入 BRep/曲面/曲线值及固定引用解析后的数据，输出几何与诊断。接口不能公开 TopoDS、当前 Scene 或产品选中状态。

| 接口能力 | 请求的必要内容 | 结果必须提供 |
| --- | --- | --- |
| Inspect | BRep、检查项、容差/单位 | 问题类型、精确拓扑位置、严重性、检查覆盖 |
| Repair | BRep、明确操作集合、最大形变/缝合预算 | 新几何、修复历史、变化量、未解决问题 |
| Boolean/Feature | 基体、工具体、特征参数、运算类型 | 结果、Generated/Modified/Deleted 来源关系 |
| Section/Project | 几何、平面/投影方向/目标面、容差 | 精确曲线、承载面、参数映射、分支和残差 |
| Evaluate/Measure | 曲线位置/面参数、坐标变换 | 点、导数、法向/距离及有效性 |

现有 `ICadFeatureModelService` 有内核类型和管材中性输入，不能把它改名就宣称通用五轴 CAD 服务完成。复用其内部计算经验和源码实现，通过新中性门面隔离；不从五轴网页 import TubeDesigner 业务编辑器。

修复先形成候选并显示变化量。修复预算不能无限扩大直至内核返回成功，尤其不能吞掉真实设计小孔/缝隙。可选 CSG 特征保存基体与参数，产生新 WorkpieceGeometry；Preview 不落正式 Repository，Commit 才形成一次 Undo。测量不改变几何版本。

## 3 自动提取不等于枚举所有面边

本节给出生命周期边界；可实现的使用图、材料域、双皮配对、贯通/盲腔及圆/矩形/腰型判定以 [11 §4—12](11-加工特征识别算法与数据契约.md) 为准。现有 CamRuntime 仅有 wire 启发式，不能作为本节或 11 的实现验收。选中面片的边界不必然是真实材料边界，必须区分 PatchBoundary；圆形闭环不必然是孔。

### 3.1 输入检查与加工面解释

薄壁实体、单层开放曲面和一般实体分别处理。输入明确表示 `SkinSurface/Solid/Assembly` 及加工侧，不能把一个实体所有面第一条 wire 都当成外轮廓。当前 CamRuntime 的 wire 角色启发式只能作为原型事实，不能当作五轴识别算法。

构建面邻接图，节点保留面方向、曲率/解析类型和几何版本；边保留共边、几何连续性、法向关系及是否周期缝。按选择范围和法向连续条件形成候选加工面片，标出断裂、非流形、重复面、零长度边和方向冲突。

对薄壁实体必须明确选用哪层皮面；两层相近曲面形成两圈边并不意味着要切两次。薄壁关联可给推荐候选，不能未经确认自动删除一层。边界闭合与孔的加工含义分开：封闭实体上的孔口还需检查贯通、盲腔及光束路径。

### 3.2 面片边界与去重

对选中有向面片集合，内部共边抵消，留下加工片边界。周期面 seam 若是同一支撑面的参数接缝，不作为实体开口；空间重合但拓扑独立的边不能只凭坐标一致去重。

按拓扑连接组装有向链，出现度数不为 2、T 分叉、重叠或断裂时返回明确图问题。允许开放修边链，不强行闭合。将每段绑定原边/面/参数区间；将曲线重参数化与业务锚点映射一起保存。

几何分类先判断精确解析曲线；B 样条近圆识别采用拟合残差、平面性、角覆盖和曲率检查，结果带证据与置信范围。近圆不是自动 FixedRotary，腰型孔也不天然具有唯一安装轴。

### 3.3 候选资源

```text
CutCandidate {
  candidateId: UUID, sourceRecipeRef: ResourceRef,
  geometryRef: ResourceRef, topologyRefs: TopologyRef[],
  supportPatchRef: ResourceRef, curveRef: ResourceRef,
  curveFrame: FrameRef, closed: bool,
  geometryKind: GeneralContour | Circle | Rectangle | Slot,
  evidenceRef: ResourceRef, ambiguities: Issue[],
  sourceDigest: Digest, algorithmVersion: string
}
```

候选 ID 在这次候选集内稳定，不拿面索引充当永久 Intent ID。确认时一次批量创建 Intent，重复候选确认通过 commandId/candidate consumption 规则阻止重复。源版本已变的候选可查看，不能再确认。

上列仅是候选摘要，完整字段以 11 的 CutCandidate 为准，包括 boundaryRole、classificationStatus、measurement、passage、材料区域、证据及互斥组；不新增 suggestedGeometryKind 同义权威。一次 Create 消费整个候选凭据，仅创建所选项，其余项需要重新提取。标准分类失败可保留有效 GeneralContour；盲腔、损坏拓扑或未完成证明不能通过改标签强制放行。

## 4 手动选面与精确选择

视口三角 faceIndex 只是拾取线索，经 View 快照中的 geometryRef/topologyMapRef 转成精确面 ID。命令携带其几何版本，原生重新验证，不能接受脱离 View 版本的数字 42。

选择面时显示候选边界，选择边时显示连通链；由同一候选模块完成闭合/方向/支撑解释。手动流程不是绕过检查的后门。多个承载面或两种可用方向必须由用户明确选择，不能自动用第一条。

手动选择直接创建、自动候选确认和投影确认都落到同一个 `CamCut.Create` 事务，因此树、撤销和来源字段保持一致。

## 5 草图投影的完整输入

```text
ProjectionRecipe {
  id: UUID, revision: u64,
  plane: {frame: FrameRef, origin: Point3, xAxis: Unit3, yAxis: Unit3},
  sketch: ResourceRef, constraints: ResourceRef,
  targetGeometry: ResourceRef, targetFaces: TopologyRef[],
  direction: {frame: FrameRef, unit: Unit3},
  rayMode: Positive | Negative | Both,
  branchPolicy: ExplicitBranch | NearestAlongRay,
  chosenBranches: {sketchElementId, sourceInterval, branchId}[],
  designAxis: {frame: FrameRef, unit: Unit3},
  measurementPlane: FrameRef,
  toleranceProfile: ResourceRef
}
```

平面正交、右手、无缩放。草图实体使用稳定 ID，尺寸约束指向实体/控制点；孔组成员 ID 与阵列行列逻辑关联，改数量时有明确新增/删除，不能按排序后位置重分配 ID。

孔径、孔距是工作平面或指定测量截面的尺寸，不冒称曲面弧长。工作平面与模型面关联时保存参考拓扑及偏移；面消失后进入 NeedsRebind，不退回全局 XY。

`projectionDirection` 决定投射线；`designAxis` 表达功能孔安装方向；`toolDirection` 由工艺求解。三者可以相同，但不是一个字段，修改其一不能隐含覆盖其余两个。

## 6 投影计算与退化处理

对草图点 `c(u)`，沿固定方向 d 求 `c(u)+λd = S(v,w)`。先做包围加速，再求精确交点/交曲线；若使用内核 wire projection，仍需恢复源区间和结果分支对应，不能仅取整个 compound。

### 6.1 多层命中

同一源点可能命中上、下两层皮或多个零件。NearestAlongRay 只有在用户明确选择此策略且整个区间分支连续时才自动用；分支交叉/交换时生成歧义区间。不能每个采样点各取最近点再拼出一条突然跳层的线。

分支跟踪以来源参数区间、目标面邻接、λ 的连续性和空间/切向误差联合判断。跨面时保留拼接位置；允许跨越周期 seam，但必须记录参数跨界。不能通过去掉 seam 附近的点掩盖断线。

### 6.2 近相切和数值预算

当 `|n·d|` 很小时，沿射线求交对面位置误差敏感。简单局部估计可用 `ε_hit ≈ ε_surface / |n·d|` 判断需要细化，但不是全局误差证明；进一步由曲率与区间求交残差给出上界。不能无限加密直到看起来平滑。

在配置的距离/角度预算下仍无法得到有界结果，返回 `PROJECTION_TANGENT_UNRESOLVED` 并定位源草图区间，让用户改方向/目标面。求交点位于无限曲面但不在裁剪面域内时必须排除。

### 6.3 结果验证

验证投影曲线位于目标面容差内、回投源平面的尺寸误差、源参数覆盖率、闭合误差、自交与分支连续性。方向性投影不是最近点贴面；必须检查每个结果点与其源点连线平行 d。分段近似不能丢掉几何角点和尺寸约束关键点。

官方 BRepProj 提供方向/中心投影与多 wire 结果迭代，这只解决内核运算；分支选择、尺寸语义和来源持久化由本产品负责：[OCCT Projection](https://dev.opencascade.org/doc/refman/html/class_b_rep_proj___projection.html)。实现以仓库 8.0.0-p1 头文件为准，不随在线文档升级二进制依赖。

## 7 拓扑来源与修改后的重绑定

来源至少为 `(geometryRef, topologyKind, localId, semanticFeatureId?, supportFingerprint)`。优先使用建模历史 Generated/Modified/Deleted：唯一历史映射可自动重绑；一对多只在原参数区间能唯一映射到对应子段时拆分引用；没有唯一证据时 NeedsRebind。

几何指纹用于筛选，不是授权：面积/长度、类型、邻接、位置、方向的近似相似不能证明身份。对称孔阵列尤其不能用最近中心点静默交换孔身份。材料侧、关键点与工艺覆盖只在来源锚点有效时保留。

源 CAD 更新后先生成 RebindReport，逐项列 Current、Mapped、Split、Missing、Ambiguous。可自动提交的唯一项和需用户确认项分开；事务提交新的来源后统一推进失效，不能把部分新来源配上旧 Plan。

## 8 起点和分段的几何锚点

锚点使用 `CurveRef + sourceSpanId + nativeParameter`，加弧长比例作为诊断/展示数据。不能只保存 polyline 顶点编号。闭合起点是遍历原点，不修改源几何形状；反向改变遍历映射而保持材料区域。

当前已接入闭合定义的起点、反向和留料侧界面。`CamCut.ResolveStart` 在固定版本 Intent 精确曲线上调用中性 `ICadReferenceService.ResolveCurve`，输入工件坐标、捕捉距离及最近点/段端点模式；返回来源锚点、精确坐标和距离，不修改工程。显示三角网格仅提供近邻查询位置。`SetStart` 保存来源锚点，`Reverse` 只修改遍历方向，`SetRetainedSide` 当前限 Circle/Rectangle/Slot。全部写操作仍通过场景事务、幂等回执与版本校验；旧查询结果不能覆盖新的输入，反向丢回执不能重复执行。通用分段与曲线显示交互仍按后续工作处理。

打断将原 Intent 标记为被子 Intent 替代并保留来源，不同时让父子都参与生成。子段继承适用工艺、材料侧及区间内人工约束；跨分段关键点产生端点约束或待确认问题；闭合过切不自动继承到每个开放子段。

分割事务一次创建全部子对象、更新层级和替代关系，并让关联生成结果失效。Undo 恢复父对象及其引用；不删除仍被历史 Plan 引用的资源。

## 9 命令与时序

| 方法 | 新增或复用 | 输入/输出要点 |
| --- | --- | --- |
| `CamGeometry.Inspect/Measure` | 新增 | 固定几何和版本；检查/测量只读，返回带覆盖范围的报告 |
| `CamWorkpiece.PreviewPreparation` | 新增 | baseGeometry、operations；产生 RuntimeOnly 候选和变形报告 |
| `CamWorkpiece.Prepare` | 扩充技术基线 | 携带匹配预览的 candidateRef；核验后一次提交几何和来源映射 |
| `CamCut.ExtractCandidates/Create/Split` | 沿用技术基线 | 候选确认统一创建；分段替代一次事务 |
| `CamProjection.Preview` | 新增 | recipeDraft、baseRecipeRef、draftRevision；TaskAccepted → branch candidates；不改工程 |
| `CamProjection.Confirm` | 新增 | candidateRef、chosenBranches、process/pose refs；一次写入 Recipe 和 Intent，返回 Intent IDs |
| `CamGeometry.RebindSources` | 新增 | old/new geometryRef、acceptedMappings；只应用有效映射 |

投影编辑流程：页面建立草稿 → 冻结草稿和工程输入并求投影 → 显示分支/残差 → 确认所选分支 → 原生核对来源和预览摘要 → 发布正式 Recipe 及几何资源 → 一次事务更新 Intent 与 Recipe 关联 → 工艺、运动及报告失效。取消预览不改工程；一次 Undo 恢复整次确认。不存在提前写入正式 Recipe 的隐含步骤。修改孔距只重算投影及受影响路径，其他 Intent 的人工参数不清空；完整 Plan 重算仍遵循 01 的保守依赖规则。

草图编辑可以使用临时 Scene 或共享编辑器；临时 Scene 不拥有最终工件。父 Scene 关闭、目标版本改变或用户取消时，提交资格失效；临时结果只能通过父 Scene 的确认命令转为正式资源。

## 10 验收的判定依据

必须覆盖：周期面 seam、薄壁双皮、真实外边界、内孔与盲腔、开放修边、相切投影、多层分支交换、对称孔阵列重导入、CSG 切分一个面、分段后关键点继承、CAD 取消、跨 Scene 迟到结果。

预期结果由解析平面/圆柱/双层面样本或独立几何求值构造。不能用同一个投影器输出作为它自身的答案；投影结果需独立回投和面距离核验。界面“有一条线”不是通过标准。

特征识别细项见 [11 的 FR01—FR36](11-加工特征识别算法与数据契约.md)，包括可复算尺寸的圆/矩形/腰型/管壁/盲腔样本及反例。它们是待执行原生验收计划，不与文档链接检查合并计为“通过”。
