# iPlaneCAM

当前交付为 **0.1.0 Debug r13**：[启动 iPlaneCAM](../../../output/releases/iPlaneCAM-0.1.0-Debug-20261117-r13/Start-iPlaneCAM.cmd)。本轮1773次原生调用、16套154组浏览器检查、10项ICRT协议检查及完整包核验通过，精确证据见 [验收汇总](../../../output/tests/iplane-cam-shapes/acceptance.json)。另有共享SDK尺寸构造16项、工具栏菜单9项专项通过；未改动的框架模块复核其182项测试及二进制绑定。

本轮新增独立椭圆、椭圆弧、贝塞尔、B样条、三角形、正多边形和星形，配合左侧分组箭头菜单及原位尺寸输入；混合多义线、文字图片、主动转矢量和独立加工副本继续保留。名称继续使用“底图”。本轮证据位于 [r13验收目录](../../../output/tests/iplane-cam-shapes)，最终版本与验证范围见 [开发验收入口](../../../doc/iPlaneCAM/README.md)。[r12交付汇总](../../../output/tests/iplane-cam-worktable/acceptance.json) 仅为历史基线；独立交付目录不替换正在绘制的用户窗口。
## 界面与编辑

产品由唯一 AppShell 入口加载，复用 `createWorkbench`、SDK Proxy、视口、Forms、Ribbon 与项目历史。顶层只有底图和下料：下料左侧零件、中央板形与布局、底部板材/结果、右侧参数；草图编辑使用当前视口和上下文命令。

底图是加工编辑工作台，保存可直接加工的曲线、可编辑文字与参考图片。下料中可把单个零件或指定排样结果板“加载到底图”，生成保留来源的独立加工副本，再做二次编辑；原零件和排样方案保持不变。结果板框是不可选的参考背景。零件及板材仍在下料入口分别创建；“编辑草图”更新原CAD轮廓，取消恢复进入时的草图。没有从底图生成零件/板材的命令。

场景左侧窄工具栏按“线条 / 曲线 / 形状”分组，直线、多段线、矩形、圆、圆弧主按钮仍可直接点击。矩形旁的小箭头展开三角形、正多边形、星形；圆旁展开椭圆；圆弧旁展开椭圆弧、贝塞尔、B样条。点击箭头或右键主按钮均可展开，弹出菜单支持边界翻转和内部滚动；Esc或点击外部关闭时不向画布落点。工具栏复用公共SDK控件及图标，文字、图片、橡皮擦和对象面板开关仍保留。对象面板默认收起，下料零件清单仍常驻；折叠保留列表DOM、宽度与滚动。橡皮擦只删除命中的可编辑对象，所属草图走自己的删除通道。

Ribbon按线条、曲线、形状、修改、变换与阵列、图层、辅助与视图分区组织；左侧工具菜单和画布右键菜单复用中性SDK。原生 capabilities 和当前选择共同决定命令可用性。

底部辅助区由 AppShell 提供“命令 / 日志”选项卡，初始显示命令，可拖动调整高度。切换仅隐藏对应面板，保留命令输入节点、原始文本/选区及日志滚动，不中断绘制。单位、当前工具、捕捉、正交、网格位于中央场景下方的状态栏；右侧专用于对象与当前操作参数。

产品通过 `getBottomDockDefinition` 声明命令页，`mountBottomDockPanel` 增量更新命令内容；日志由框架拥有。共享工作台的可选 `renderSceneStatusBar` 放置场景状态栏。两处命令输入共用既有绘图会话与键盘处理，场景/底部异步更新均不重建画布。

- 绘制：直线、多段线、矩形、圆、圆弧，以及独立椭圆、椭圆弧、贝塞尔、B样条、三角形、正多边形和星形；混合多义线仍连续支持五种曲段。底图与零件/板材所属草图共用绘制交互，支持吸附、正交、坐标与原位尺寸输入。
- 编辑：平移、复制、旋转、镜像、缩放、删除、开放折线闭合、尺寸修改；直线/多段线既有节点的 X/Y 修改。
- 阵列：矩形、圆周阵列，保留原对象，新增其余实例，一次事务提交。
- 图层：视口右侧竖向色层栏，初始8色且可继续新增；选中对象点色归层，无选择设当前。可折叠，管理中改名、配色、显示/锁定及迁移删除。
- 下料：独立零件与库存、矩形/异形板及孔、数量和排样参数、结果板与报告。
- 图文：文字工具点击画布定位，右栏输入并确认创建；内容、字体和字高可在同一EC上重编辑。图片支持PNG/JPEG/BMP/TIFF导入、位置/尺寸/角度及整体变换，保存后不依赖原文件。
- 图片转加工曲线：用户主动设置黑白阈值、反色、小轮廓过滤面积与精简精度后生成，保留原图；追踪真实像素边界与内孔。简化若产生自交或改变孔包含关系，回退原始像素轮廓，保守误差在后续变换/阵列中继续保存。

