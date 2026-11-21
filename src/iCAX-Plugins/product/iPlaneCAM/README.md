# iPlaneCAM 原生产品

产品 ID：`icax.iplane-cam`。模块：`iPlaneCAM.dll`。启动组件：`CPlaneSceneComponent`。

本轮r13新增椭圆、椭圆弧、贝塞尔、B样条、三角形、正多边形、星形七种独立图元，以及左侧“线条 / 曲线 / 形状”分组箭头菜单。底图和零件/板材所属草图复用SDK交互与几何服务，原生SDO分别校验业务目标；混合多义线、图文及加工副本继续保留。交付包为 [20261117-r13](../../../../output/releases/iPlaneCAM-0.1.0-Debug-20261117-r13)，1773次原生调用、16套154组浏览器检查及完整打包核验通过，本轮证据见 [验收汇总](../../../../output/tests/iplane-cam-shapes/acceptance.json)。本轮不做旧工程或旧API兼容。

[r12历史汇总](../../../../output/tests/iplane-cam-worktable/acceptance.json)记录1140次原生调用、15套152组浏览器检查、182项框架测试及10项ICRT协议检查，只绑定r12。带孔材料副本的变换/复制/阵列由几何服务重新校验材料语义并同步EC孔数，撤销重做及保存恢复均保持一致；普通自由曲线仍允许开放绘制。

本模块负责底图、零件、板材、正式草图、图层、排样任务和布局结果的业务调度。底图是可直接绘制、编辑加工轨迹几何的正式台面；零件与板材通过各自草图编辑几何。底图与草图拥有独立业务身份，没有从底图生成零件或板材的命令。行业工艺与 NC 输出保留为独立扩展，本轮不实现。

底部命令/日志选项卡、中央状态栏、图层栏、标注及异步刷新保留在当前产品中，随本轮包执行当前功能回归。
## 领域与服务分层

SDO 是 Scene 业务入口，各入口直接绑定自己的 Logic 成员，不经过统一字符串命令路由。Logic 负责权限、版本、对象关系、事务、撤销、资源引用与任务提交；中性几何和排样算法由注入的 Service 执行。

| 源文件 | 责任 |
| --- | --- |
| [PlaneSDO.cpp](PlaneSDO.cpp) | `CPlaneSceneSDO` / `SceneLogic`，只读场景投影 |
| [PlaneDrawingSDO.cpp](PlaneDrawingSDO.cpp) | `CPlaneDrawingSDO` / `DrawingLogic`，底图导入、创建与编辑 |
| [PlanePartSDO.cpp](PlanePartSDO.cpp)、[PlaneStockSDO.cpp](PlaneStockSDO.cpp) | 零件与板材各自的 SDO / Logic |
| [PlaneObjectSDO.cpp](PlaneObjectSDO.cpp) | `CPlaneObjectSDO` / `ObjectLogic`，对象属性与删除 |
| [PlaneSketchSDO.cpp](PlaneSketchSDO.cpp) | `CPlaneSketchSDO` / `SketchLogic`，正式草图生命周期 |
| [PlaneLayerSDO.cpp](PlaneLayerSDO.cpp) | `CPlaneLayerSDO` / `LayerLogic`，图层与归层 |
| [PlaneNestingSDO.cpp](PlaneNestingSDO.cpp) | `CPlaneNestingSDO`、`CPlaneLayoutSDO` / `NestingLogic`，排样及报告 |
| [PlaneEditTasks.cpp](PlaneEditTasks.cpp) | `CPlaneTaskSDO` / `EditTaskLogic`，框架几何任务协调 |
| [PlaneTextSDO.cpp](PlaneTextSDO.cpp)、[PlaneImageSDO.cpp](PlaneImageSDO.cpp)、[PlaneArtworkLogic.cpp](PlaneArtworkLogic.cpp) | 文字与图片各自SDO/Logic；typed内容和姿态、PNG/ICRT显示发布、图文变换及主动描边任务 |
| [PlaneMachiningSDO.cpp](PlaneMachiningSDO.cpp)、[PlaneMachiningLogic.cpp](PlaneMachiningLogic.cpp) | 加载零件/结果板独立加工副本，保存来源，按单事务发布并保护原CAD/结果 |
| [PlaneCurveLogic.cpp](PlaneCurveLogic.cpp) | 底图与草图共用的业务编辑、复制及几何引用更新 |
| [PlaneCAM.cpp](PlaneCAM.cpp)、[PlaneDomainInternal.h](PlaneDomainInternal.h) | EC 存取、资源发布、排样快照/提交与领域辅助 |
| [PlaneSDODispatch.h](PlaneSDODispatch.h) | 传输解码、Scene/产品校验、成员绑定与返回序列化 |
| [PlaneSceneBehaviour.cpp](PlaneSceneBehaviour.cpp) | 场景初始化、恢复、完成任务处理与关闭 |

