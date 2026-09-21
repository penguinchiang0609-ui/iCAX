# 管材、棒材与型材排样：算法、论文、开源项目与商业组件调研报告

**调研截止日期：2026-09-20**  
**面向用途：切管 CAM 产品研发、算法选型、第三方组件评估及研发任务拆分。**

> 核心结论：管材排样不能只建模成“将若干长度装进几根原料”。面向异形端口、坡口与多卡盘设备，建议采用“组合优化器 + 自有端口几何服务 + 加工可行性验证器”的分层架构。
>
> 本报告依据公开论文、作者研究代码、官方仓库和厂商资料。未执行独立求解性能对比、未实机切割、未取得未公开 OEM 报价。厂商功能说明不等于独立验证；“待确认”也不等于“不支持”。下文的数学建模、接口与研发路线明确属于工程建议。

## 0. 选型摘要

| 业务目标 | 建议路线 | 不应混淆的边界 |
|---|---|---|
| 平端零件定尺下料、多种原料长度、余料复用 | 先用 PackingSolver 建立基线；复杂需求可用列生成和整数规划 | 这是长度组合优化，不是三维端口互嵌 |
| 斜切端口、梯形件、特定翻转及方向组合 | 研究 Lewis–Bonnet 算法；试用 AutoBarSizer、CutGLib | 特殊斜切模型不等于任意鱼嘴、曲面端口 |
| 圆管鱼嘴、方管跨面端口、角钢/槽钢复杂端口 | 自研端口最小间距及姿态兼容服务；外层使用模式优化和局部搜索 | 普通一维 SDK 不会自动读取 BRep 后解决全部几何问题 |
| 共切、坡口、多卡盘、零尾料、落料支撑 | 让排样与加工规划迭代验证 | 几何不重叠不代表能共切或能夹持加工 |
| 第三方采购 | 第一轮询证 AutoBarSizer、CutGLib、ALMA 线性优化组件 | 完整 CAM 的销售许可不自动包含算法 SDK/OEM 分发权 |

上述组件的公开能力来源见 [C01]–[C07]；开源与论文来源见 [O01]–[O12]、[P01]–[P10]。

## 1. 先把问题分成四层

### 1.1 第一层：定尺下料

输入零件长度和数量、原料长度与库存数量，决定使用哪些原料、每根原料放哪些零件。还可考虑前后修边、锯缝/割缝、材料成本、余料价值和订单约束。这对应一维切割库存问题（1D cutting stock）及装箱问题（bin packing）的相关变体。[P04a][P04b][P05]

零件种类少、同类数量大时，按“模式 × 使用次数”建模通常值得优先尝试；零件实例高度个性化时，按实例分配与排序的模型更自然。这是本报告建议的建模分流，不是对某种算法性能的保证。

### 1.2 第二层：顺序和姿态相关的端口互嵌

斜切端口、鱼嘴、台阶、舌头等会使相邻零件的轴向间距取决于两端几何和相对姿态。不能只给每个零件一个固定长度，也不能给每个零件一个与邻居无关的“平均重叠量”。顺序相关切损已经是独立研究问题。[P01][P02]

典型决策包括：哪两个零件相邻；前后方向是否允许交换；绕管轴旋转多少；哪种端口组合能紧排；组合后是否仍满足焊缝、孔位朝向和机床约束。

### 1.3 第三层：共切和加工可行性

本报告采用以下术语：

| 术语 | 判定含义 |
|---|---|
| 紧排 | 两件距离较小，但各自加工端口 |
| 互嵌 | 利用端口凹凸或斜切结构减少整体轴向占用 |
| 共切候选 | 存在可能共用的切割边界，尚未验证全部工艺 |
| 可执行共切 | 刀路、割缝补偿、壁厚/坡口及保留材料均兼容，并满足加工顺序和夹持条件 |

不能把两个设计轮廓重合直接判定为可共切。实际刀路、割缝补偿及夹持区会影响排样；SigmaNEST 的官方管材技术资料也明确讨论了这种区别。[C11]

### 1.4 第四层：生产与库存优化

订单交付、材料批次、机器选择、上料次数、重复排样模式、余料回库、换型和加工时间属于更外层的优化。余料复用也有独立研究综述，不能简单用“超过某个长度就是等值资产”代替经济评价。[P08][P09]

本报告首先面向直线、等截面原料。弯管、变截面、锥管、扭曲件应先进入专门的工艺/几何模型，不宜直接套用下面的直管公式。

## 2. 产品所需的输入、输出与约束

以下是建议的数据契约，不是任何单一厂商的现有接口。

### 2.1 原料数据

| 字段 | 内容及原因 |
|---|---|
| Stock ID / Stock type | 区分真实单根库存和可采购原料类型，避免同一根余料被重复分配 |
| 材料、牌号、批次 | 工艺、追溯与混料限制 |
| 截面与壁厚 | 圆管、矩形管、角钢、槽钢、工字钢及自定义截面的几何兼容性 |
| 实测长度及公差 | 名义 6000 mm 不必然表示可安全使用完整 6000 mm |
| 首尾端面几何 | 余料可能是斜端、鱼嘴端，不能只保留长度 |
| 焊缝/表面方向 | 限制零件旋转或要求关键孔、槽避让焊缝 |
| 缺陷与禁用区域 | 锈蚀、已有孔、压痕或局部不可夹持区域 |
| 原料成本 | 多长度采购时不能仅最小化根数 |
| 可用设备及夹持规则 | 决定该原料和排样方案能在哪台机器加工 |
| 余料估值及回库门槛 | 评价长度、形状、夹持能力、库存需求，而非只按长度 |

### 2.2 零件数据

建议保留 PartType ID、数量、几何哈希、截面规格、BRep、左右端口描述、内部孔槽、允许姿态集合、尺寸/形位容差、坡口工艺、共切许可、优先级和订单归属。

**端口几何相同并不必然代表整个零件可互换。** 两件可能因为内部孔位、焊缝要求或夹持面不同而具有不同的合法姿态和加工条件。

