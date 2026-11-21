# 20 NFP 纯靠接逐件构造

在第一个能放下的规格内部选择靠近已有材料和边界的落点。它与跨全部规格择优、纯低左和方向混合靠接分别保留为不同策略。

类型：独立构造策略。当前实现状态：**已实现受限适配**。核对日期：2026-11-07。

## 代码实现与调用

入口：`plane.nfp-closest`。实现位置：[NestingPlaneStrategies.cpp](<D:/penguinchinage0609/iCAX/src/iCAX-Engine/foundation/SheetNesting/NestingPlaneStrategies.cpp:528>)

首个可行规格内按外扩材料与邻件交面积及板外面积评分，板外权重为 0.9。

本轮纯NFP构造增加显式自由域边界优先查询：`BoundaryVerticesOnly=true` 先验证真实自由域顶点，保留原件直线共边关键提案及普通间距/名义共边双域；该角没有合法候选时，再以完整枚举模式回退中点、接触采样与其他候选。通用NFP服务默认枚举行为不变。不是先精查全部raw候选后才截取64，也不把优先域失败当作不可行证明。

靠接评分的外扩轮廓按原件与角度在局部坐标、precision-8语义下准备，再按候选平移复用。这是构造评分代理；实际原件碰撞、边距、间距与共边资格仍按最终世界几何严格复核。本轮完整率与耗时仍须由真实路线实测给出，不能以缓存存在或候选减少代替验收。

范围与差异：实现纯靠接分支本身；未复制照片基线的赋值分支缺陷，也未接旧系统所有恢复数量重排后处理。

完整调用示例、注册清单及测试范围见 [实现状态](<D:/penguinchinage0609/iCAX/doc/Plane/策略/实现状态.md>)。

## 输入输出与独立边界

| 项目 | 本策略契约 |
| --- | --- |
| 输入 | 矩形工作域、固定原件、待排规格和合法角度。 |
| 输出 | 首可行规格的纯靠接构造布局。 |
| 显式配置 | 物理外扩尺度、靠件/板边权重、首件规则和后处理。 |

本节保留原资料的目标契约；实际可用字段以本篇代码入口及头文件为准。下述设计步骤中超出“代码实现与调用”范围的内容仍是待扩展项。

以 (PartID, Instance) 记录原实例，兼容材料和合法角度不变；试排不消费正式库存，胜者一次提交。原件实材、间距和板边须复核；不支持或有限搜索失败保留未排需求。

## 原资料的方法与实际分支

CNfpGreedyStrategy 的意图是比较小可行域凸包上的顶点，按候选外扩轮廓与邻件交面积、外扩落到板外的面积加权评分；板外项权重为 0.9。首件使用独立 FirstReplaceIfBetter，不能套用后续件评分。

照片分析指出基类写成 else if(m_nStrategyType=ST_OTHERDIRECTION)，赋值后的枚举值 3 为真，使非 BottomLeft 分支提前进入另一方向比较，预期纯靠接分支不可按名称认定生效。非桥切时的后处理还可能包括恢复数量重排、扫描补漏和 ARRAY_1/2 对照。该缺陷属于照片基线中的旧系统。

## 面向 iCAX 的独立策略步骤

1. 使用显式枚举描述 pure-contact、bottom-left 与 direction-contact，分支比较不得修改配置。
2. 按固定规格顺序找到首个存在合法落点的规格，枚举该规格全部候选角。
3. 对可行域接触候选执行原件碰撞和板内校验；可行域选择与候选上限写入配置。
4. 首件定义靠板边规则；后续件将原件变换到实际候选位置后再计算外扩交面积。
5. 外扩尺度采用声明的物理单位，靠件项、靠板项分项记录，并设稳定平局顺序。
6. 每次只提交一件、扣数并更新障碍；重排和补隙作为显式后处理，各自从一致快照开始。
7. 以公共 Quality 比较最终结果；局部靠接分只决定构造路径。

