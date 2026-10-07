# 百叶窗：归档时规格与实现

模板 `louver-window`，描述符与内部模型版本均为 `2.1.0`。原实现目录为 `templates/product/louver_window`，现暂停运行入口并保存在 [reference](reference/template.py)。它实现矩形空心管框和固定管状叶片，不是可调百叶、薄板折弯叶片或完整通风性能设计。

默认外宽 1200 mm、外高 1500 mm；边框为 40 × 100 mm、外圆角 2 mm、壁厚 1.5 mm，叶片为 80 × 20 mm、外圆角 2 mm、壁厚 1.5 mm。默认叶片轴线水平、截面滚转 45°，中心距 85 mm，两端最小阵列边距各 3 mm，余量均分，连接为顶接。全部 35 项参数的默认值、选项、单字段约束、显隐和呈现条件见 [参数附录](parameters.md)；原始声明见 [template.json](reference/template.json)。这些单字段范围还要同时满足下述几何关系。

## 成品布局与几何

坐标约定为 X 宽、Y 深、Z 高。排列方向角 φ 在窗面 XZ 内定义叶片轴线，允许 −180° 至 180°；滚转角 θ 独立绕叶片轴线旋转，允许 −90° 至 90°。当 θ=0°，截面宽 B 沿窗深方向；θ=90°，B 平行窗面。

`layout` 按实际圆角矩形截面计算投影，采用 `hp=(B−2R)|sinθ|+(T−2R)|cosθ|+2R` 与 `dp=(B−2R)|cosθ|+(T−2R)|sinθ|+2R`。投影搭接不等于实体相交；脚本还对相邻真实截面作几何碰撞检查。边框与叶片通过 `tube_profile_catalog.py` 读取矩形管资源，包含实际内外轮廓；描述符的截面选项只有只读 `rect`。

外框支持左右包上下、上下包左右与 45° 拼角。中间立柱及横梁各 0–10，立柱等距，横梁在立柱处分段。叶片可按每格分段或整根穿过支撑；`through_insert` 会采用实际贯通支撑布局，保留的 `supportMode` 草稿不能制造重叠管件。

每格可按中心距、投影净缝、投影搭接或数量排列：净缝模式中心距为 `hp+G`；搭接模式为 `hp−O` 且须大于零；数量模式在可用区间内均布，单根居中。起末边距在叶片投影外侧扣除，均分选项分配剩余空隙。每格最多 200 根、全窗最多 500 根，超限拒绝。斜向叶片用凸半平面裁切得到端面与长度，不把斜向叶片一律当等长料。

## 连接与制造声明

公共 `display()` 使用内置成品布局，仅发布显示资源与设计描述；它复制输入后采用 `builtin_rules`，所以隐藏的装配来源草稿不改变成品显示。显示不执行制造布尔加工，也不保留端部接收者、槽孔及斜切加工数据。显示中的管端或拼角近似不能作为最终加工实体的证据。

公共 `manufacturing()` 使用宿主原输入，在 `manufacturing_declaration()` 下输出连接及待执行工艺，产品脚本此时不切实体。加工执行器后续才依据声明生成实际加工几何。

| 内置连接 | 有效参数及约束 | 后续几何加工 |
| --- | --- | --- |
| `face_weld` 顶接 | `endClearance` 0–10 mm；插深、外伸草稿不参与 | 叶片真实端平面裁切 |
| `slot_insert` 插槽 | 深度沿接收管壁法向计量；必须穿过近壁并保留对壁，同一分段支撑两侧插入不能相撞 | 端切及按叶片实际外轮廓切接收管槽 |
| `through_insert` 穿透 | `throughExtension` 指定穿出外伸；中间支撑按贯通处理 | 管框和所有相交支撑的截面槽 |

槽口间隙只在真正需要槽口时有效。叶片深度投影加双侧间隙必须放进框管内的有效深度；插深须满足 `frame.wall+clearance+0.01 < depth < frame.face−frame.wall−clearance−0.01`，双侧插槽还须满足 `2×(depth+clearance+0.01)<frame.face`。这些约束在 `layout()` 中检查，不由单字段最大值代替。

内置工艺使用 `structural-planar-trim`、`structural-stock-fit`、`structural-profile-pocket`。接收槽以实际叶片截面及姿态声明刀具包络，包含圆角和深度边界。制造零件按截面、材料、长度及端面归并；接收管不同槽位采用保守独立身份，避免误合并。

隐藏参数 `assemblyPlanningMode=external_templates` 仅支持轴向 0/±90/±180° 的顶接叶片，存在中间支撑时须分段。它提供方头原管、4 个外框 L 节点和各立柱、横梁、叶片端部 T 节点，保存真实端点、制造截面基准、轴向站位及由接收截面求得的毛坯余量；不执行旧拼框规则和端隙。斜向、插槽或穿透组合明确拒绝此模式。

外部模式的 `profile-stock-preparation` 声明采用 `icax.assembly-process-input` v2，目标为初始 stock，节点状态为 `await-external-assembly-process`、`ready=false`。没有分配及验证各节点工艺前，它不是可直接使用的最终下料。外部提供的截面镜像映射也只支持具有共同镜像中心的对称矩形轮廓，不支持任意异型材镜像。

## 输出与边界

内部文档包含加工件归并表、叶片阵列、投影宽深、整体深度和 `louver` 规划扩展；公共 SDK 会转成独立显示模型或制造声明，不能将这些内部扩展视作公共返回字段。产品编号和材料用于身份/清单；不生成焊接路径、切管机 NC、排样或运动仿真。本模板未提供薄板成形、联动开合、风量/遮光/强度认证。

## 源码与参考测试

`reference/template.py` 中 `layout` 定义投影、阵列、碰撞及插深约束；`_generate_external_geometry` 定义原管和 L/T 节点；`_generate_document` 定义内置结构加工声明；`display` / `manufacturing` 是当前公共边界。图标与示意图保留在 `reference/resource/`。

既有定位资料包括 `src/tests/icax-plugins/product/TubeDesigner/TubeDesignerTest/LouverTubeTests.py`、`WindowCatalogueTests.py`、`ProductDisplayComponentBoundaryTests.py`、`ProductDisplaySeparationTests.py`、`ProductPureFunctionBoundaryTests.py`，以及 `src/tests/iCAX-UI/TubeDesignerStructuralMachining.native.mjs`。它们分别涉及方向与端长、实体槽口、输入不变及显示/制造隔离。部分历史测试引用 `generate` 或 `legacy_processed`；原描述符只声明 `builtin_rules/external_templates`，不能由旧测试推导新增模式支持。本次未重新运行这些用例。