### 2.3 输出不是只有零件列表

每根原料至少输出：原料 ID、按加工/排放顺序排列的零件实例、每个实例的刚体变换、轴向坐标、端口间距、共切关系及审批状态、切割顺序约束、前后余料几何、材料成本、预估加工时间、未满足需求及其原因、求解状态和验证状态。

应分别记录 `solution_feasible`、`geometry_validated`、`process_validated` 和 `optimality_proven_for_model`。这些状态不能压缩成一个“排样成功”。

## 3. 数学模型与几何模型

本节是建议的工程建模。组合优化背景见 [P01][P04a][P04b][P05]；公式的适用假设逐项说明。

### 3.1 经典一维模式模型

令 p 为一根原料上的合法排样模式，a_ip 为模式 p 包含的 i 类零件数量，x_p 为采用模式 p 的次数，q_i 为需求，c_p 为模式成本。

\[
\min \sum_p c_p x_p
\]

\[
\sum_p a_{ip}x_p=q_i,\qquad x_p\in\mathbb Z_{\ge0}.
\]

对原料池 s 的库存限制：

\[
\sum_{p:s(p)=s}x_p\le Q_s.
\]

真实单根余料可以视为数量为 1 的独立原料池。需求用等式是为了禁止未经授权的超产；允许超产或缺货时，应显式增加变量、限制和经济代价，不能悄悄丢件。

一个适合讨论的经济目标是：原料成本 + 加工成本 + 换型/搬运成本 + 交期损失 − 可兑现余料价值。实际实施可优先采用分级目标，先满足硬性需求和安全约束，再比较成本、加工时间与库存。

### 3.2 为什么零件包围盒长度不够

假设某两件轴向包围盒长度各为 1000 mm，其端口允许互嵌 80 mm，在暂不计加工间隙的简化示例中，总占用可能是 1920 mm，而不是 2000 mm。

该 80 mm 属于“两个端口在特定姿态下”的关系。换一个邻居、旋转其中一件、改变割缝补偿或坡口，就可能变化。因此几何服务应返回成对关系：

\[
d(i,\alpha,j,\beta),
\]

而不只是给 i 返回一个固定 `nesting_length`。d 在一般情况下有方向性，不必满足对称性或三角不等式。

### 3.3 姿态必须是物理上允许的刚体姿态

建议先从原料截面、焊缝和工艺约束生成合法姿态集合，再交给组合优化器。

理想圆截面的轴向旋转是连续变量；方形、矩形及开口型材的允许旋转应按截面保持不变的刚体对称性和现场装夹条件确定。固定规格的非正方形矩形管一般不能把 90° 旋转当成无条件合法的排样自由度。存在焊缝、方向性表面或机床限制时，即使几何对称也可能禁止旋转。

“前后翻转”也必须映射成真实的三维刚体变换；不能把 CAD 镜像当成物理翻转而意外生产镜像件。

### 3.4 单区间轴向占据模型下的端口间距

假设两个零件来自同一等截面原料，截面材料坐标为 u。在给定姿态 a 下，零件在沿轴向的材料母线上由一个区间表示：

\[
[l_a(u),r_a(u)].
\]

若 a 在前、b 在后，要求对所有相关 u 都满足：

\[
x_a+r_a(u)+g_{ab}(u)\le x_b+l_b(u).
\]

则最小允许参考点间距为：

\[
d(a,b)=\max_u\{r_a(u)-l_b(u)+g_{ab}(u)\}.
\]

这里 g 是转换到轴向分离约束中的工艺裕量，不是未经转换直接相加的名义割缝。壁厚、坡口和刀具扫掠影响必须被几何表示或保守包络覆盖。

该式依赖“同一材料母线上的前后有序、单区间占据”等条件。只有外表面曲线、没有内壁/厚度信息时，它不自动构成厚壁或坡口实体的完整验证。

### 3.5 非单区间、跨越和非相邻干涉

出现回折端口、局部多段材料占据、异常薄片或复杂结构时，不能无条件使用上面的最大差公式。

一维区间层面，A 的区间为 [a_-,a_+]，B 的区间为 [b_-,b_+]，B 平移 d 后发生内部重叠的 d 区间为：

\[
(a_- - b_+,\ a_+ - b_-).
\]

对各材料母线、各占据区间对汇总，可构造禁止平移区域的表示或保守近似。这是由区间相交关系直接得到的工程建模思路；连续截面上的认证与高效计算仍需几何实现。

对于一般几何，相交随轴向平移未必单调，因此不能在没有单调性证明时，直接对“是否碰撞”进行二分搜索并声称得到最近合法位置。

### 3.6 圆管展开的正确用途

圆管表面可用周期坐标 s=Rθ，周长为 2πR。绕轴旋转对应展开图上的周期平移。它有利于轮廓比较和候选方向搜索，但不是普通无限平面自由排样。

方管各面必须随同一刚体一起运动，跨角点特征不能独立切开后分别旋转。对于厚壁、坡口和曲面端口，展开轮廓可用于加速候选生成，最终仍需由三维/工艺模型确认。

二维 NFP 和碰撞检测可以借鉴，但二维排样器没有自动替你施加周期、截面刚体及机床约束。[P10][O09]–[O12]

### 3.7 固定顺序下的姿态动态规划

当代价可以相邻分解、姿态集合有限、首尾代价已正确建立时，可用：

\[
DP_{k+1}(b)=\min_a\{DP_k(a)+d(a,b)\}.
\]

加上首尾约束即可选择这条固定顺序的最佳离散姿态链。每件最多 q 个姿态时，该递推的复杂度为 O(nq²)。这只是该条件化模型的结论，不是完整机器模型的复杂度。

不能分别把每一对邻居独立调到“最省料”，因为中间零件必须同时满足左右两个邻居，不能在左关系里是 0°、右关系里又变成 90°。

对于一般排布，实际轴向占用应复核为所有放置实体的全局最大轴向坐标减最小轴向坐标，再结合原料端面和工艺边界。不能默认最后一个零件必然定义全局右端。