[PlaneGeometry.h](PlaneGeometry.h) 仅保留中性数据辅助和服务解析入口。几何算法已移入独立 [PlaneGeometryServices](../../geometry/PlaneGeometryServices/PlaneGeometryService.cpp) DLL，通过 [IPlaneGeometryService](../../../iCAX-Engine/framework/GeometryServices/PlaneGeometryService.h) 注入；算法位于该插件的 `PlaneGeometryAlgorithms.cpp`、`PlaneGeometryDimensions.cpp`。

混合多段线通过 `capabilities.polylineSegmentTypes` 声明五类曲段。`PlaneDrawing.Create` / `PlaneSketch.Create` 的 `primitive: "polyline"` 可接受连续 `segments`，服务返回单一精确 `polycurve`，产品仍只创建一个对应业务EC。曲段计算、采样和相交验证位于几何服务的 `PlaneMixedCurves.cpp`，没有放入SDO或前端业务层。几何资源保存曲段参数，显示路径为派生数据。新品按当前完整能力契约接入，不提供旧版接口兜底或旧工程迁移承诺。

独立图元由 `capabilities.drawingPrimitives` 声明；`drawingPrimitiveOptions` 提供 `polygon.sides={default:6,min:3,max:64}` 与 `star.tips={default:5,min:3,max:64}`。左侧五个既有主按钮保持直接调用，矩形、圆、圆弧的附属箭头只呈现声明支持的额外图元。选项位于当前SDK绘图状态，不混入工程待应用表单。

`ellipse` / `ellipseArc` 接收中心、两个半轴及旋转角，椭圆弧另有有符号扫过角θ；原位A/B可依次Enter，或A Tab B Enter，θ可为负且绝对值不超过360°。`polygon` 使用中心、外接圆半径R及首顶点方向，`star` 使用中心、R/r且0＜r＜R；边数和星角数均为3～64。`triangle` 输入三个顶点；`bezier` 的交互输入四个控制点；`bspline` 的交互输入4～256个控制点后右键“结束”，默认三次。

椭圆/椭圆弧、贝塞尔和B样条输出精确曲段并保留控制点、节点和权重；三角形、正多边形、星形输出精确闭合顶点。`planarDrawingPrimitives.mjs` / `planarPrimitiveDimensions.mjs` 只负责构造、预览与标注，正式几何仍由服务生成。`planarCurveSegments.mjs` 的连续混合会话和既有 `segments` 契约保持；多义线B样条用“完成本段”，独立B样条用“结束”。

排样使用独立 `iPlaneNesting.dll`，由 [PlaneNestingServices](../../geometry/PlaneNestingServices/PlaneNestingService.cpp) 封装为 [IPlaneNestingService](../../../iCAX-Engine/framework/GeometryServices/PlaneNestingService.h)。中性 DXF 读取复用 `ICadDocumentImportService`。三个服务不读取 Repository、不创建业务实体、不发布工程资源，也不决定撤销和业务版本。

## 正式数据与资源

权威业务状态位于 Scene Repository，使用 typed EC、框架事务、撤销还原与工程保存机制：

