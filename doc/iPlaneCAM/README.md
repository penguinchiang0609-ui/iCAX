# iPlaneCAM 开发验收版

当前交付为 **0.1.0 Debug r13**：[启动 iPlaneCAM](../../output/releases/iPlaneCAM-0.1.0-Debug-20261117-r13/Start-iPlaneCAM.cmd)。最终包核验已通过；交付目录独立，未替换正在绘制的r12用户窗口。使用新功能前请保存当前工程，再从新入口启动。

本轮新增七种独立图元、左侧分组箭头菜单和配合坐标输入的原位尺寸；混合多义线、可编辑文字、参考图片、主动转矢量及加工副本继续保留。名称仍使用“底图”。当前证据汇集于 [r13验收目录](../../output/tests/iplane-cam-shapes)，最终以本轮汇总绑定的精确包目录、源码/二进制哈希及报告范围为准。[r12汇总](../../output/tests/iplane-cam-worktable/acceptance.json) 为历史基线，不能代替r13验证。本新品不做旧工程或旧API兼容。

## 绘制与编辑

顶层为“底图 / 下料”。底图是直接绘制、编辑加工轨迹几何的工作台。零件与板材分别拥有CAD草图，进入草图后复用同一画布和绘制工具；完成校验后更新原对象，取消恢复原曲线。下料保持左侧零件、中央板材与布局、底部板材/结果、右侧参数。

下料中的单个零件或指定结果板可“加载到底图”。加载创建保留来源的独立加工副本，二次编辑不修改源CAD或排样结果；结果板按保存的placement几何和姿态复制，板框是不可选参考背景。没有从底图生成零件或板材的入口。

左侧窄工具栏按“线条 / 曲线 / 形状”分组，原直线、多段线、矩形、圆、圆弧主按钮可直接点击。矩形箭头展开三角形/正多边形/星形，圆箭头展开椭圆，圆弧箭头展开椭圆弧/贝塞尔/B样条；右键主按钮也可展开。菜单在视口边缘翻转，超高时内部滚动；Esc或点击外部关闭不增加画布点。文字、图片、橡皮擦和对象面板开关继续保留。对象面板初始收起，折叠保留列表节点、宽度和滚动，下料零件列表仍常驻。

独立图元在底图和零件/板材所属草图中共用SDK交互，正式输出由注入的原生几何服务生成，预览采样不替代精确几何：

| 独立图元 | 构造与输入 |
| --- | --- |
| 椭圆 | 中心、第一半轴端点、另一半轴；标注A/B支持依次Enter或A Tab B Enter，方向可由鼠标确定 |
| 椭圆弧 | 椭圆构造后指定终点方向或有符号扫过角θ；θ非零且绝对值≤360°，负数为顺时针 |
| 三角形 | 三个精确顶点，原生拒绝退化共线三角形 |
| 正多边形 | 中心、外接圆半径R和首顶点方向；右栏边数3～64，默认6 |
| 星形 | 中心、外半径R和内半径r，须0＜r＜R；右栏星角数3～64，默认5 |
| 贝塞尔 | 起点、两个控制点、终点，共四点，保存精确三次曲线 |
| B样条 | 4～256个控制点，右键“结束”，保存精确三次B样条 |

无效输入保留已接受的构造前缀。边数/星角数是当前绘图选项，独立于工程待应用表单；不会使保存被误判为尚有未提交参数。椭圆/椭圆弧、贝塞尔和B样条保存精确曲段，三角形、正多边形和星形保存精确闭合顶点。

文字在画布点击定位后，由右栏确认创建；可再次编辑同一对象的内容、字体和字高。参考图片支持PNG/JPEG/BMP/TIFF，可选择、定位、调整尺寸/角度及整体变换。图像和字形经框架Scene View显示，工程内保存必要资源，重开不依赖原文件。

图片在用户主动设置阈值、反色、最小面积和精简精度并执行转换时生成加工曲线，原图保留。转换提取像素边界与内孔；简化若造成自交或改变包含关系，回退原轮廓。保守误差随之后的变换、复制和阵列保存。

多段线可连续切换五种曲段，一次结束后保存为一个业务EC和一个精确polycurve：

| 曲段 | 快捷键 | 输入步骤（起点继承上一段终点） |
| --- | --- | --- |
| 直线 | L | 指定终点 |
| 圆弧 | A | 指定经过点、终点 |
| 椭圆弧 | E | 指定中心、另一半轴半径点、终点方向；半轴点所在侧决定顺/逆时针 |
| 贝塞尔 | B | 指定两个控制点、终点 |
| B样条 | S | 连续指定控制点，含起点至少4点后右键“完成本段”，可继续下一段 |