### 3.8 共切是独立的工艺判断

建议以“单次切割扫掠是否同时生成两个合格保留实体”为根本标准，逐步检查轮廓匹配、方向、割缝补偿、内外壁、坡口刀轴、引入引出、热影响要求、微连桥、夹持和落料。

两个端口可以互嵌但不能共切；两个外表面曲线相同，也可能因内壁坡口不同而不能共切。应将 `common_cut_candidate` 与 `common_cut_approved` 分开存储。[C11]

## 4. 算法路线：各自解决哪一层

### 4.1 贪心与局部修复：必须有的基线

建议实现长度降序、优先余料、最紧适配和最小增量插入等构造策略。它们用于快速给出可行解、提供超时回退方案和衡量高级算法的增益。

对于端口互嵌，插入代价不能只看零件长度，应计算受影响的邻接间距、首尾变化、姿态链及原料长度。标准一维问题的近似保证不能原样套到带几何、余料估值和机器限制的扩展模型。[P05]

### 4.2 动态规划与背包

平端定尺场景可把“生成一根原料的高价值组合”写成有界背包。重复件较多时，按零件类型和数量建模可避免逐个实例展开。

长度离散化会影响状态规模与安全性。建议明确整数单位和误差预算；若采用保守整数化，可向上取整零件消耗、向下取整可用长度，但仍须回原始几何复核。加入成对端口代价后，普通仅按容量的背包状态不足，需要端点/姿态等额外状态。[P04a][P04b][P05][O02]

### 4.3 模式整数规划、列生成与分支定价

模式数量过多时，可只维护当前部分模式，通过定价子问题寻找有改善潜力的新模式。平端场景的定价常可落到背包；复杂端口场景需要把顺序、姿态与几何可行性纳入模式构造。[P04a][P04b][O02]

**精确性边界：**只用启发式定价，没有可靠的定价界，就不能由“暂时没找到新模式”推断完整问题已最优。限制模式集上的整数最优也不等于所有可能模式中的最优。ColumnGenerationSolver 的官方说明也明确区分有界定价与无界启发式定价。[O02]

本报告建议先用列生成/模式池获得高质量解；对小样本或适用模型，再实现带证据的分支定价作为质量标尺。

### 4.4 Arc-flow：经典一维问题的另一条精确路线

Arc-flow 把可行装料组合编码成网络中的路径，并利用图压缩减少规模。相关论文和 VPSolver 实现值得用于经典一维及相关装箱模型。[P06][O03]

代价是长度精度、容量、资源维数和姿态状态增长可能扩大图。不要先把全部三维端口状态都塞进网络，再寄希望于图压缩自动解决。

### 4.5 端口斜切特例：Euler-Splice / Match-Splice

Lewis 与 Bonnet 的 2025 年论文给出了若干特殊单根棒材排序问题的精确算法。改进的 Euler-Splice 对其对应模型为 O(n²)；更一般的 Match-Splice 使用匹配与拼接。[P02]

**不得泛化的三点：**它不是任意端口代价矩阵的多项式最优算法；不是整个多原料分配问题的通用精确解；论文列举的若干代价函数中，有的适用性已证明，有的仅给出经验支持的猜想。引入自己的端口代价后，必须重新核验条件。

它适合作为满足条件的单根原料优化子程序，嵌入外层分组、局部搜索或模式生成，而不应一开始就舍弃特殊结构、全部套通用元启发式。

### 4.6 ALNS / VNS / 模拟退火：建议的工业主搜索方向

这是本报告建议的产品路线，不是已测得的性能结论。余料及额外生产约束下的模拟退火已有相关研究，可作为设计参考。[P09]

建议的邻域包括：移出利用最差的一根料并重新插入；移出高损耗邻接附近的零件；跨原料交换或移动；同类端口聚集/打散；替换原料规格；局部重排与姿态动态规划；在不损害订单约束时合并模式或减少上料次数。

评价应使用增量计算、端口关系缓存和独立验证。昂贵的精确几何尽量只用于可能改进的候选，不能让每次邻域操作都从头做整套布尔运算。

### 4.7 遗传算法

遗传/进化方法值得研究，但关键是编码和解码器：编码原料分组、零件顺序与合法姿态；交叉后修复数量、库存与几何；结合局部搜索，而不是把任何随机排列都当成合法方案。Lewis 与 Holborn 的梯形排样论文提供了与本问题更接近的参考。[P03]

不建议仅凭“遗传算法”这个名称选择路线，也不建议第一版只做一个随机序列解码器便投入生产。

### 4.8 通用整数/约束求解器

OR-Tools、HiGHS、SCIP 可以承担不同层面的组合优化；CPLEX、Gurobi、Hexaly 是可询证的商业优化后端。它们不是 CAD 端口识别或机床后处理库。[O04]–[O06][C14]–[C16]

建议用小规模、受限姿态模型做精确参考，用局部子问题改善工业解。即使离散模型被证明最优，也只证明该模型和姿态集合下的最优，不自动证明连续旋转空间或真实加工过程的全局最优。

## 5. 论文清单及阅读顺序

### P01：顺序相关切损——最贴近通用问题定义

**M. Garraffa, F. Salassa, W. Vancroonenburg, G. Vanden Berghe, T. Wauters.**  
*The one-dimensional cutting stock problem with sequence-dependent cut losses.*  
International Transactions in Operational Research, 23(1–2), 5–24, 2016；在线发表于 2014。  
DOI：`10.1111/itor.12095`。

研究切损依赖切割顺序的一维切割库存扩展，并提出专门考虑该因素的 HSD 模式启发式。用于建立“端口几何产生相邻代价，模式优化处理全局组合”的认识；不是 BRep 端口计算论文。[P01]

### P02：特殊棒材端口排序的精确算法——优先精读

**Rhyd Lewis, Louis Bonnet.**  
*Exact algorithms in bar nesting: How to cut general items from linear stocks so that wastage is minimised.*  
Computers & Industrial Engineering, 200, 110838, 2025。  
DOI：`10.1016/j.cie.2024.110838`。