| ECS 组件 | 保存内容 |
| --- | --- |
| `CPlaneProjectComponent` | 排样参数、当前新建图层 `DefaultLayerID`、一次性图层目录版本 `LayerCatalogVersion`、业务修订与排样输入修订 |
| `CPlaneObjectComponent` | 名称、备注、数量、单位来源、尺寸、孔数、图层、草图关系、几何版本及资源引用 |
| `CPlaneArtworkComponent` | typed X/Y/AngleDeg/ScaleX/ScaleY、基础物理尺寸、像素尺寸、算法BitmapRef、显示TextureRef和派生材质引用 |
| `CPlaneTextComponent` / `CPlaneImageComponent` | 文字内容/字体/字高，或图片原格式；与通用图文组件组合，不把文字内容放入资源业务JSON |
| `CPlaneMachiningSourceComponent` | typed来源种类、原对象/零件/结果/结果板/placement ID、实例序号、源几何版本/引用和源排样姿态，ReferenceOnly板框标记 |
| `CPlaneDrawingComponent` / `CPlanePartComponent` / `CPlaneStockComponent` | 独立业务角色 |
| `CPlaneLayerComponent` | 图层名称、typed `Color` / `Order`、显示与锁定状态；`MaterialRef` 只保存派生显示材质引用 |
| `CPlaneSketchComponent` | 所属零件/板材、Editing/Ready 状态、编辑基准几何引用 |
| `CPlaneSketchCurveComponent` | 曲线所属草图与编辑中删除标记；曲线本身是稳定 EC |
| `CPlaneSketchBackupComponent` | 取消所需的原曲线 ID、名称、图层、版本及几何/显示引用等 typed 备份 |
| `CPlaneTaskComponent` | 排样状态、输入对象、参数和计算间距快照、错误及结果关系 |
| `CPlaneEditTaskComponent` | 几何任务种类、目标/owner/sketch 关系、状态、错误与结果 ID |
| `CPlaneTaskInputComponent` | 输入对象、角色、数量、几何版本、误差界及准确资源引用；几何任务使用所需字段 |
| `CPlaneResultComponent` | 方案、需求守恒与校验结论、数量、面积、利用率、参数快照 |
| `CPlaneResultBoardComponent` | 物理板、来源库存、使用序号及单板指标 |
| `CPlanePlacementComponent` | 所属结果板、来源零件、实例序号、位置、角度与镜像 |
| `CPlaneUnplacedComponent` | 未排入实例及原因 |

字段见 [PlaneComponents.h](PlaneComponents.h)。资源仅承载原始 DXF、解析几何和显示载荷，使用框架 Embedded 版本资源；不将图层、草图会话、任务或结果关系打包为资源业务对象。源文件/几何使用 `resource.binary`；显示几何使用 `resource.flatbuffer` / ICRG，显示材质使用 ICRM 及框架 `MakeRenderMaterialResource`。报告是 EC 的只读投影。

显示复用 `CRenderInstanceComponent`、`CTransformComponent` 和正式 Scene View；坐标为 XY 平面、毫米。零件定义与排入实例有独立实体身份。

图文与源关系仍是正式EC。原文件和归一PNG使用后台binary资源；前端不允许GET binary。派生显示经通用 `ICRT` FlatBuffer纹理（依赖算法PNG）、`ICRM.base_color_texture_url`、带UV的`ICRG`平面，由同一Scene View与SDK视口显示。文字白色透明字形乘图层色；图片白基色保原色、放z=-.001的参考背景，文字z=+.001。显示纹理嵌入工程并保全依赖，重开不读取原文件。框架资源访问策略和endpoint未改。

未保存操作由框架工程日志恢复，资源版本闭包写入配套 `.ipcam.log.resources`，恢复资源后再回放EC日志。保存成功后以完整工程归档为基线；明确选择“直接关闭”丢弃本次日志和资源恢复对，正常退出或意外中断保留恢复。此机制属于通用Project/ProjectFile/Database层，产品不另存私有工程状态。

## 正式草图生命周期

