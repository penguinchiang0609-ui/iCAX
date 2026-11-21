# 27 固定布局上的 NFP 增量补隙

固定已有零件，仅把剩余需求加入指定工作域，输出新增件。已有件不移动，调用者可将它嵌入阵列、异形板或局部修复流程。

类型：共享补隙服务。当前实现状态：**已实现受限适配**。核对日期：2026-11-07。

## 代码实现与调用

入口：`MakePlaneNfpFillStage`。实现位置：[NestingPlaneStrategies.cpp](<D:/penguinchinage0609/iCAX/src/iCAX-Engine/foundation/SheetNesting/NestingPlaneStrategies.cpp:1782>)

在已有板上新增缺失原实例，保持固定姿态和组号，支持原实例编号有空洞、多板补齐，返回完整 Result。

范围与差异：不新开板、不跨板移动旧件；受 NFP 准入限制。不能与现有跨板 MakeNfpRefillStage 混为同一动作。

完整调用示例、注册清单及测试范围见 [实现状态](<D:/penguinchinage0609/iCAX/doc/Plane/策略/实现状态.md>)。

## 输入输出与独立边界

| 项目 | 本策略契约 |
| --- | --- |
| 输入 | 原问题、工作域、固定原实例和剩余需求。 |
| 输出 | 仅新增原件、更新余量和完整/部分/取消状态。 |
| 显式配置 | 底左或靠接候选、几何准入、每次补隙预算。 |

本节保留原资料的目标契约；实际可用字段以本篇代码入口及头文件为准。下述设计步骤中超出“代码实现与调用”范围的内容仍是待扩展项。

固定件身份与姿态不变；新增需求只提交一次。空增量、部分增量、完整补满、取消与不支持必须可以区分。

## 原资料的方法与实际分支

CNfpFillStrategy 接收工作矩形、已有结果和剩余仓库，返回新增件列表。仓库按值复制，服务本地扣数与调用者提交扣数是不同状态，不应误认为重复扣同一库存。新增件立即成为障碍，可有孔内填充；空固定布局加恢复全部数量则是从头重排。

共享入口门槛包括非全矩形、共边或近零间距、单外轮廓至多 1000 点及规格少于 500，直接入口未必相同。默认 BottomLeft 可达；组合评分受旧基类赋值问题影响，未到达的评分另有未平移模板疑点。超时返回 false 时仍可能已有新增件，布尔值不足以表达结果。

## 面向 iCAX 的独立策略步骤

1. 定义请求为原问题、工作域、固定原实例、待补数量、候选策略与子预算。
2. 先独立验证固定布局，将固定件与非订单障碍放入候选环境；禁止隐式移动或重计固定件需求。
3. 在剩余需求中选择原件，生成合法 NFP 接触落点；不支持的几何明确拒绝或进入显式回退。
4. 原件复核通过后追加到新增集合，同步本地余量与障碍，再生成下一件。
5. 返回 AddedPlacements、Remaining、停止原因和统计；部分有效新增与完整补满分别表示。
6. 调用方按增量统一提交一次；局部重排必须先移除指定原实例，再将同一实例清单全部放回。
7. 提交后整体再次验评，固定件姿态和身份应与输入完全一致。

## 原有代码复用点

| 代码位置 | 符号或分支 | 复用关系 |
| --- | --- | --- |
| [NestingNfpPlacement.h](<D:/penguinchinage0609/iCAX/src/iCAX-Engine/foundation/SheetNesting/NestingNfpPlacement.h:49>) | `class NfpPlacementGenerator` | 工作域内的 NFP 候选接口，输出已复核落点或受阻候选。 |
| [SheetNestingPipeline.h](<D:/penguinchinage0609/iCAX/src/iCAX-Engine/foundation/SheetNesting/SheetNestingPipeline.h:83>) | `MakeNfpRefillStage` | 排序、构造、后处理及共同验评契约。 |
| [SheetNestingDomainRepair.h](<D:/penguinchinage0609/iCAX/src/iCAX-Engine/foundation/SheetNesting/SheetNestingDomainRepair.h:6>) | `MakeDomainRepairStage` | 有效单板部分布局的有界局部修复接口。 |
| [SheetNestingReassembly.h](<D:/penguinchinage0609/iCAX/src/iCAX-Engine/foundation/SheetNesting/SheetNestingReassembly.h:21>) | `MakeNfpReassemblyStage` | 可选局部原实例释放与联合重组接口。 |

## 当前适用边界

不新开板、不跨板移动旧件；正间距候选服务现包含圆／带孔件的保守私有提案和真实孔域候选，返回前按原材料、已占原件和原始数量严格复核。它与 MakeNfpRefillStage 分别处理固定新增和跨板转移，二者不能互相替代。

## 实现后的验收条件

以下保留原资料导出的完整验收目标，不表示每项已通过。当前已执行的测试与未覆盖范围见“实现状态”，尤其保留上述适配差异。

- 固定件指纹不变。
- 零新增、部分新增和全部补满区分。
- 取消后可返回已验证新增。
- 调用者仅扣一次新增需求。
- 新件互相成为障碍。
- 移除修复时等面积异件不可替换。
- 跨板前填与本板缺件补隙统计分开。

## 来源与相关策略

来源目录：[27](<D:/penguinchinage0609/iCAX/doc/Plane/27>)；对应照片内原文编号 `29`。下列 4 张照片共同构成本篇来源，重叠拍摄段落已合并。

- [微信图片_20261107113418_137_285.jpg](<D:/penguinchinage0609/iCAX/doc/Plane/27/微信图片_20261107113418_137_285.jpg>)
- [微信图片_20261107114737_138_285.jpg](<D:/penguinchinage0609/iCAX/doc/Plane/27/微信图片_20261107114737_138_285.jpg>)
- [微信图片_20261107114742_139_285.jpg](<D:/penguinchinage0609/iCAX/doc/Plane/27/微信图片_20261107114742_139_285.jpg>)
- [微信图片_20261107114745_140_285.jpg](<D:/penguinchinage0609/iCAX/doc/Plane/27/微信图片_20261107114745_140_285.jpg>)

原照片分析的是 WiseCAM-Restricted，标注基线 `05ffc57d`；其中旧类名、启用状态和静态疑点归属于该基线。iCAX 对照取当前工作区文件，代码位置不表示整个源算法已移植。

相关但不同的策略：[18 NFP 参考点域与共享几何缓存](<D:/penguinchinage0609/iCAX/doc/Plane/策略/18-NFP 参考点域与共享几何缓存.md>)；[30 固定末板实例的缩域重构](<D:/penguinchinage0609/iCAX/doc/Plane/策略/30-固定末板实例的缩域重构.md>)；[40 固定实例布局的持续迭代压紧](<D:/penguinchinage0609/iCAX/doc/Plane/策略/40-固定实例布局的持续迭代压紧.md>)。

[返回总索引](<D:/penguinchinage0609/iCAX/doc/Plane/策略/README.md>)