关注斜切/投影结构下的单根排序与姿态。使用其算法前应核对代价函数的适用条件、允许翻转、首尾模型与单根/多根边界，不能只依据标题中的“general items”扩大解释。[P02]

配套研究代码与数据：Zenodo v2，2025-01-08，DOI `10.5281/zenodo.14616065`，包含 C++、Python、实例生成器、数据和说明书。本次未完成压缩包内部许可及依赖授权核验，列为“公开研究代码，商用嵌入待确认”。[O07]

### P03：多原料分组与单根排序相结合

**Rhyd Lewis, Penny Holborn.**  
*How to Pack Trapezoids: Exact and Evolutionary Algorithms.*  
IEEE Transactions on Evolutionary Computation, 21(3), 463–476, 2017；在线发表于 2016。  
DOI：`10.1109/TEVC.2016.2609000`。

适合阅读特殊单根精确方法如何与多根分组/进化搜索结合。其梯形几何假设不能直接覆盖鱼嘴、坡口和任意开口型材。[P03]

### P04：列生成基础

**P. C. Gilmore, R. E. Gomory.**  
*A Linear Programming Approach to the Cutting-Stock Problem*, Operations Research, 9(6), 849–859, 1961。DOI：`10.1287/opre.9.6.849`。  
*A Linear Programming Approach to the Cutting Stock Problem—Part II*, Operations Research, 11(6), 863–888, 1963。DOI：`10.1287/opre.11.6.863`。

用于理解模式变量、主问题和定价。算法思想重要，不包含现代切管几何或机床模型。[P04a][P04b]

### P05：经典一维问题精确算法综述

**M. Delorme, M. Iori, S. Martello.**  
*Bin packing and cutting stock problems: Mathematical models and exact algorithms.*  
European Journal of Operational Research, 255(1), 1–20, 2016。  
DOI：`10.1016/j.ejor.2016.04.030`。

适合作为组合优化负责人选模型、建立基线、理解精确算法与下界的入口。[P05]

### P06：Arc-flow 及图压缩

**Filipe Brandão, João Pedro Pedroso.**  
*Bin packing and related problems: General arc-flow formulation with graph compression.*  
Computers & Operations Research, 69, 56–67, 2016。  
arXiv：`1310.6887`。

与 VPSolver 配合阅读，评估整数容量状态图路线是否适合自己的长度规模和约束。[P06]

### P07：数值精确性

**Roberto Baldacci, Stefano Coniglio, Jean-François Cordeau, Fabio Furini.**  
*A Numerically Exact Algorithm for the Bin-Packing Problem.*  
INFORMS Journal on Computing, 36(1), 141–162, 2024；在线发表于 2023。  
DOI：`10.1287/ijoc.2022.0257`。

提醒研发区分“浮点计算看起来最优”和“有可靠数值证据的最优”。适合建设小实例精确对照和审查求解界；不直接处理管端几何。[P07]

### P08：余料复用的新综述

**Victor Senergues, Nadjib Brahimi, Adriana Cristina Cherri, François Klein, Olivier Péton.**  
*Cutting stock problem with usable leftovers: A review.*  
European Journal of Operational Research, 328(1), 1–14, 2026。  
DOI：`10.1016/j.ejor.2025.03.014`。

用于建立余料回库、复用、估值和跨期决策的研究地图，而非只使用固定长度阈值。[P08]

### P09：余料和生产约束下的混合启发式

**Massimo Bertolini, Davide Mezzogori, Francesco Zammori.**  
*Hybrid heuristic for the one-dimensional cutting stock problem with usable leftovers and additional operating constraints.*  
International Journal of Industrial Engineering Computations, 15(1), 149–170, 2024。  
DOI：`10.5267/j.ijiec.2023.10.006`。

研究模拟退火、余料和附加运营约束，适合对接成本、优先级和库存业务。不要把论文的实例效果直接当成激光切管项目的预期提升率。[P09]

### P10：二维排样几何基础

**Julia A. Bennell, José F. Oliveira.**  
*The geometry of nesting problems: A tutorial.*  
European Journal of Operational Research, 184(2), 397–415, 2008。

用于理解二维排样几何与无适合多边形等方法，再判断哪些可迁移到周期展开面。不是现成管材排样方案。[P10]

**建议阅读顺序：P01 → P02 → P03；组合优化负责人再读 P04/P05/P06/P07；产品和库存负责人读 P08/P09；几何负责人补 P10。**

## 6. 开源项目与公开研究代码

### 6.1 组合优化优先候选

| 项目 | 公开许可/语言 | 适合承担的任务 | 必须补齐的内容 |
|---|---|---|---|
| `fontanf/packingsolver` | MIT；C++ | 一维下料、不同原料尺寸、余料相关目标等基线 | 任意端口、姿态和机器模型 |
| `fontanf/columngenerationsolver` | MIT；C++ | 模式主问题、列生成及相关搜索框架 | 自定义定价、几何可行性；精确求解需要相应定价界和分支规则 |
| `fdabrandao/vpsolver` | BSD-3-Clause；C++/Python | Arc-flow 建模及求解组织 | 自己选择求解后端；复杂几何状态另建 |
| `google/or-tools` | Apache-2.0；C++及多语言接口 | 小规模参考模型、约束组合与逻辑 | CAD/工艺几何、可扩展大问题分解 |
| `ERGO-Code/HiGHS` | 主体 MIT；C++等接口 | LP/MIP 主问题与受限模式整数求解 | 端口关系与问题建模；具体分发包依赖许可另核验 |
| `scipopt/scip` | 当前公开仓库 Apache-2.0；C/C++ | MIP、可定制分支切割/定价架构 | 较多模型、插件及工程工作 |
| Lewis–Bonnet Zenodo 代码 | C++/Python；本次未确认可用的代码授权范围 | 特殊棒材排序的复现与概念验证 | 许可、依赖、数据输入及工业稳健性 |