1. `Open(ownerId)` 只允许零件或板材。首次建立 owner→sketch 与 sketch→curve 的 EC 关系和 typed 备份，不改 owner 的几何、数量或来源。Ready 草图再次打开保留曲线 ID；owner 几何引用在外部改变时重新初始化曲线。
2. `Create/Edit/Remove` 只操作 Editing 草图中的曲线。编辑中删除旧曲线采用删除标记，保留取消所需的身份与引用。底图与草图复用几何服务，业务目标分别校验。
3. `Complete` 从有效曲线构建材料轮廓。校验通过后，以一个事务更新原 owner 的几何版本/显示引用，清理删除曲线与备份，草图转 Ready，并使排样输入修订变化。失败不提交 owner。
4. `Cancel` 恢复原曲线 ID、引用、图层和属性，移除本次新增曲线，owner 几何不动。owner 变旧或锁定后仍可取消，避免会话无法退出。
5. owner 正在编辑草图时拒绝删除；Ready owner 删除时级联释放其草图、曲线和备份 EC。草图与备份参与框架保存、Undo/Redo，不使用前端私有草图 JSON。

## Scene SDO

以下方法限定本产品 Scene 通道；产品或应用通道不能替代。修改命令接受可选字符串 `expectedRevision`，返回简短标识、状态或版本；列表、几何、任务与报告单独查询。

| 方法 | 要点 |
| --- | --- |
| `PlaneScene.ReadState` | iplaneCam 含 drawings/parts/stocks/results/layers/sketches/sketchCurves、currentLayerId、参数与能力；layers 按 order 排序，含 color/isCurrent/default/materialRef；对象含 layerName/layerColor/materialRef；另有 revision 与排样 tasks |
| `PlaneDrawing.Import` / `Create` | 绝对sourcePath；或line/polyline/rect/circle/arc、ellipse/ellipseArc/bezier/bspline、triangle/polygon/star的精确构造参数 |
| `PlaneDrawing.Edit` | move/rotate/scale/copy/mirror/delete/close/dimensions/nodes/array-rectangular/array-polar |
| `PlaneDrawing.ReadGeometry` | 几何引用/版本、curves、snapPoints、dimensions |
| `PlaneText.Create` / `Update` | text、fontFamily、fontSizeMm、x/y/angleDeg；Update同一id；字形栅格化走框架异步任务 |
| `PlaneImage.Import` / `Update` | 绝对sourcePath，归一PNG后台解码；可改widthMm/heightMm/x/y/angleDeg，保持同一EC与原图 |
| `PlaneImage.Vectorize` | id、threshold(0–255)、invert、minAreaMm2、simplifyMm；用户主动触发异步生成独立加工曲线，原图保留 |
| `PlaneMachining.LoadPart` | partId、可选x/y；精确几何独立复制到底图并保存来源，不改零件CAD |
| `PlaneMachining.LoadResult` | boardId（模型results.boards.id）；按固化placement几何与姿态生成独立加工副本及不可选参考板框 |
| `PlanePart.Import` | 独立 DXF、名称与需求数量 |
| `PlaneStock.Import` / `CreateRectangle` | 板材 DXF 或 width/height/quantity |
| `PlaneObject.Update` / `Remove` | 对象类型与 ID；结果仅删除父方案并级联其结果子实体 |
| `PlaneSketch.Open` | ownerId；返回 sketchId、ownerId |
| `PlaneSketch.Create` / `Edit` / `Remove` / `ReadGeometry` | Create 带 sketchId，其余用曲线 EC id |
| `PlaneSketch.Complete` / `Cancel` | 草图 id；Complete 可带 async:true |
| `PlaneLayer.Create` / `Update` | 名称、严格 `#RRGGBB` 颜色、visible/locked；Create 自动分配顺序，不设八层上限 |
| `PlaneLayer.SetCurrent` | 图层 id；作为新建、导入和新增草图曲线的当前图层，拒绝锁定层 |
| `PlaneLayer.Assign` | 对象 ids 与 layerId；源层、目标层及全部对象先校验后单事务归层 |
| `PlaneLayer.Remove` | id；占用层或当前层须给 targetLayerId，单事务迁移对象、草图备份与当前层；拒绝最后一层及锁定源/目标层 |
| `PlaneTask.Read` / `Cancel` | 按 taskId 查询或取消草图完成/阵列几何任务 |
| `PlaneNesting.SetParameters` | partGap、stockMargin、rotationStepDeg、timeLimitSeconds、allowMirror |
| `PlaneNesting.Run` / `Cancel` / `ReadTask` | 排样启动、取消与查询 |
| `PlaneNesting.ReadReport` / `PlaneLayout.ReadReport` | 按 resultId 查询 EC 报告投影 |

