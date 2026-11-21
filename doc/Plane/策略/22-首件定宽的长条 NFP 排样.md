# 22 首件定宽的长条 NFP 排样

先用一件确定条带厚度，再在该条带内靠接排放其余原件。该策略适合明确需要长条布局的任务，首件与方向作为独立配置参与竞争。

类型：独立构造策略。当前实现状态：**已实现受限适配**。核对日期：2026-11-07。

## 代码实现与调用

入口：`plane.nfp-strip`。实现位置：[NestingPlaneStrategies.cpp](<D:/penguinchinage0609/iCAX/src/iCAX-Engine/foundation/SheetNesting/NestingPlaneStrategies.cpp:1617>)

第一件成功后锁定条带横向宽度或纵向厚度，后续 NFP 候选限制在该条带；输出恢复物理板尺寸。

范围与差异：是有限板内条带，不是卷料；首件选择受原顺序与有限候选影响。

完整调用示例、注册清单及测试范围见 [实现状态](<D:/penguinchinage0609/iCAX/doc/Plane/策略/实现状态.md>)。

## 输入输出与独立边界

| 项目 | 本策略契约 |
| --- | --- |
| 输入 | 原板、原需求、首件配置和横/纵条带模式。 |
| 输出 | 条带内布局、实际条带尺寸和剩余需求。 |
| 显式配置 | 首件排序、锁定厚度、方向和条带候选预算。 |

本节保留原资料的目标契约；实际可用字段以本篇代码入口及头文件为准。下述设计步骤中超出“代码实现与调用”范围的内容仍是待扩展项。

以 (PartID, Instance) 记录原实例，兼容材料和合法角度不变；试排不消费正式库存，胜者一次提交。原件实材、间距和板边须复核；不支持或有限搜索失败保留未排需求。

## 原资料的方法与实际分支

NFP_DOOR 创建 `COneLongRectStrategy`。旧门业 ONELONGRECT 分别按面积和长度建立两份候选。首件成功后，RectSheet 先变为首件移除矩形，再只恢复一个方向到原板边；另一个方向只加容差，并清空角度 NFP 域缓存。

原文 Y 分支恢复 right，非 Y 分支恢复 top，不能凭方向枚举名称反推几何。首件评分为内缩板外差集半周长；后续为条带外差集与邻件交集半周长之和，同分按重心靠左底。这些都是接触代理。

## 面向 iCAX 的独立策略步骤

1. 明确横向长条或纵向长条，使用物理坐标字段描述，避免沿用含混方向名称。
2. 对每个首件排序配置新建 job，在原板内按合法角度试放首件。
3. 用首件实际占位厚度建立条带，另一维保持原可用板长；条带仅为搜索约束，物理 Stock 不变。
4. 清空依赖工作区域的可行域缓存，保留可复用的不可变件对 NFP。
5. 在条带内为剩余规格生成 NFP 接触候选，首可行件型或全局候选模式必须写进配置。
6. 按接触/紧凑局部启发式推进，原件身份、角度和需求保持。
7. 输出条带尺寸、首件及实际包络；按完整度和统一质量选优。扩大带宽属于另一个显式变体。

## 原有代码复用点

| 代码位置 | 符号或分支 | 复用关系 |
| --- | --- | --- |
| [NestingNfpPlacement.cpp](<D:/penguinchinage0609/iCAX/src/iCAX-Engine/foundation/SheetNesting/NestingNfpPlacement.cpp:242>) | `NfpPlacementQuery Find` | 可行平移域、接触候选与最终原件验证。 |
| [SheetNestingBeam.h](<D:/penguinchinage0609/iCAX/src/iCAX-Engine/foundation/SheetNesting/SheetNestingBeam.h:15>) | `FirstPartIndex` | 可选 NFP 束搜索范围与首件配置。 |
| [NestingRemnantSearch.cpp](<D:/penguinchinage0609/iCAX/src/iCAX-Engine/foundation/SheetNesting/NestingRemnantSearch.cpp:13>) | `Result RunStripRules` | 现有有限比例缩域重构，目标为改善经验证的规整余料。 |
| [NestingGeometry.cpp](<D:/penguinchinage0609/iCAX/src/iCAX-Engine/foundation/SheetNesting/NestingGeometry.cpp:425>) | `bool InsideSheet` | 原件角度、面积、包含和相交检查；具体能力以所引函数为准。 |

## 当前适用边界

是有限板内条带，不是卷料；首件选择受原顺序与有限候选影响。

## 实现后的验收条件

以下保留原资料导出的完整验收目标，不表示每项已通过。当前已执行的测试与未覆盖范围见“实现状态”，尤其保留上述适配差异。

- 100×80 原板与 30×10 首件应分别形成约 100×10 或 30×80 的工作条带，保留实际板尺寸。
- 换首件后重建域。
- 非零原点正确。
- 后续宽件未排必须保留需求。
- 比较实际成果包络而非条带面积。

## 来源与相关策略

来源目录：[22](<D:/penguinchinage0609/iCAX/doc/Plane/22>)；对应照片内原文编号 `24`。下列 4 张照片共同构成本篇来源，重叠拍摄段落已合并。

- [微信图片_20261107113338_116_285.jpg](<D:/penguinchinage0609/iCAX/doc/Plane/22/微信图片_20261107113338_116_285.jpg>)
- [微信图片_20261107113339_117_285.jpg](<D:/penguinchinage0609/iCAX/doc/Plane/22/微信图片_20261107113339_117_285.jpg>)
- [微信图片_20261107113341_118_285.jpg](<D:/penguinchinage0609/iCAX/doc/Plane/22/微信图片_20261107113341_118_285.jpg>)
- [微信图片_20261107113342_119_285.jpg](<D:/penguinchinage0609/iCAX/doc/Plane/22/微信图片_20261107113342_119_285.jpg>)

原照片分析的是 WiseCAM-Restricted，标注基线 `05ffc57d`；其中旧类名、启用状态和静态疑点归属于该基线。iCAX 对照取当前工作区文件，代码位置不表示整个源算法已移植。

相关但不同的策略：[20 NFP 纯靠接逐件构造](<D:/penguinchinage0609/iCAX/doc/Plane/策略/20-NFP 纯靠接逐件构造.md>)；[21 NFP 主方向与靠接混合构造](<D:/penguinchinage0609/iCAX/doc/Plane/策略/21-NFP 主方向与靠接混合构造.md>)；[30 固定末板实例的缩域重构](<D:/penguinchinage0609/iCAX/doc/Plane/策略/30-固定末板实例的缩域重构.md>)。

[返回总索引](<D:/penguinchinage0609/iCAX/doc/Plane/策略/README.md>)