多段线绘制时右键“切换曲段”展开五种类型，当前类型显示勾选，鼠标悬停或点击均能展开。光标靠边时子菜单向左或向上翻转，保持在场景内。固定曲段未完成时，其它类型禁用，并直接显示补点说明；已输入点不丢失。右键保留“闭合 / 取消 / 结束”；多段线中的B样条有待输入点时增加“完成本段”。Esc先收子菜单，再收主菜单，不取消绘制；点击菜单外画布只收菜单，不添加点。

圆 R、矩形 W/H、圆弧 R/α/θ 可在标注处输入；选中图形可查看或双击编辑适用尺寸。多段线预览显示 L 和相对上一段前进切向的只读夹角，直行 0°、垂直 90°、折返 180°。首段或零长段不显示夹角；重叠通过相机缩放查看。

独立椭圆依次确定中心和半轴A/B，可用“A Enter、B Enter”或“A Tab、B Enter”配合鼠标方向完成。独立椭圆弧另输入有符号扫过角θ，正数为逆时针、负数为顺时针，非零且绝对值不超过360°。正多边形用中心、外接圆半径R和首顶点方向构造，右栏边数为3～64，默认6；星形用中心、外半径R与内半径r构造，须0＜r＜R，右栏星角数为3～64，默认5。三角形输入三个顶点；贝塞尔输入起点、两个控制点和终点；独立三次B样条输入4～256个控制点后右键“结束”。无效尺寸保留已接受的构造点，边数/星角数属于短期绘图选项，不占用工程待应用表单草稿。

正式几何由原生服务生成：椭圆/椭圆弧、贝塞尔和B样条保存精确曲段参数，三角形、正多边形和星形保存精确闭合顶点；预览采样不替代正式曲线。独立B样条的“结束”与混合多义线中B样条的“完成本段”是两个操作。

优先使用光标旁的右键菜单，Ribbon“当前曲段”和右栏为辅助入口，L/A/E/B/S仍可选用。圆弧由当前端点、经过点、终点确定；椭圆弧以当前端点为第一半轴端点，依次输入中心、另一半轴半径点、终点方向；贝塞尔输入两个控制点和终点。B样条至少4点（含起点）后右键“完成本段”，可继续下一段B样条；也可切其它类型自动完成当前有效B样条。空格仍可完成本段，Backspace/Ctrl+Z退回未提交输入，均无需强制使用。

椭圆弧的另一半轴点所在侧决定顺/逆时针，支持小弧和大弧。整条混合多段线只有一个EC和一个精确 `polycurve`，保存各段解析参数、控制点、节点及权重；显示采样不替代原曲线。整体变换、阵列、闭合、草图材料校验与保存重开均保留曲段类型。夹角采用上一段精确终点切向；混合曲线单段尺寸和控制点暂只读。

## 分层与源码