尺寸/节点编辑必须带完整 `expectedGeometryRef`，按 URL 与版本核对；节点编辑当前只支持单一直线/多段线，保持点数。复制产生新 EC；阵列保留源 EC，第 0 项不重复创建，其余一个事务创建，总数上限 500。矩形阵列用 rows/columns/spacingX/spacingY，环形用 count/cx/cy/angleDeg；镜像用 axis=x/y 与可选 cx/cy。

图文和加工加载仅接受底图业务请求；传入sketchId/ownerId明确拒绝，不把所属CAD草图请求静默改成全局对象。文字/图片仍通过 `PlaneDrawing.Edit` 走已有六种整体变换及删除，但由typed图文逻辑处理；不进入曲线算法或提供nodes/close/array。`ReadGeometry`返回空curves/snapPoints/dimensions，防止参考平面误作为矩形刀路。

描边按真实像素边界保留孔，RDP简化后复用OCCT求交并核包含关系/方向；自交、孔关系变化或验证超预算均回退原像素轮廓。简化误差在geometry文档及typed对象同步记录，move/copy/mirror/array保留，scale乘factor，与重新采样界取max。图片矢量化、复制和阵列保持typed image-vector来源。

加载结果从placement持久化的源GeometryRef读取精确曲线，按既有内核语义Rotate→Mirror(y取负)→Move；不从可能已改动的CAD重建，也不使用离散显示paths冒充精确曲线。每实例保存part/result/board/placement来源和姿态。参考板框的原始/复制/阵列对象在首次提交事务中即不可选，不能依赖下一次Pump补救。

## 彩色图层与材质

新工程初始化与当前 Laser3D 相同的八色：`#22C55E`、`#06B6D4`、`#3B82F6`、`#F59E0B`、`#14B8A6`、`#84CC16`、`#8B5CF6`、`#EAB308`。可以继续新增、改名、改色或删除，不设八层上限。`DefaultLayerID` 沿用已有字段，表达当前新建图层，不另建第二份当前层状态。

新工程初始化8个色层，`LayerCatalogVersion`记录初始化完成，用户删除的初始层不会在重开后重新长出。新建对象和导入对象使用当前层，复制/阵列继承源层，首次打开草图的原有曲线继承owner层，新增草图曲线使用当前层。文字换层或图层改色重建带原TextureRef的文字材质；图片始终白基色，图层颜色不染原图。

`Color` 是正式 EC 权威，`MaterialRef` 是按颜色生成的派生 ICRM 显示载荷。创建、改色、归层、删除迁移和取消草图都通过 `CRenderInstanceComponent.MaterialResourceID/Version` 接入现有 View/SDK 显示，不在前端维护另一份颜色权威。改色仅发布新显示材质并调整引用，不重建解析几何、显示几何或增加 GeometryVersion。

Layer SDO 在正式事务中同步材质和可见/可选状态。Undo/Redo 恢复持久的层与材质引用；由于 `Visible/Selectable` 是非持久显示字段，场景生命周期和 Behaviour 另按 EC 比较差异后恢复。稳定帧不写入，不重复发布材质，不增加用户撤销步骤；`ReadState` 等查询保持只读。软删除草图曲线始终隐藏且禁选，图层改色不能把它复活；Cancel 明确恢复删除标记和对应层的显示。

源层锁阻止编辑/归层，目标层锁阻止接收归层或迁移，当前层锁阻止新增/导入。删除接收层与源层必须不同，且至少保留一层。当前层和占用层可显式迁移后删除，草图备份同步迁移，避免 Cancel 恢复到已删除层。图层操作不修改几何引用、数量、排样参数或输入签名；异步几何发布前仍重新检查层锁。

## Task 与生命周期

后台复用 `iCAX::Tasks::Run` 与取消令牌，仅捕获中性快照和服务。`PlaneNesting.Run` 始终启动排样任务；`PlaneSketch.Complete` 和两种阵列编辑在 `async:true` 时返回 Running/taskId，由 `PlaneTask.Read` 查询。DXF Import 当前仍同步，不把前端 Promise 等同于后台解析。

