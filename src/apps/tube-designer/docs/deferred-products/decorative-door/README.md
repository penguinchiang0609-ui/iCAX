# 雕花装饰门：归档时规格与实现

模板 `decorative-door`，描述符与内部模型版本 `1.0.0`。原目录 `templates/product/decorative_door` 已暂停运行入口；[reference](reference/template.py) 保留门面生成器、图案资源、原 README 和图标。现有实现是参数化门面、板件与表面加工声明，**不是完整门窗结构系统或雕刻 CAM，所有输出始终 `ncReady=false`**。

默认双开，总宽 1800 mm、高 2200 mm、板厚 40 mm、扇缝 4 mm；镜像铣线、整面布局、8 条竖线、平底槽宽 8 mm、正面深度 6 mm、边框保留 100 mm，开启五金区域保护。完整 45 项参数、默认值、选项、约束与条件见 [参数附录](parameters.md)、[原描述符](reference/template.json)。材料和表面处理只是规格备注，不能证明复合板结构或渲染纹理。

## 分扇和图案布局

坐标 X 为门宽，Z 为高度，Y 为板厚。单开 1 扇；双开 2 扇等宽；子母 2 扇，主扇比例 0.51–0.85 并可指定侧；多扇 2–6 扇等宽。总宽扣除扇间缝再分配，单扇不读取多扇构图、扇缝与比例草稿。安全装饰区必须给边框和最少 20 mm 净余量。

多扇构图支持各扇重复、奇偶交替镜像、全宽连续。连续模式先形成统一图案再裁入各扇安全区，拼缝侧使用 `meetingBorder`；不是各扇拉伸拼成假连续。布局为整面、居中带、偏置带、上半、下半、上下双块（中间留分隔）。

| 图案资源 | 当前几何 / 工艺 | 最小特征 |
| --- | --- | --- |
| `lines` | 指定角度平行路径，按数量或间距，平底或 V 形槽 | 2 mm |
| `diamond` | 菱形单元二维阵列，切穿或定深凹雕 | 5 mm |
| `octagon` | 八角单元二维阵列，切穿或定深凹雕 | 5 mm |
| `round_scene` | 圆环及资源中的几何折线，平底凹槽 | 2 mm |
| `panels` | 矩形直壁池板，可加独立凸起扣线 | 10 mm |
| `glass_lattice` | 菱形镂空，外购背衬玻璃 | 5 mm |

精确图案点列、归一化轮廓、生成器、适配标记与标签保存在 [patterns.json](reference/patterns.json)。parallel_paths 最多 100 条，cell_regions 为 1–12 列、1–16 行；净单元尺寸须足够容纳最小特征与保留材料，排线中心距须至少槽宽加 2 mm。源码还检查 `len(source)×len(sides)×count>800` 时拒绝，不能把这个乘积约束理解为无限量花纹。

## 深度、五金保护与显示

定深加工可选正面、背面、双面，累计剩余厚度必须满足 `T−depth×面数 >= minSkin`。切穿/玻璃花格采用板厚作为深度，仅走一次贯穿，不让隐藏的定深/双面草稿干预。V 槽只适用 lines，槽宽按 `2×depth×tan(vAngle/2)` 计算；它仍受最小特征与排线间距约束。

五金保护按每扇锁区和高度 0.12H、0.5H、0.88H 的三个铰链区声明，第一扇合页侧可选左右，其后交替。保留区须在扇内。线槽/凹坑受边框及保护区裁切；贯穿花格或扣线单元若撞保护区则整单元跳过，避免孤立材料；扣线越过装饰安全区也整单元跳过。关闭保护时对应尺寸草稿不参与加工，不生成锁具或合页实体。

显示由 `door_display_geometry.py` 独立计算保留材料表面，呈现门厚、孔壁、凹槽底面与 V 形坡面；不借制造布尔刀具生成显示。圆景显示弦差不超过 0.2 mm，加工分支保留精确圆弧。重复扇采用局部显示原型与位置变换；镜像、硬件保留区和连续裁切参与原型身份，避免误合并不同外观。

## 制造声明与板件清单

公共 `display()` 输出 `icax.display-model` v2。公共 `manufacturing()` 在声明上下文仅给出 `icax.manufacturing-model` v4 的连接与工艺；它不在产品入口执行布尔或刀路。脚本内部以 `surface-feature-cut` 调用声明加工，内部输入为 `icax.assembly-process-input` v1：stock 板厚、表面坐标系/深度轴、line/polygon/ring 图元、实际边界及保护区，参数包括 depth/through/grooveWidth/tool/vAngle。SDK 在公共制造输出中将其转成 v2 的初始 stock 引用；宿主随后绑定真实设计对象。`assembly_geometry_process_runtime.invoke` 在声明阶段记录调用并保留初始目标，执行阶段才生成实际切除。

门扇为自制板件；池板扣线是独立凸起环件，接触未切表面而非与门板融合；背衬玻璃是外购显示件，以装饰安全区尺寸生成，位于背面指定间距之后，不进入自制制造目标。内部 parts 表记录类别、宽高厚、数量；`doorDecoration` 记录保护区、特征、跳过计数和剩余厚度。这些是内部记录，公共 SDK 不直接发布内部扩展或重返宿主 `parameters`。

## 明确未实现

未包含门框、锁具/铰链安装件、门芯与防火层、玻璃固定件、强度认证。lines 是平面截断端部；round_scene 是几何圆景，不是艺术浮雕；panels 是直壁凹坑，没有斜面清根。实际刀具半径及扫掠、走刀、桥位、工装、粗精加工、NC、仿真均未实现。没有龙凤、人物、欧式艺术浮雕等授权源模型，六个图案不代表完整艺术素材库；记录的图案原始路径不能当成机床轨迹。

## 源码与参考测试

`reference/template.py` 的 `_generate_document` 定义分扇、布局、保护区及加工声明；`patterns.json` 定义六类资源；`reference/README.md` 保存原调研出处和边界。依赖共享 `plate_geometry.py`、`door_display_geometry.py`、`shared_tube_geometry.py`、`assembly_geometry_process_runtime.py` 与 SDK，均未因归档修改。

参考测试在 `src/tests/icax-plugins/product/TubeDesigner/TubeDesignerTest/` 的 `DecorativeDoorTests.py`、`ProductDisplaySeparationTests.py`、`ProductPureFunctionBoundaryTests.py`、`ProductManufacturingDeclarationTests.py`、`AssemblySurfaceMachiningTests.py`；前端条件资料在 `src/tests/iCAX-UI/DecorativeDoorTest.mjs`；原生加工资料在 `ProductPureMachining.native.mjs`。它们涉及图案/门型组合、实际边界、V 槽及剩余厚度、外购玻璃、输入不变、声明与加工隔离。本次只读核对，没有重新运行或宣称新验收通过。