来源：[O01]–[O07]。这些是候选组件，不是已完成产品验收的推荐名单。

### 6.2 PackingSolver 的重要源码边界

其说明提到 consecutive items 的 nesting length；但本次检查的一维数据结构中，`ItemType::nesting_length` 是**每个零件类型一个值**，含义是在该件接在另一件之后时扣减的长度。[O01b]

这不等价于任意 `d(i,α,j,β)` 成对矩阵。不能因为出现 nesting 一词，就认定该项目已提供任意鱼嘴/斜端组合的三维紧排能力。

建议先用它做统一输入下的平端和固定消耗基线。需要复杂端口关系时，应核验可扩展接口和算法内部的长度计算，而不是只在最终输出中挪动零件。

### 6.3 二维相关项目：借鉴几何或搜索，不直接当管材内核

| 项目 | 许可 | 有用部分 | 边界 |
|---|---|---|---|
| `Jack000/SVGnest` | MIT | 二维不规则形状排样、NFP/搜索思路 | 浏览器二维项目；没有完整管材周期、壁厚和机床模型 |
| `tamasmeszaros/libnest2d` | LGPL-3.0 | C++二维几何与排样框架 | README 提示功能尚不完整，对孔和凹形等有局限；闭源集成需单独审查许可 |
| `JeroenGar/sparrow` | MIT | Rust 二维不规则条带排样、现代碰撞和搜索实现 | 目标仍是二维；不能由相关 3D 名称推断支持激光切管 |
| `AngusJohnson/Clipper2` | BSL-1.0 | 二维裁剪、偏置等几何基础 | 不是完整排样器，也不是三维 BRep 内核 |

来源：[O09]–[O12]。应冻结具体提交版本、建立几何回归集，再评估依赖库和数值容差。

### 6.4 测试实例

`mdelorme2/BPPLIB` 汇集一维装箱/切割库存实例、代码及相关资源，适合作为经典模型基准入口。[O08]

它不能覆盖鱼嘴、坡口、焊缝方向和卡盘换夹。研究数据、依赖代码与可再分发权限也应分别确认。

**本次检索未能确认一个覆盖“任意 BRep 管端 + 姿态 + 共切补偿 + 坡口 + 多卡盘约束”，且公开许可允许直接产品嵌入、已有完整生产验证的开源引擎。** 这不是对全网不存在此类项目的断言。

## 7. 商业库与可集成引擎

### 7.1 Fraunhofer SCAI / AutoBarSizer

公开定位是可集成的几何优化引擎，面向棒材、型材等线性材料；明确支持斜切件交错排样、截面对称性及旋转限制、多原料库存和余料评价。接口采用 XML，可通过标准输入输出、文件或网络服务连接；官方页面列出 Windows。[C01]

**建议评估定位：**平端与斜切型材的商业引擎候选。任意非平面鱼嘴、STEP/BRep 输入、激光坡口和多卡盘约束的直接支持范围，本次没有充分公开证据，需要厂商用用户案例演示。官网展示的演示 GUI 不代表完整日常生产 CAM。

采购应明确接口、离线部署、OEM 分发、并发许可和维护条款；本次未查得公开统一报价。

### 7.2 Optimalon / CutGLib

公开产品为可集成的优化组件，含一维及矩形二维优化。手册具有截面尺寸、斜切角、180° 旋转、前后翻转及角度匹配控制等接口。[C02][C03]

**建议评估定位：**Windows/.NET 下的平端、斜切试用候选。不能将这些角度参数解释为任意空间曲线/BRep 端口、激光坡口和机床模型。现代 .NET 版本、跨平台、线程安全和回调能力需实际确认。

截至调研日，官网标价：单机 US$190；Server（1 开发电脑 + 1 服务器）US$2500；Site US$3500。官网称 Site 可以随应用免版税分发到不限数量电脑；价格为官网公开标价，不是本报告取得的合同报价。[C04]

Site 分发条款不能自动扩展解释为无限 SaaS 服务器、独立转售求解服务或永久免费升级。签约前核验具体 EULA、部署架构、税费与维护范围。

### 7.3 ALMA / Almacam Nesting Component

ALMA 官方组件介绍明确列出线性切割优化，包括 bars、sections、tubes，因此不能简单说其组件“只有二维”。与此同时，组件品牌站重点展示二维 true-shape nesting；具体线性模块及 API 边界应取得书面规格。[C05][C06]

**建议评估定位：**认真询证的组件候选。要问的是“可以交付哪个可嵌入管材模块、输入是什么、能否外部提供端口成对间距和姿态约束”，而不仅是“ALMA 有没有管材排样”。

ALMA 的 Almacam Tube 是完整管材 CAM 产品；完整产品支持的功能，不能自动推断均已开放成 SDK。[C07]

### 7.4 NestLib：相关候选，但不是本次首选

公开产品资料主要展示二维排样引擎。不能据此证明任意三维管端处理能力；本次也未建立足够充分的最新官方供货与许可核验链，不将其排入前三个管材引擎试用对象。[C13]

### 7.5 通用商业优化库

| 产品 | 建议评估用途 | 不应期待的能力 |
|---|---|---|
| IBM CPLEX / CP Optimizer | 数学规划主问题、逻辑与调度等子问题 | 自动读 BRep 并完成管端互嵌 |
| Gurobi | 通用数学优化后端候选，具体 OEM/运行时许可询证 | 自动提供管材工艺模型 |
| Hexaly | 集合划分、装箱和组合搜索建模候选 | 不建模便直接得到生产排样 |

官方来源见 [C14]–[C16]。开发座席价不等于客户运行时/OEM 分发价，试用、桌面嵌入、云服务、机床随附和经销商转授权必须区分。

## 8. 完整 CAM：用于功能对标，不冒充 SDK