Scene Behaviour 完成处理后才提交业务。几何任务核对输入引用/版本；草图完成另核 owner 基准和有效曲线 ID 集。过期输入标 Stale；取消/失败/过期均不覆盖 owner。提交前复核对象与全部有效草图曲线层锁。终态几何任务释放 Work 和计算输出并从运行态移除，EC 任务及输入历史仍可查。

场景关闭/组件销毁时取消并等待任务；工程重开不恢复线程或 SDK 句柄，遗留 Running 记录转 Interrupted。新增场景、基线加载和启用由 Behaviour 恢复运行态/二维显示类型。查询不初始化场景、不推进任务或提交结果。

排样 C ABI 不能中断已执行的 solve，进入后须等待返回并丢弃取消结果；几何材料构建/阵列循环有协作取消检查。Repository 事务不自动回滚资源库写入，业务校验须先于资源发布。

## 几何与导入边界

- DXF 为绝对本地路径，不超过 64 MiB；支持 LINE、含 bulge 的 LWPOLYLINE、CIRCLE、ARC，其他实体明确拒绝。
- 支持已知 INSUNITS 的英寸/毫米/厘米/米；缺失或 unitless 必须显式 units，检查 XY 平面、挤出方向和有限数值。
- 底图和编辑中草图允许开放曲线；零件/板材导入及 Complete 要求一个闭合外轮廓和内孔。分离外轮廓、嵌套材料岛明确拒绝。
- 解析闭合/重叠/相交检查、离散和尺寸描述在几何服务。排样用多边形快照，弦高界不超过 0.05 mm，另记连接误差；计算间距加入保守裕量，源曲线不改。
- 产品用 1°～360° 离散步进、正数且不超过 120 秒的预算。报告分别保存用户参数、实际计算间距和 PolygonMaterial 范围；几何合法、需求完成、紧凑程度分别判断。
- DXF的ELLIPSE/SPLINE实体导入仍未实现，独立绘制不代表支持对应DXF实体。旧POLYLINE、BLOCK/INSERT、完整DXF图层、修剪/延伸/偏移/圆角/倒角/连接/打断、混合曲线控制点编辑、手动布局与余料管理未纳入本轮实现。

## 构建与验证

[iPlaneCAM.vcxproj](iPlaneCAM.vcxproj) 依赖独立几何/排样服务项目，使用统一 x64 Debug/Release 输出与中间目录。[产品清单](../../../apps/iplane-cam/product.manifest.json) 注册三个服务及产品组件、Behaviour、SDO；发行脚本负责部署，不能把算法 DLL 当作 iCAX Module。

当前测试入口见 [iPlaneCAM tests](../../../tests/icax-plugins/product/iPlaneCAM)。[ArtworkServiceTests.inc](../../../tests/icax-plugins/product/iPlaneCAM/ArtworkServiceTests.inc) 验证真实图像、字形、轮廓与简化拓扑；[ArtworkBusinessTests.inc](../../../tests/icax-plugins/product/iPlaneCAM/ArtworkBusinessTests.inc) 验证typed图文、来源、变换、任务过期和加工副本；[ArtworkTextureTests.inc](../../../tests/icax-plugins/product/iPlaneCAM/ArtworkTextureTests.inc) 验证ICRT及原资源访问策略。已有草图、图层、混合多段线、排样与历史流程共同回归。

打包浏览器实际检查纹理像素与方向、画布拾取、图文编辑、描边及孔、源CAD与加工副本隔离、异步Undo/Redo、焦点/选区/监听器/滚动和相机。保存后在独立进程打开与同进程重开分别记录；同进程缓存不能代替独立进程资源恢复验证。

最终包核验读取本轮报告、当前源码与包内资产哈希、全部运行时DLL、构建日志及原生日志；历史r8/r9/r10报告不代替当前包的证据。独立桌面CEF、系统文件选择器、完整Release与安装器仍单独记录。启动与范围见 [产品文档入口](../../../../doc/iPlaneCAM/README.md)，完整边界见 [架构与领域设计](../../../../doc/iPlaneCAM/架构与领域设计.md)。
