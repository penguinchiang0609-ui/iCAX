# 18 NFP 参考点域与共享几何缓存

NFP 把两个形状的相对平移冲突转为参考点空间的禁放域；它负责产生几何候选，不自行决定最终排法和目标。

类型：共享几何服务。当前实现状态：**复用现有 NFP 服务**。核对日期：2026-11-07。

## 代码实现与调用

入口：`NfpCache`。实现位置：[NestingNfp.cpp](<D:/penguinchinage0609/iCAX/src/iCAX-Engine/foundation/SheetNesting/NestingNfp.cpp:382>)

复用当前件对几何准备、相对变换、缓存及候选生成服务。放置生成器持有完整不可变 Problem 快照，所有已准备轮廓、件对 NFP 和世界姿态缓存都绑定该快照；外部修改输入不会污染已构造的生成器，新输入需要新建生成器。

世界姿态几何缓存只复用同一原件与同一姿态的材料几何和保守范围，有界为4096项/65536顶点。每次查询仍按当前布局重建障碍与阻挡索引，不缓存需求、库存或某次布局的判断。直角平移使用已对照逐顶点原件语义验证的变换；任意角仍走原世界变换，算术不支持时保留原检查流程。

范围与差异：原始 NFP 核心仍以简单无孔多边形为基础；Plane 封装已增加圆／带孔件的保守私有轮廓及真实孔域候选，返回前使用原材料严格复核。缓存仍只保存几何，不保存局部布局或跨任务方案。

完整调用示例、注册清单及测试范围见 [实现状态](<D:/penguinchinage0609/iCAX/doc/Plane/策略/实现状态.md>)。

## 输入输出与独立边界

| 项目 | 本策略契约 |
| --- | --- |
| 输入 | 固定形状 A、移动形状 B、姿态、参考点与几何预算。 |
| 输出 | 禁放平移域、复用变换、支持状态及缓存统计。 |
| 显式配置 | 几何限额、缓存容量、同键并发和取消策略。 |

本节保留原资料的目标契约；实际可用字段以本篇代码入口及头文件为准。下述设计步骤中超出“代码实现与调用”范围的内容仍是待扩展项。

缓存只包含不可变几何；布局、库存和取消状态不得相互污染。候选仍回到原始材料校验，几何不支持与空可行域分开。

## 原资料的方法与实际分支

旧体系普通外 NFP 走轨迹线生成，凸形走凸分支；干涉轮廓与本体交叉求两份并集。旧缓存支持相对角和反向复用，但包装只接受单个 polygon；矩形内域直接由当前旋转件包络收缩，再增量扣已排件 NFP。

源文特别指出基类判断误写赋值，导致非 BottomLeft 分支改成 OTHERDIRECTION 并提前返回；覆写比较器的类须另看。此缺陷属于源照片所分析的另一代码库，不是 iCAX 当前实现。

## 面向 iCAX 的独立策略步骤

1. 对固定 A 和移动 B，在统一参考点约定下求禁放相对平移域，可写为 A⊕(-B)；实际材料、角度与参考点均参与契约。
2. 对矩形板由旋转 B 的 Bounds 求平移内域，再减各固定原件的已变换禁放域。
3. 缓存只持有不可变几何，规范化轮廓与相对角必须严格核对，哈希相同后仍核内容。
4. 复用反向件对、整体旋转和平移时通过 Lookup.Transform 还原，不重复加原点。
5. 保留多环、窄通道与退化接触候选；布尔空结果不能代替所有接触情形。
6. 每次候选回到原件真实材料检查，NFP 只是候选来源。
7. 有界缓存、并发同键合并、取消隔离、构造限额与超限状态分别记录。

## 原有代码复用点

| 代码位置 | 符号或分支 | 复用关系 |
| --- | --- | --- |
| [SheetNestingNfp.h](<D:/penguinchinage0609/iCAX/src/iCAX-Engine/foundation/SheetNesting/SheetNestingNfp.h:80>) | `class NfpCache` | 不可变件对几何缓存、查询和状态契约。 |
| [NestingNfp.cpp](<D:/penguinchinage0609/iCAX/src/iCAX-Engine/foundation/SheetNesting/NestingNfp.cpp:498>) | `NfpCache::Prepare` | NFP 几何准备与查询实现，包括复用与限额。 |
| [NestingNfp.cpp](<D:/penguinchinage0609/iCAX/src/iCAX-Engine/foundation/SheetNesting/NestingNfp.cpp:382>) | `NfpCache::Get` | NFP 几何准备与查询实现，包括复用与限额。 |
| [NestingNfpPlacement.cpp](<D:/penguinchinage0609/iCAX/src/iCAX-Engine/foundation/SheetNesting/NestingNfpPlacement.cpp:242>) | `NfpPlacementQuery Find` | 可行平移域、接触候选与最终原件验证。 |
| [NestingNfpPlacement.h](<D:/penguinchinage0609/iCAX/src/iCAX-Engine/foundation/SheetNesting/NestingNfpPlacement.h:49>) | `class NfpPlacementGenerator` | 工作域内的 NFP 候选接口，输出已复核落点或受阻候选。 |

## 当前适用边界

原始 NFP 核心仍以简单无孔多边形为基础；圆／孔的保守私有提案与真实孔域套排位于 Plane 封装，最终按原件验真。不可变几何缓存不保存局部布局或跨任务方案。

## 实现后的验收条件

以下保留原资料导出的完整验收目标，不表示每项已通过。当前已执行的测试与未覆盖范围见“实现状态”，尤其保留上述适配差异。

- 局部原点/整体旋转/互换 A B 不改变物理禁放结论。
- 并发同键只构造一次。
- 等待者取消不污染成功缓存。
- 微小边与零宽接触有真实几何复核。
- 不支持、限额、取消、空可行域分别返回。

## 来源与相关策略

来源目录：[18](<D:/penguinchinage0609/iCAX/doc/Plane/18>)；对应照片内原文编号 `20`。下列 4 张照片共同构成本篇来源，重叠拍摄段落已合并。

- [微信图片_20261107113301_100_285.jpg](<D:/penguinchinage0609/iCAX/doc/Plane/18/微信图片_20261107113301_100_285.jpg>)
- [微信图片_20261107113305_101_285.jpg](<D:/penguinchinage0609/iCAX/doc/Plane/18/微信图片_20261107113305_101_285.jpg>)
- [微信图片_20261107113307_102_285.jpg](<D:/penguinchinage0609/iCAX/doc/Plane/18/微信图片_20261107113307_102_285.jpg>)
- [微信图片_20261107113311_103_285.jpg](<D:/penguinchinage0609/iCAX/doc/Plane/18/微信图片_20261107113311_103_285.jpg>)

原照片分析的是 WiseCAM-Restricted，标注基线 `05ffc57d`；其中旧类名、启用状态和静态疑点归属于该基线。iCAX 对照取当前工作区文件，代码位置不表示整个源算法已移植。

相关但不同的策略：[05 剩余多边形内域的全规格候选构造](<D:/penguinchinage0609/iCAX/doc/Plane/策略/05-剩余多边形内域的全规格候选构造.md>)；[19 NFP 节距驱动的分区阵列](<D:/penguinchinage0609/iCAX/doc/Plane/策略/19-NFP 节距驱动的分区阵列.md>)；[27 固定布局上的 NFP 增量补隙](<D:/penguinchinage0609/iCAX/doc/Plane/策略/27-固定布局上的 NFP 增量补隙.md>)。

[返回总索引](<D:/penguinchinage0609/iCAX/doc/Plane/策略/README.md>)