多段线右键“切换曲段”选择类型，当前项带勾选；支持悬停或点击展开及场景边缘翻转。固定曲段未完成时禁用其它类型并给出补点说明。Ribbon、右侧选项与快捷键是辅助。鼠标与坐标输入可交替使用；椭圆弧鼠标终点投影到当前椭圆，输入坐标则校验是否在椭圆上。Backspace/Ctrl+Z退回尚未提交的绘制输入；项目正式变更使用框架历史。

右键保持“闭合 / 取消 / 结束”，多段线中的待完成B样条另有“完成本段”。Esc先收子菜单，再收主菜单，不取消绘制；点击菜单外画布只收菜单，不加点。L、R和夹角协助绘制，夹角按上一段精确终点切向计算；标注重叠时可缩放相机查看。圆R、矩形W/H、独立圆弧R/α/θ仍可原位输入。

精确曲线参数、控制点、B样条节点及权重保存到几何资源，显示与排样另行采样。混合曲线支持整体移动、复制、旋转、镜像、缩放、矩形/圆周阵列及闭合；单段尺寸、混合曲线控制点暂只读。零长度直线段明确拒绝，不作为已完成曲段保留。

## 框架与数据

| 层次 | 入口和职责 |
| --- | --- |
| 公共二维SDK | [编辑会话](../../src/iCAX-UI/SDK/Cad/planarEditorSession.mjs)、[独立图元](../../src/iCAX-UI/SDK/Cad/planarDrawingPrimitives.mjs)、[图元尺寸](../../src/iCAX-UI/SDK/Cad/planarPrimitiveDimensions.mjs)、[混合曲段](../../src/iCAX-UI/SDK/Cad/planarCurveSegments.mjs)、[预览](../../src/iCAX-UI/SDK/Cad/planarDrawingPreview.mjs)，管理交互草稿、数值计算和呈现 |
| 产品前端 | [entry.mjs](../../src/apps/iplane-cam/webpage/entry.mjs)、[drawingCommands.mjs](../../src/apps/iplane-cam/webpage/drawingCommands.mjs)，适配当前业务目标与SceneProxy |
| 原生业务与EC | [产品说明](../../src/iCAX-Plugins/product/iPlaneCAM/README.md)、[PlaneComponents.h](../../src/iCAX-Plugins/product/iPlaneCAM/PlaneComponents.h)，SDO/Logic负责业务校验、事务、版本、任务和发布 |
| 几何服务DLL | [IPlaneGeometryService](../../src/iCAX-Engine/framework/GeometryServices/PlaneGeometryService.h)、[混合曲线算法](../../src/iCAX-Plugins/geometry/PlaneGeometryServices/PlaneMixedCurves.cpp)，处理中性几何快照 |
| 图文服务DLL | [IPlaneArtworkService](../../src/iCAX-Engine/framework/GeometryServices/PlaneArtworkService.h)由独立PlaneGeometryServices DLL实现，提供WIC解码、GDI字形和像素轮廓提取，不访问EC或资源库 |
| 通用呈现 | [右键菜单](../../src/iCAX-UI/SDK/Cad/planarContextMenu.mjs)、[工具栏](../../src/iCAX-UI/SDK/Cad/planarToolPalette.mjs)、[SDK纹理](../../src/iCAX-UI/SDK/Viewport/README.md)；ICRT纹理、ICRM材质和ICRG网格复用既有FlatBuffer资源通道 |
| 排样服务与内核 | [IPlaneNestingService](../../src/iCAX-Engine/framework/GeometryServices/PlaneNestingService.h)注入，封装独立[iPlaneNesting](../../src/iCAX-Engine/foundation/iPlaneNesting/README.md) |

正式业务数据使用EC。服务不访问Repository或管理项目历史；资源保存源文件、精确几何及派生显示。排样、阵列、草图完成、图像解码/描边、文字栅格化和加工副本加载通过框架Task运行；输入或图层版本变化使任务过期，防止迟到结果覆盖当前对象。源文件读取仍同步执行。

保留图层色栏、底部命令/日志选项卡和中央状态栏。普通刷新不替换画布、命令输入和事件监听，并保护焦点、文本选区、多栏最新滚动、相机及未完成绘制。

## 本轮验证与历史基线

r13最终核验通过：**1773次原生调用、16套154组浏览器检查、10项ICRT协议检查**，476项包库存及69个测试运行时DLL逐项绑定源码/产物。新图元专项包含2次独立启动、9条阶段检查、31次实际资源GET及8个真实场景对象版本绑定；154按顶层逻辑检查计数，不重复累计阶段检查。未改动的框架模块沿用并重新核对182项测试及相同二进制的绑定，另有2项既有禁用用例。最终证据见 [验收汇总](../../output/tests/iplane-cam-shapes/acceptance.json)。