| 产品 | 公开资料可支持的对标方向 | 本次不能据此推断的事项 |
|---|---|---|
| ALMA Almacam Tube | 三维管型、排样、共切、机床仿真等流程 | 全部能力是否可作为独立 OEM 算法组件采购 |
| 柏楚 TubesT | 管材/型材设计处理、排样、补偿及相应加工功能 | 是否提供可独立嵌入其他 CAM 的同等算法 SDK |
| Lantek Flex3D Tubes | 三维管材导入、排样、余料、刀路和 NC 流程 | UI/自动化接口是否等价于可分发算法引擎 |
| SigmaTUBE / SigmaNEST 管材方案 | 真实刀路、共切、夹持死区及生产规则与排样的关系 | 产品许可是否包含底层求解器再分发 |

来源：[C07][C09][C10][C11]。

采购评测时，应让完整 CAM 与候选组件共同处理同一批经确认的输入和约束，再比较。不能让一个产品开启互嵌、另一个仅按包围盒排样，却归因于“优化算法水平”。

## 9. 建议的自有架构

以下全部是针对切管 CAM 研发的工程设计建议。

### 9.1 模块边界

```text
订单 / 零件 / 原料库存
          ↓
材料、截面与工艺兼容分组
          ↓
几何预处理：端口、截面、允许刚体姿态、端口特征签名
          ↓
端口关系服务：候选检索 → 最小间距/可行间距 → 共切候选
          ↓
组合优化：原料选择 + 分组 + 顺序 + 姿态
          ↓
独立整体验证：实体保留 + 割缝/坡口 + 夹持/支撑 + 切割顺序
          ↓
可执行排样 / 刀路生成 / 报表 / 余料回库
```

在现有 C++/OCCT 几何架构中，建议让端口关系服务依赖几何抽象接口；外层优化器只接触稳定数据结构、姿态 ID 和查询接口。以后替换组合优化器或几何内核时，不必重写全部业务流程。

### 9.2 端口关系服务契约

```text
QueryPairRelation(
    frontPartGeometryHash,
    frontPoseId,
    rearPartGeometryHash,
    rearPoseId,
    stockProfileVersion,
    processProfileVersion,
    tolerancePolicyVersion
) -> {
    status,
    pitchLowerBound?,
    feasiblePitch?,
    axialSafetyMargin,
    geometryWitness?,
    commonCutCandidate,
    restrictions,
    verificationLevel,
    diagnosticCode
}
```

`pitchLowerBound` 是有根据的乐观下界；`feasiblePitch` 是已确认可行的放置间距。两者概念不同，不能把粗采样所得间距直接标成“精确最小值”。如果没有认证下界，则留空，而不是填一个看似精确的数字。

`geometryWitness` 可记录限制间距的端口点、曲线段、面或碰撞见证，方便解释“为什么还差 0.2 mm”，也便于调试工艺公差。

### 9.3 几何计算性能策略

优先按端口特征和姿态去重，按需计算成对关系；对候选采用分层包围体、解析端面或保守包络先筛选，再做局部精化和完整验证。缓存键必须含几何、姿态、截面、壁厚/工艺与容差版本。

不能只把碰撞采样点加密一次，就声明完全可靠。认证可来自解析上界、曲线/曲面细分误差界、区间包络或更精确几何复核。对无法认证的候选，应保守退让或者返回待验证，不应默认为可行。

### 9.4 推荐的混合求解流程

```text
validate inputs and units
partition demand into compatible material/profile/process groups
build a fast feasible solution and keep it as incumbent

while time remains:
    generate or update promising single-stock patterns
    optimize discrete poses for selected fixed sequences
    improve grouping with relocation / swaps / destroy-and-repair
    periodically solve a restricted master problem
    validate candidate improvements independently
    replace incumbent only with a fully acceptable improvement

perform final geometry/process checks
return validated incumbent, unmet demand, reasons, metrics, proof status
```

中途取消和超时应返回最近的已验证可行解。库存不足、某件在所有原料中都不可加工、工艺配置不完整时，要明确返回原因，而不是交付一个少件的“成功方案”。

### 9.5 与加工规划形成受控反馈

排样器可以先输出共切候选，由加工层确认；不通过时给出禁止关系、最小间距修正或加工顺序约束，再做局部重排。应设置迭代上限和回退，避免“排样—后处理—排样”无限振荡。

对卡盘尾料，第一阶段可采用保守固定不可用长度；后续再引入移动卡盘、换夹、支撑以及末件无特征区域的利用。不能把简单 `L-固定尾料` 标成完整零尾料优化。[C11]

## 10. 基准测试与验收

本节是建议的测试方案，所有时间档位、规模和门槛都应由实际产品要求确认，不是已完成测试结果。

### 10.1 分层测试集

| 层级 | 数据来源/构造 | 目标 |
|---|---|---|
| A：纯一维 | BPPLIB，另加真实平端订单 | 验证数量、库存、组合优化、可复现性 |
| B：斜切 | 参数化梯形、斜切方管/型材、Lewis–Bonnet 研究实例 | 验证排序、方向和特殊算法条件 |
| C：异形端口 | 圆管鱼嘴、方管跨面槽口、双舌头、台阶、不同内外端线 | 验证端口关系和实体不干涉 |
| D：真实加工 | 坡口、割缝补偿、引线、微连桥、焊缝、支撑与换夹 | 验证最终可加工性 |
| E：生产库存 | 多原料长度、有限库存、异常余料、急单和混合批次 | 验证业务约束与总成本 |

A 类公共实例来源 [O08]，B 类研究资源来源 [O07]；C–E 类需要自建并保留现场复核结论。

### 10.2 公平对比条件

固定硬件、线程数、随机种子、时间预算、长度单位、原料库存、割缝、头尾修边、旋转许可、共切开关、余料定义及交付需求。分别记录纯求解时间和包括几何预处理的端到端时间。

可设置交互、常规、深度三个预算，例如 1 s、10 s、60 s；这些只是建议档位。比较每个预算下的可行解，而不是某算法跑一分钟、另一算法跑一秒。

### 10.3 指标不能只有“利用率”