## 原有代码复用点

| 代码位置 | 符号或分支 | 复用关系 |
| --- | --- | --- |
| [NestingNfpPlacement.cpp](<D:/penguinchinage0609/iCAX/src/iCAX-Engine/foundation/SheetNesting/NestingNfpPlacement.cpp:242>) | `NfpPlacementQuery Find` | 可行平移域、接触候选与最终原件验证。 |
| [NestingContactRules.cpp](<D:/penguinchinage0609/iCAX/src/iCAX-Engine/foundation/SheetNesting/NestingContactRules.cpp:1045>) | `Result RunContactRules(` | 现有统一接触构造及其包络、方向、余料和共享边评分分支。 |
| [NestingGeometry.cpp](<D:/penguinchinage0609/iCAX/src/iCAX-Engine/foundation/SheetNesting/NestingGeometry.cpp:395>) | `bool ShapesConflict` | 原件角度、面积、包含和相交检查；具体能力以所引函数为准。 |
| [NestingStrategies.cpp](<D:/penguinchinage0609/iCAX/src/iCAX-Engine/foundation/SheetNesting/NestingStrategies.cpp:264>) | `StrategyRegistry DefaultStrategies` | 工厂包装、排序配置与真实默认注册入口。 |
| [NestingRanking.cpp](<D:/penguinchinage0609/iCAX/src/iCAX-Engine/foundation/SheetNesting/NestingRanking.cpp:89>) | `QualityKey Quality` | 公共参赛资格与最终排名；不能用局部评分替代。 |

## 当前适用边界

实现纯靠接分支本身；未复制照片基线的赋值分支缺陷，也未接旧系统所有恢复数量重排后处理。

## 实现后的验收条件

以下保留原资料导出的完整验收目标，不表示每项已通过。当前已执行的测试与未覆盖范围见“实现状态”，尤其保留上述适配差异。

- 首件与后续件分别核对。
- 纯靠接和低左在构造样例上能够产生预期差异。
- 评分随候选真实平移变化。
- 不支持角度不得加入。
- 同预算区分构造本身与后处理贡献。
- 同分结果可重放。

## 来源与相关策略

来源目录：[20](<D:/penguinchinage0609/iCAX/doc/Plane/20>)；对应照片内原文编号 `22`。下列 4 张照片共同构成本篇来源，重叠拍摄段落已合并。

- [微信图片_20261107113328_108_285.jpg](<D:/penguinchinage0609/iCAX/doc/Plane/20/微信图片_20261107113328_108_285.jpg>)
- [微信图片_20261107113330_109_285.jpg](<D:/penguinchinage0609/iCAX/doc/Plane/20/微信图片_20261107113330_109_285.jpg>)
- [微信图片_20261107113331_110_285.jpg](<D:/penguinchinage0609/iCAX/doc/Plane/20/微信图片_20261107113331_110_285.jpg>)
- [微信图片_20261107113332_111_285.jpg](<D:/penguinchinage0609/iCAX/doc/Plane/20/微信图片_20261107113332_111_285.jpg>)

原照片分析的是 WiseCAM-Restricted，标注基线 `05ffc57d`；其中旧类名、启用状态和静态疑点归属于该基线。iCAX 对照取当前工作区文件，代码位置不表示整个源算法已移植。

相关但不同的策略：[21 NFP 主方向与靠接混合构造](<D:/penguinchinage0609/iCAX/doc/Plane/策略/21-NFP 主方向与靠接混合构造.md>)；[22 首件定宽的长条 NFP 排样](<D:/penguinchinage0609/iCAX/doc/Plane/策略/22-首件定宽的长条 NFP 排样.md>)；[27 固定布局上的 NFP 增量补隙](<D:/penguinchinage0609/iCAX/doc/Plane/策略/27-固定布局上的 NFP 增量补隙.md>)。

[返回总索引](<D:/penguinchinage0609/iCAX/doc/Plane/策略/README.md>)