新增共享SDK尺寸构造16项、工具栏菜单9项专项通过。实际界面见 [独立图元](../../output/tests/iplane-cam-shapes/package-shapes/reopen/standalone-shapes-fresh-reopened.png) 和 [左侧曲线组菜单](../../output/tests/iplane-cam-shapes/package-worktable-visual-r13/05-left-curve-group-menu.png)。

以下为**r12历史基线**：1140次原生业务调用、15套152组浏览器检查、182项框架测试、10项ICRT协议检查。框架另有2项既有禁用测试，未计入182项。独立进程专项按丢弃/恢复两个场景计数，内部包含4次独立启动与32项阶段检查，不与152重复累计。r12的474项包库存、69个运行时DLL及10份构建日志仅绑定r12包，不计作r13通过证据。

r12构建、原生调用、打包浏览器检查及源/包哈希由其历史交付汇总绑定。工作台专项检查真实纹理像素和方向、画布拾取、图文编辑、描边及孔、源CAD与加工副本隔离、异步Undo/Redo、焦点/选区/监听器/滚动和相机。保存后独立进程恢复与同进程重开分别验证；同进程缓存不代替资源持久化检查。

r12已验证带孔材料副本的变换、复制及阵列，几何服务重建材料边界和孔关系，EC孔数同步保存；撤销重做、保存恢复后仍一致，源CAD不变。r12历史界面见 [文字与图片](../../output/tests/iplane-cam-worktable/package-worktable-visual-r12/02-editable-text-and-color-image.png) 和 [结果板加工副本](../../output/tests/iplane-cam-worktable/package-worktable-visual-r12/04-loaded-board-processing-copies.png)。

材料检查补充求交完成状态、真实参数端点判断及线段与有理曲线的Bernstein检查，拒绝近端再次穿越。正常近线曲段、单段闭合贝塞尔/B样条仍可作为材料；采样误差0.05mm及连接误差计入排样计算间距。

下表均为r12历史证据，不代替r13当前包的核验。

| r12证据 | 路径 |
| --- | --- |
| 最终包核验 | [汇总](../../output/tests/iplane-cam-worktable/acceptance.json)、[核验脚本](../../output/tests/iplane-cam-worktable/verify-final.ps1) |
| 原生完整流程 | [原生日志](../../output/tests/iplane-cam-worktable/native-test.log)、[测试源码](../../src/tests/icax-plugins/product/iPlaneCAM) |
| 工作台真实界面 | [打包浏览器报告](../../output/tests/iplane-cam-worktable/package-worktable/worktable-native-browser-report.json) |
| 鼠标曲段菜单 | [报告](../../output/tests/iplane-cam-worktable/package-menu/polyline-menu-native-browser-report.json) |
| 独立进程恢复 | [报告](../../output/tests/iplane-cam-worktable/package-fresh-process/fresh-process-native-browser-report.json)，分别验证明确丢弃及真实进程退出后的未保存恢复，并重新描边使用恢复的图片 |
| ICRT协议 | [报告](../../output/tests/iplane-cam-worktable/render-texture-resource-report.json)，真实原生PNG及截断/非法协议检查 |
| r12功能回归 | 同一历史验收目录下package-*报告，覆盖绘制、切向角、闭合、动态输入、尺寸、独立圆弧、草图、图层、命令日志和完整排样流程 |

浏览器连接真实原生后端，脚本提供文件路径；AppShell专用套件使用受控传输数据，单独记录。独立CEF桌面实操、系统文件选择器、完整Release与安装器不在本次通过范围。产品与框架为x64 Debug，排样内核为x64 Release。没有替换已打开的用户窗口。

## 当前范围

DXF支持LINE、ARC、CIRCLE、LWPOLYLINE（含bulge），缺失单位须明确选择。零件与板材支持不规则外轮廓和孔；排样报告标明PolygonMaterial、离散误差和计算间距。

DXF的ELLIPSE/SPLINE实体导入仍未提供；独立绘制这些曲线不代表支持对应DXF实体。旧POLYLINE、BLOCK/INSERT、文字/标注/填充、原DXF图层映射、图层线型、节点增删/拖动、混合曲线控制点编辑、修剪/延伸/偏移/圆角/倒角/连接/打断、完整约束求解、手动排样与余料计算仍未提供。工艺与NC保留为独立扩展。

可编辑文字仍是参考对象，尚无文字转加工轮廓。图片转换是黑白阈值轮廓提取，图片上限64MiB/1600万像素；不包含多色、中心线或照片语义描摹。

r9布局和r8图层报告为历史参考：[r9](../../output/tests/iplane-cam-bottom-tabs/acceptance.json)、[r8](../../output/tests/iplane-cam-layers/acceptance.json)，不代替当前包的测试证据。