| 层 | 入口与职责 |
| --- | --- |
| 公共二维交互 | [SDK/Cad](../../iCAX-UI/SDK/Cad/planarEditorSession.mjs) 提供编辑会话、动态输入、尺寸、预览、菜单和节点模型；[独立图元](../../iCAX-UI/SDK/Cad/planarDrawingPrimitives.mjs) 与[图元尺寸](../../iCAX-UI/SDK/Cad/planarPrimitiveDimensions.mjs) 管理构造输入；[混合曲段](../../iCAX-UI/SDK/Cad/planarCurveSegments.mjs) 保留连续会话与精确切线；[SDK/Forms](../../iCAX-UI/SDK/Forms/domPatch.mjs) 提供局部 DOM 更新 |
| 产品前端 | [entry.mjs](webpage/entry.mjs)、[editingTarget.mjs](webpage/editingTarget.mjs)、[sketchOperations.mjs](webpage/sketchOperations.mjs) 将公共交互接到当前业务目标和 SceneProxy |
| 图层呈现与适配 | [SDK/Forms/layerPalette.mjs](../../iCAX-UI/SDK/Forms/layerPalette.mjs) 提供中性图层栏与管理界面；[layers.mjs](webpage/layers.mjs) 负责原生接口、排队提交、保存flush与关闭保护 |
| 工具栏与右键菜单 | [planarToolPalette.mjs](../../iCAX-UI/SDK/Cad/planarToolPalette.mjs)、[planarContextMenu.mjs](../../iCAX-UI/SDK/Cad/planarContextMenu.mjs) 提供中性保留式控件，产品声明工具/菜单并业务调度 |
| 图文前端与显示 | [artworkOperations.mjs](webpage/artworkOperations.mjs) 只适配表单、任务与选择；[SDK纹理](../../iCAX-UI/SDK/Viewport/renderTexture.mjs) 经原资源通道读取ICRT，使用ICRG UV网格显示，无产品私有overlay |
| 业务逻辑 | [原生产品](../../iCAX-Plugins/product/iPlaneCAM/README.md) 按 Drawing/Part/Stock/Object/Nesting/Layer/Sketch/Task 拆分 SDO 与逻辑，管理事务、版本、校验和发布 |
| EC 数据 | [PlaneComponents.h](../../iCAX-Plugins/product/iPlaneCAM/PlaneComponents.h) 保存图形、草图/曲线/备份、图层、需求、库存、任务与结果关系 |
| 几何服务 DLL | [IPlaneGeometryService](../../iCAX-Engine/framework/GeometryServices/PlaneGeometryService.h)、[PlaneGeometryServices](../../iCAX-Plugins/geometry/PlaneGeometryServices/PlaneGeometryService.cpp)：接收中性快照，创建/变换/校验几何，不访问 Repository |
| 图文算法服务 | [IPlaneArtworkService](../../iCAX-Engine/framework/GeometryServices/PlaneArtworkService.h)、[实现](../../iCAX-Plugins/geometry/PlaneGeometryServices/PlaneArtworkService.cpp)：同一独立服务DLL内用Windows WIC解码/GDI栅格化、提取及简化像素轮廓；不访问EC、资源和历史 |
| 排样 | [IPlaneNestingService](../../iCAX-Engine/framework/GeometryServices/PlaneNestingService.h) 封装独立 [iPlaneNesting](../../iCAX-Engine/foundation/iPlaneNesting/README.md)；算法 DLL 保持独立 |

ServiceProvider 负责注入服务。SDO 是业务调度入口，计算实现位于服务 DLL；EC 是正式数据，资源仅保存源文件、几何和显示内容。命令返回简洁结果/标识，模型和列表由查询获取。

排样、完成草图、阵列、文字栅格化、图片解码/矢量化及加工副本加载使用框架Task。工作线程只处理中性快照，Scene发布前重验身份、版本、引用、取消状态及源/目标图层版本和锁。输入或图层改变统一转Stale，失败、取消和过期结果不覆盖正式对象。DXF及图片源文件的后端读取仍同步，不能把全部IO称为后台任务。

前端任务跟踪保留项目所有权，项目切换和暂时查询失败不视为任务已完成。普通刷新复用输入、画布和监听器，并保留焦点、文本选区、多栏滚动、相机与选择。

## 范围与验收

DXF 当前接通 LINE、ARC、CIRCLE、LWPOLYLINE（含 bulge），已知 INSUNITS 按源单位换算，缺失时要求明确选择。源文件嵌入工程，材料检查保留外轮廓及内孔；报告标明 PolygonMaterial，并记录曲线离散误差及保守计算间距。

DXF的ELLIPSE/SPLINE实体导入仍未实现，独立绘制这些曲线不代表支持对应DXF实体。旧POLYLINE、BLOCK/INSERT、文字/标注/填充、DXF原图层映射、图层线型、修剪/延伸/偏移/圆角/倒角/连接/打断、节点增删/拖动、完整约束求解和手动排样仍未实现。工艺与NC留给独立扩展。

可编辑文字是参考对象，尚无文字转加工轮廓；新增文字能力不等于支持DXF TEXT/MTEXT。图片矢量化是黑白阈值轮廓提取，包含内孔，不是多色分层描摹、中心线提取或照片语义识别。图片最大64MiB/1600万像素，超限明确拒绝；不做旧工程/API兼容。

本轮原生/真实浏览器、包资产哈希及历史基线统一记录在 [开发验收入口](../../../doc/iPlaneCAM/README.md)。浏览器连接真实原生后端与独立 CEF 桌面实操分开记录；不把脚本提供文件路径当作系统文件选择器通过。产品/框架当前构建为 x64 Debug，内核为 x64 Release。