建议同时记录采购/投料成本、原料根数与规格、不可回收废料、可用余料的数量与价值、加工时间、换型/上料次数、几何与工艺验证失败数、首解时间、最终时间和内存峰值。

**禁止用包围盒长度之和除原料长度作为互嵌排样的物理利用率。** 由于轴向包围盒可以重叠，该值甚至可能超过 100%。

可报告净材料体积比：

\[
\eta_{net}=\frac{\sum V_{finished\ parts}}{\sum V_{consumed\ stock}},
\]

但要注明它受孔槽等固有去除体积影响，不等价于优化算法可控制的损耗。余料应另列，不应隐去库存占用成本。

同样，使用 `ceil(包围盒长度总和/原料长度)` 作为互嵌问题下界可能错误。对同截面等长原料，基于实际成品材料体积的物理下界可以作为弱参考；更强下界需来自有效松弛或有认证的定价模型。

### 10.4 必备回归案例

| 案例 | 必须核验的预期 |
|---|---|
| 名义长度恰好装满，但计割缝后超长 | 不能接受超长方案 |
| 原料首端可直接作为成品端，另一个需要修边 | 切割次数和损耗不能统一按 n−1 处理 |
| 同一中间件左右邻接要求不同角度 | 必须统一该件姿态，不能局部分别最优 |
| 矩形管误允许 90° 旋转 | 截面/工艺约束应拒绝 |
| 几何对称但焊缝方向有限制 | 应禁止不合法状态 |
| 外端线相同、内壁坡口不同 | 不能只按外线批准共切 |
| 粗离散无碰撞、精确曲线存在尖峰 | 必须被保守界或最终验证捕捉 |
| 非单调相交区间 | 不能用未经证明的二分搜索遗漏禁止区 |
| 非相邻零件存在干涉 | 整体验证必须发现 |
| 两个订单竞争同一根余料 | 库存只能使用一次 |
| 余料足够长但无法夹持 | 不能按长度直接判为可用 |
| 零件能放进几何长度但刀头碰卡盘 | 不能输出加工就绪 |
| 共切省一刀却导致零件提前掉落 | 加工顺序/支撑应否决 |
| 超时或取消 | 返回最近已验证的可行解 |
| 需求不足以用完原料或原料不足 | 不允许静默超产、少产 |
| 参数改变后沿用旧缓存 | 缓存必须失效并重新验证 |
| 连续角度离散后求得最优 | 只能声明该离散模型最优 |
| 启发式定价没有新模式 | 不得声称完整问题最优 |

### 10.5 质量等级

建议内部区分：合法解；优于基线；在限定时间达到目标；具备有效最优性差距；对明确模型证明最优。带几何和机器约束的结果首先必须合法，非法的“更高利用率”不参与排名。

## 11. 厂商询证与概念验证材料

### 11.1 要求厂商回答的关键问题

| 范畴 | 具体问题 |
|---|---|
| 输入几何 | 接收长度/角度、二维轮廓、网格，还是 STEP/BRep？内外壁和坡口如何表示？ |
| 邻接模型 | 接受任意有向成对间距和姿态约束吗？能调用外部几何服务吗？ |
| 自由度 | 支持哪些绕轴旋转、翻转及截面对称性限制？焊缝限制能否表达？ |
| 共切 | 只是端面匹配，还是包含割缝、坡口和最终保留实体验证？ |
| 机床 | 固定尾料，还是移动夹持/换夹/支撑状态？由谁验证？ |
| 原料与余料 | 有限库存、不同端面、不同价格、重复利用和唯一 ID 如何处理？ |
| 工程接口 | DLL/C API/.NET/进程/XML/服务？可取消、可复现、线程安全、可中间取解吗？ |
| 授权 | 开发、运行时、机床 OEM、桌面分发、SaaS、离线及经销商权利分别如何约定？ |
| 维护 | 问题响应、回归样例、版本冻结、兼容性、升级费用和停止供货如何保障？ |

### 11.2 三组必须提供的挑战样例

第一组只做平端，固定全部割缝与库存条件，证明基础能力。第二组给斜切件，并要求输出合法方向与真实间距。第三组给曲面鱼嘴、不同内外壁端线和机器限制，要求明确哪些由 SDK 原生处理、哪些由用户回调、哪些完全不支持。

厂商仅给出利用率截图，不足以验收。至少应交付可解析排样坐标、姿态、使用的库存 ID、端口间距、未满足需求、约束配置和可复现参数。

## 12. 知识产权与许可风险

公开专利也可用于了解工程问题拆分。与主题相关的公开文本包括 `CN112418530A`（管材零件排样优化）、`CN108465944A`（管材共边切割套料路径优化）、`CN119721317A`（考虑斜切的管型材切割优化）。[IP01]–[IP03]

专利文本不是算法性能证明，也不是自由实施许可。商业实施前应按权利要求、目标地区和有效法律状态做专业核验。本报告不对侵权与否给出结论。

对开源组件，应核验固定版本的许可证、静态/动态链接、修改和通知要求、第三方求解器与几何依赖。论文开放访问不自动意味着附属代码允许商业嵌入；公开下载也不等于明确授权。

## 13. 建议的分阶段研发路线

| 阶段 | 范围 | 可验收交付物 |
|---|---|---|
| A：数据与基线 | 平端、多库存长度、割缝/修边、余料 ID、精确需求 | 统一数据结构；基线求解器；独立验证器；一维基准集 |
| B：斜切与姿态 | 参数化斜切、合法翻转/旋转、成对间距、固定序列姿态 DP | 斜切回归集；特殊算法适用性判定；商业组件对照 |
| C：异形端口 | 圆管/方管及开口型材真实端口、缓存和整体干涉 | 几何查询接口；认证/保守等级；可解释失败原因 |
| D：加工联动 | 共切补偿、坡口、支撑、夹持和换夹 | 加工验证反馈；实机验证样例；保守回退方案 |
| E：全局优化 | 模式池/列生成、ALNS、多成本、库存和批量生产 | 统一时间预算的对比报告；长期回归；成本收益评估 |

