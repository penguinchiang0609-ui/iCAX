# 21 NFP 主方向与靠接混合构造

把沿指定主轴靠近起点和邻边接触一起用于落点选择。该策略独立于纯靠接，允许以方向偏好换取不同的材料集中方式。

类型：独立构造策略。当前实现状态：**已实现受限适配**。核对日期：2026-11-07。

## 代码实现与调用

入口：`plane.nfp-directional`。实现位置：[NestingPlaneStrategies.cpp](<D:/penguinchinage0609/iCAX/src/iCAX-Engine/foundation/SheetNesting/NestingPlaneStrategies.cpp:528>)

主轴坐标与邻接交域半周长、板外交域半周长混合评分；XFirst 可切换主轴。

范围与差异：混合评分是当前明确适配，板外权重为 0.4；不声称旧库所有远侧边修正规则均已移植。

完整调用示例、注册清单及测试范围见 [实现状态](<D:/penguinchinage0609/iCAX/doc/Plane/策略/实现状态.md>)。

## 输入输出与独立边界

| 项目 | 本策略契约 |
| --- | --- |
| 输入 | 原需求、各连通可行域、允许角与主轴。 |
| 输出 | 方向与靠接混合评分选出的原件布局。 |
| 显式配置 | 主次轴权重、半周长代理尺度、远侧边修正和平局序。 |

本节保留原资料的目标契约；实际可用字段以本篇代码入口及头文件为准。下述设计步骤中超出“代码实现与调用”范围的内容仍是待扩展项。

以 (PartID, Instance) 记录原实例，兼容材料和合法角度不变；试排不消费正式库存，胜者一次提交。原件实材、间距和板边须复核；不支持或有限搜索失败保留未排需求。

## 原资料的方法与实际分支

CNfpClosestBLStrategy 覆写比较器，绕开旧基类赋值分支。它检查每个可行域的顶点，不是只用一个区域的凸包。主轴使用 left-x 或 bottom-y，次轴权重为零；邻件外扩交集的半周长作为靠接代理，板边半周长项带 0.4 权重，并修正远侧边。

规格层仍是首个可放即选，首件另有规则；也不能照搬 Greedy 类的后处理链。半周长代理不等于原件真正共享的边长。

## 面向 iCAX 的独立策略步骤

1. 明确主轴、坐标原点、外扩尺度及各评分项单位；支持源式分项和归一化变体两种版本。
2. 对首个可行规格汇总各允许角、各连通可行域的顶点和受支持的接触候选。
3. 用真实候选姿态计算主轴位移、邻件交集半周长、板边项及远侧边修正。
4. 首件独立比较靠边位置，后续件用混合分；次轴是否参与由明确参数决定。
5. 同分按稳定规格、角度与坐标序择优；逐件提交并更新障碍和库存。
6. 用最终原件几何验评整板，与纯靠接和低左按同预算分别对照。

## 原有代码复用点

| 代码位置 | 符号或分支 | 复用关系 |
| --- | --- | --- |
| [NestingNfpPlacement.cpp](<D:/penguinchinage0609/iCAX/src/iCAX-Engine/foundation/SheetNesting/NestingNfpPlacement.cpp:242>) | `NfpPlacementQuery Find` | 可行平移域、接触候选与最终原件验证。 |
| [NestingGeometry.cpp](<D:/penguinchinage0609/iCAX/src/iCAX-Engine/foundation/SheetNesting/NestingGeometry.cpp:388>) | `bool ShapesHaveCommonEdge` | 原件角度、面积、包含和相交检查；具体能力以所引函数为准。 |
| [NestingRanking.cpp](<D:/penguinchinage0609/iCAX/src/iCAX-Engine/foundation/SheetNesting/NestingRanking.cpp:89>) | `QualityKey Quality` | 公共参赛资格与最终排名；不能用局部评分替代。 |
| [SheetNesting.h](<D:/penguinchinage0609/iCAX/src/iCAX-Engine/foundation/SheetNesting/SheetNesting.h:111>) | `enum class RankingPolicy` | 原始输入、实例身份、状态、预算及公共结果模型；业务扩展须与此契约兼容。 |

## 当前适用边界

混合评分是当前明确适配，板外权重为 0.4；不声称旧库所有远侧边修正规则均已移植。

## 实现后的验收条件

以下保留原资料导出的完整验收目标，不表示每项已通过。当前已执行的测试与未覆盖范围见“实现状态”，尤其保留上述适配差异。

- 多个连通区域均有候选。
- 主轴切换后方向项对应坐标正确。
- 次轴为零时不能暗加次轴大权重。
- 外扩半周长不记成真实共边统计。
- 首件和远侧边修正分别验证。
- 候选顺序变化不改变显式平局规则。

## 来源与相关策略

来源目录：[21](<D:/penguinchinage0609/iCAX/doc/Plane/21>)；对应照片内原文编号 `23`。下列 4 张照片共同构成本篇来源，重叠拍摄段落已合并。

- [微信图片_20261107113333_112_285.jpg](<D:/penguinchinage0609/iCAX/doc/Plane/21/微信图片_20261107113333_112_285.jpg>)
- [微信图片_20261107113334_113_285.jpg](<D:/penguinchinage0609/iCAX/doc/Plane/21/微信图片_20261107113334_113_285.jpg>)
- [微信图片_20261107113335_114_285.jpg](<D:/penguinchinage0609/iCAX/doc/Plane/21/微信图片_20261107113335_114_285.jpg>)
- [微信图片_20261107113337_115_285.jpg](<D:/penguinchinage0609/iCAX/doc/Plane/21/微信图片_20261107113337_115_285.jpg>)

原照片分析的是 WiseCAM-Restricted，标注基线 `05ffc57d`；其中旧类名、启用状态和静态疑点归属于该基线。iCAX 对照取当前工作区文件，代码位置不表示整个源算法已移植。

相关但不同的策略：[20 NFP 纯靠接逐件构造](<D:/penguinchinage0609/iCAX/doc/Plane/策略/20-NFP 纯靠接逐件构造.md>)；[22 首件定宽的长条 NFP 排样](<D:/penguinchinage0609/iCAX/doc/Plane/策略/22-首件定宽的长条 NFP 排样.md>)。

[返回总索引](<D:/penguinchinage0609/iCAX/doc/Plane/策略/README.md>)