不建议承诺“某月完成全管型零尾料最优排样”，应先固定管型、端口类型、姿态、机器和几何精度边界，再估算人员与周期。

## 14. 最终建议

**开源自研路线：**先用 PackingSolver 形成对照；采用 HiGHS/OR-Tools 等建立适合的问题模型；需要模式级扩展时重点评估 ColumnGenerationSolver；把端口间距、合法姿态、共切和机器可行性留在自有几何/工艺层。

**商业试用路线：**优先 AutoBarSizer、CutGLib、ALMA 的线性/管材组件。前三者的优先级表示“值得花时间验证”，不是本次未执行的性能排名。

**论文路线：**先读顺序相关切损、Lewis–Bonnet 2025 和 Lewis–Holborn 2017，再补列生成、Arc-flow、余料与数值精确性。

真正需要形成长期资产的是：稳定的端口与加工约束表达、可扩展的优化接口、独立验证器和真实客户回归集，而不是单独拥有某一种启发式算法。

---

## 附录：可追溯来源目录

以下地址用于定位官方页面、论文与仓库。动态网页和主分支会变化；实际选型应固定版本与访问快照。未在目录中列出未经核验的下载镜像。

### 论文

[P01] Garraffa et al., sequence-dependent cut losses：`https://onlinelibrary.wiley.com/doi/10.1111/itor.12095`

[P02] Lewis & Bonnet 2025，作者机构全文：`https://orca.cardiff.ac.uk/id/eprint/175006/1/CAIE2025.pdf`

[P03] Lewis & Holborn，作者全文：`https://rhydlewis.eu/papers/IEEETEC2016.pdf`；机构书目：`https://pure.southwales.ac.uk/cy/publications/how-to-pack-trapezoids-exact-and-evolutionary-algorithms/`

[P04a] Gilmore & Gomory 1961：`https://pubsonline.informs.org/doi/10.1287/opre.9.6.849`

[P04b] Gilmore & Gomory 1963：`https://pubsonline.informs.org/doi/10.1287/opre.11.6.863`

[P05] Delorme et al. 2016，机构书目：`https://research.tilburguniversity.edu/en/publications/bin-packing-and-cutting-stock-problems-mathematical-models-and-ex/`

[P06] Brandão & Pedroso，Arc-flow：`https://arxiv.org/abs/1310.6887`

[P07] Baldacci et al.，数值精确算法：`https://pubsonline.informs.org/doi/10.1287/ijoc.2022.0257`

[P08] Senergues et al.，余料综述：`https://www.sciencedirect.com/science/article/pii/S0377221725002115`

[P09] Bertolini et al.，混合启发式：`https://growingscience.com/beta/ijiec/6624-hybrid-heuristic-for-the-one-dimensional-cutting-stock-problem-with-usable-leftovers-and-additional-operating-constraints.html`

[P10] Bennell & Oliveira，几何教程：`https://www.sciencedirect.com/science/article/pii/S0377221706012379`

### 开源、研究代码与数据

[O01] PackingSolver：`https://github.com/fontanf/packingsolver`

[O01b] PackingSolver 一维数据结构，本次检查的主分支路径：`https://raw.githubusercontent.com/fontanf/packingsolver/master/include/packingsolver/onedimensional/instance.hpp`

[O02] ColumnGenerationSolver：`https://github.com/fontanf/columngenerationsolver`

[O03] VPSolver：`https://github.com/fdabrandao/vpsolver`

[O04] OR-Tools：`https://github.com/google/or-tools`；官方装箱示例：`https://developers.google.com/optimization/pack/bin_packing`

[O05] HiGHS：`https://github.com/ERGO-Code/HiGHS`

[O06] SCIP：`https://github.com/scipopt/scip`

[O07] Lewis–Bonnet 代码与数据 v2：`https://zenodo.org/records/14616065`

[O08] BPPLIB：`https://github.com/mdelorme2/BPPLIB`

[O09] SVGNest：`https://github.com/Jack000/SVGnest`

[O10] libnest2d：`https://github.com/tamasmeszaros/libnest2d`

[O11] Sparrow：`https://github.com/JeroenGar/sparrow`

[O12] Clipper2：`https://github.com/AngusJohnson/Clipper2`

### 商业组件及 CAM

[C01] Fraunhofer AutoBarSizer：`https://www.scai.fraunhofer.de/en/business-research-areas/optimization/products/autobarsizer.html`

[C02] CutGLib 产品：`https://www.optimalon.com/cutting_optimization_library.htm`

[C03] CutGLib 手册：`https://www.optimalon.com/Examples/CutGLib.pdf`

[C04] CutGLib 授权及官网标价：`https://www.optimalon.com/CutGLib_license.htm`

[C05] ALMA 组件范围：`https://almacam.com/library-web-app/software-components/almacam-nesting-component/`

[C06] ALMA 组件品牌站：`https://www.almacam-components.com/`

[C07] Almacam Tube：`https://almacam.com/software/3d-cam/almacam-tube/`

[C09] 柏楚 TubesT：`https://www.bochu.com/en/soft/tubest/`

[C10] Lantek Flex3D Tubes：`https://www.lantek.com/us/tubes-pipes-cad-cam-nesting-software`

[C11] SigmaNEST 管材技术资料：`https://www.sigmanest.com/en/news/tackling-tube-nesting`

[C13] NestLib 公开产品资料，当前供货及许可待再次确认：`https://nestlib.geometricglobal.com/modules/nesting-engine/`

[C14] IBM CPLEX：`https://www.ibm.com/products/ilog-cplex-optimization-studio`

[C15] Gurobi licensing：`https://www.gurobi.com/product/licensing`

[C16] Hexaly 官方装箱示例：`https://www.hexaly.com/templates/bin-packing-problem-bpp`

### 公开专利文本

[IP01] `https://patents.google.com/patent/CN112418530A/zh`

[IP02] `https://patents.google.com/patent/CN108465944A/zh`

[IP03] `https://patents.google.com/patent/CN119721317A/zh`
