# Classify 后：以 Face 为对象的逻辑边解析与切割单元生成

2026-10-23。当前确定的解析主线是 **Face → Wire → 连续逻辑边 → 侧边/皮上边/内部边 → 边界分区 → 切割单元**。本文按用户明确的先后关系重写。已完成独立诊断工具及实样检查，尚未接入生产算法。

## 1. 先固定对象和顺序

~~~text
一张 Classify 加工 Face
  └─ 逐个 Wire，保持环内方向和次序
      └─ 按几何连续性将 topo_edge/span 组织为 EdgeGroup
          └─ 对整个 EdgeGroup 分类
              1. 整组在皮上 → Skin
              2. 一个或两个组端点在皮上，除端点外的主体位于材料内部 → Side
              3. 整组位于材料内部、不接皮 → Internal
                  └─ 用 Side 分隔环，收集 SkinChain / InternalChain
                      └─ 结合该 Face 的几何构造切割单元
~~~

**Side 的判断对象是 EdgeGroup。** 不能先按原始 edge 识别皮上/侧边/内部，再合并同类；也不能先找直线碎段再建立其余逻辑边。

整条在皮上的边优先归 Skin，这是用户已确认的规则。因此条钢断面四条边全部为 Skin，Side 可以为空。

侧边不再要求是直线。Skin/Side/Internal 描述逻辑边与皮的连接关系；直线性、直线近似偏差是另外的几何属性，供后续切割单元及激光加工可行性求解使用。轻微弯曲的侧边仍然是侧边，不能因为非直线而被塞进 Internal。

分析入口保留 FaceId。邻面凹凸和截面关系是它需要查询的信息，不改变以 Face 为核心的组织方式。精确直纹求解、非直纹近似和摆放选择都发生在边界解析之后。

## 2. 最小数据结构

以下为拟新增接口；EdgeId、FaceId、Curve 等复用现有中性几何资源，不将 TopoDS 类型暴露到 CAM 公共接口。

~~~cpp
struct SEdgeSpanRef
{
    uint64_t EdgeId;
    ParameterRange Range;
    bool Reversed;                     // 沿当前 wire 的使用方向
};

struct SLogicalEdge
{
    uint64_t Id;
    vector<SEdgeSpanRef> Parts;          // 按连续顺序保存所有源碎边
    bool Closed;
    optional<Point3> Start, End;        // 闭合逻辑边没有人为的加工端点
};

enum class EFaceEdgeRole
{
    Skin,
    Side,
    Internal,
    Unresolved
};

struct SClassifiedEdgeGroup
{
    SLogicalEdge Geometry;
    bool WholeOnSkin;
    bool WholeStraight;                // 独立几何属性，不参与 Role 分类
    bool StartOnSkin, EndOnSkin;
    EFaceEdgeRole Role;
};

struct SBoundaryChain
{
    vector<uint64_t> GroupIds;          // 保留逻辑边本身，不再次抹掉拐角
    bool Closed;
};

struct SWireBoundaryAnalysis
{
    uint64_t WireId;
    bool Outer;
    vector<SClassifiedEdgeGroup> OrderedGroups;
    vector<uint64_t> SideGroups;
    vector<SBoundaryChain> SkinChains;
    vector<SBoundaryChain> InternalChains;
};

struct SFaceBoundaryAnalysis
{
    uint64_t FaceId;                    // 始终由这张 face 承载分析
    vector<SWireBoundaryAnalysis> Wires;
    vector<SBoundaryIssue> Issues;      // 缺失曲线、未遍历段、奇点等
};
~~~

Geometry 和分类结果分开，确保分类器只能在逻辑边建好后运行。Parts 保存源 EdgeId、参数区间和方向，后续仍能查询 pcurve、面邻接和材料侧。

EdgeGroup 是一个有序复合曲线，不要求立即将多个 topo_edge 改造成一条新的 topo_edge。MakeWire 可以组织拓扑，但它本身不会决定哪些连接属于同一逻辑边。

## 3. 连续性分组先独立完成

核心类：

~~~cpp
class CLogicalEdgeBuilder
{
public:
    vector<SLogicalEdge> Build(
        const SOrderedWire& wire,
        const SContinuityOptions& options);
};
~~~

Build 的输入没有 Skin、Side 或 Internal 标签，也不需要截面轮廓。

本次诊断先使用以下连续性判据：

~~~text
位置连续：distance(Ei.end, Ej.start) ≤ εposition

切向连续：
dot(tangent(Ei.end), tangent(Ej.start)) ≥ cos(εangle)
~~~

切线必须按 wire 的行进方向取值，处理 reversed coedge；不能取点积绝对值，否则折返也会被认为连续。G0 相接但存在拐角时断组。2026-10-23 用户进一步明确：**直线与相切圆弧仍须分成两个逻辑边，不能仅因 G1 连续而合组。** 同一直线或同一圆弧自身的碎段才在满足连接和切向条件时合并；解析圆弧还需检查圆心、半径和所在平面。几何表示类型不等于几何种类：F2 的直线和圆弧都由 BSpline 源边表达，须先辨认直线几何，不能只比较 BRep 曲线枚举。Skin/Side/Internal 分类仍在几何分组之后进行，Side 的语义不因此增加直线限制。

F2 实样复核：`001-E-33-3#1.step` 原面 F2 的 44 条源边组成 **12 条直线＋4 条圆弧，共 16 个逻辑边**；每条圆弧包含 8 条源碎边。全部逻辑边归 Skin。当前按一个平面、一个闭合 Skin 外环、零 Side、零 Internal、零内孔保留为 1 个无侧边单元，保留全部 16 条切割边；16 个边组接点不是面内分割线。摆放前不强制选择单一 UV 母线方向，也不以固定方向的连通分片代替切割单元。逐处理由及原面/单元几何核验见 `output/tests/cut-unit-design-20261023/classify-cut-units/f2-logical-proof/README.md`。该结论仅针对本次 F2 复核，不扩展为全库旧分片已经完成逻辑边验收。

~~~cpp
Build(wire):
    spans = ReadOrderedGeometrySpans(wire)
    // 正式实现还需在原曲线自身的非 G1 点、折返点和奇点处分段。

    for each neighboring pair (a, b), including last → first:
        join[a,b] = PositionContinuous(a,b)
                 && OrientedTangentContinuous(a,b)

    split cyclic sequence at every false join
    if every join is true:
        return one closed EdgeGroup
    return groups
~~~

规则：

- 每个 wire 独立分组；不跨外环、内环，也不跨 face 拼接。
- 直线碎边、圆弧碎边、样条碎边都进入同一个连续性流程。
- 不要求组内曲线类型相同；整组是否直线留给下一步判断。
- 首尾连接也必须检查，否则分组结果会受 wire 起始 edge 影响。
- 退化点保留为顶点事件，缺失曲线保留为错误；不强行拼过。
- 每个有效源 span 必须且只属于一个逻辑边；不能在分组中漏边。
- 后续需要物化 OCC Wire 时，再从 Parts 构造；源 face 和曲线不因分组而修改。

## 4. 建好逻辑边以后，才识别侧边

核心类：

~~~cpp
class CFaceEdgeGroupClassifier
{
public:
    SClassifiedEdgeGroup Classify(
        const SLogicalEdge& group,
        const SExactSection& section);
};
~~~

主判断：

~~~cpp
Classify(group, section):
    if geometry is invalid:
        return Unresolved

    if WholeGroupOnSkin(group, section):
        return Skin

    if !group.Closed
       && OpenBodyInsideMaterial(group, section)
       && (OnSkin(group.Start, section) || OnSkin(group.End, section)):
        return Side

    if WholeGroupInsideMaterial(group, section):
        return Internal

    return Unresolved  // 混合接触、位于材料外或几何不完整，保留具体原因
~~~

OpenBodyInsideMaterial 检查逻辑边除全局端点之外的主体位于截面材料域内部。不能只用“不在皮上”代替“在材料内部”，因为管腔和材料外部同样不在皮上。Internal 包含完全位于材料内部的开放边和闭合边。

WholeGroupIsStraight 单独计算整个复合曲线是否落在同一直线上；它不再是分类条件。只检查 Parts 中每段都是 Line 不够：许多小直线也可能构成折线。后续可记录端点连线替代整组时的最大偏差、折返情况和过切/欠切，而不修改 Side 角色。只是倾斜的直线仍是直线，不要求它平行截面法向或某个坐标轴。

WholeGroupOnSkin 将整组曲线投影到精确截面，判断它是否始终位于截面边界。只检查两个端点会把跨过壁厚的侧边误判为皮上边。

正式实现用解析曲线关系或带误差控制的区间检查。当前诊断的皮上判定是每条源边 49 点采样，皮上容差 0.001 mm，截面材料域查询容差 1e-7 mm，因此输出是分组验证证据，不是连续皮面判定证书。

如果一个连续组出现较长的皮上区段与非皮上区段混合，须保留接触区间并在语义分区时处理；不能通过“分组前给碎边贴标签”绕回旧流程。Internal 不直接等同于半切透。

## 5. 用侧边分隔环，得到皮上链和内部链

核心类：

~~~cpp
class CFaceBoundaryPartitioner
{
public:
    SWireBoundaryAnalysis Partition(
        const vector<SClassifiedEdgeGroup>& orderedGroups);
};
~~~

此时才进行基于角色的分组：

~~~text
连续性分组后的环：
    Skin(G1) → Skin(G2) → Side(G3)
    → Internal(G4) → Internal(G5) → Side(G6)

边界分区：
    SideGroups    = { G3, G6 }
    SkinChains    = { [G1, G2] }
    InternalChains= { [G4, G5] }
~~~

这里的 SkinChain 可以经过 G1/G2 之间的几何拐角，因为它是边界分区，不是把两条曲线宣称为一个连续逻辑边。两次“分组”的含义必须区分：

- **EdgeGroup：纯几何连续性分组，在识别侧边之前。**
- **BoundaryChain：侧边分隔后的角色分区，在识别侧边之后。**

逐 wire 扫描，以 Side 为分隔项；两条 Side 之间根据角色收集 SkinChain 或 InternalChain。若余下部分存在角色变化，保留变化节点继续分段，不能强行把混合区全部归成一种角色。

无 Side 时保留闭合结构。例如条钢断面仍保留四条 Skin 逻辑边，可以属于同一条闭合 SkinChain；不能人为指定两条边为 Side。只有一条 Side 时，在它处打开循环序列，不复制出第二条侧边。多个内环分别保存，不能扁平化成一个边列表。

本轮实样补充：这里按 Side 分隔得到的是边界分类视图，不能直接当成切割单元的扫掠配对。`C-7-2 HW150x150 F1` 的边界为 Side/Side/Side/Internal，其中一条 Side 在有效条带候选中用作轨道，另外两条才作为起止母线。因此必须分开保存 `FaceEdgeRole` 和 `UnitBoundaryUse`，不因选择某个扫掠方向而改写逻辑边角色。

## 6. Face 解析的主流程

~~~cpp
SFaceBoundaryAnalysis CFaceCutAnalyzer::Analyze(face, section)
{
    SFaceBoundaryAnalysis result;
    result.FaceId = face.Id;

    for (wire : face.Wires)
    {
        // 第一步：只处理几何连续性。
        auto groups = logicalEdgeBuilder.Build(ReadOrderedWire(face, wire));

        // 第二步：对已经形成的完整逻辑边分类。
        vector<SClassifiedEdgeGroup> classified;
        for (group : groups)
            classified.push_back(groupClassifier.Classify(group, section));

        // 第三步：侧边分隔边界，收集皮上链、内部链。
        result.Wires.push_back(boundaryPartitioner.Partition(classified));
    }

    CheckSourceSpanCoverage(result);
    return result;
}
~~~

现阶段应该先把这个结果做准确，并能高亮每张 face 的 EdgeGroup 和三类边。切割单元由这个结果往下构造：

~~~text
Face + SideGroups + SkinChains + InternalChains
    → 确定可处理的边界组合与面内区域
    → 精确直纹或非直纹近似分解
    → 摆放后确定入射、刀向、切深与工艺
    → 连接刀路
~~~

因此不再通过扫掠候选反推侧边，也不先把输入限制成三边或四边的直纹面。

半切透后续使用内部边位置、相邻加工面的凹凸及具体加工方向判断。该信息可附着于逻辑边的区间上，但不能反过来干扰本节的连续性分组。

## 7. 已运行的实样验证

使用原始 Classify Face/Wire，不先 Sewing，也不修改原模型。

当前 `face_boundary_probe` version 3 已实现本版材料域三分类，Side 不再要求直线。270 个文件全部重新诊断，结果保存到独立的 `face-combinations` 目录。旧版 `face-boundaries` 只保留为历史分组证据。

| 组合 | 完成分类的 face 数 |
|---|---:|
| 皮上 | 656 |
| 皮上＋侧边 | 8,765 |
| 皮上＋侧边＋内部 | 4,390 |
| 皮上＋内部 | 26 |
| 内部 | 362 |
| 侧边＋内部 | 106 |
| 侧边 | 5 |
| 合计完整分类 | 14,310 |
| 有未决组 | 447 |

总计 14,757 张原始 face，读取失败和 wire 遍历遗漏均为 0。计数不去重同一零件的不同版本；“只有侧边”5 张中含 2 张面积接近零的记录，选取的代表是面积 0.124653 mm² 的弓形小面。447 张未决面包括组内混合接皮或部分位于材料外，不通过扩大容差强制归类。参数缝和退化顶点事件不计入物理角色掩码。

新增七类代表与拆分候选：

- 只有皮上：02-05-052.step F1，变深度条带，保持曲轨道。
- 皮上＋侧边：电子表扶手管 F35，皮上轨道之间的矩形条带。
- 皮上＋侧边＋内部：008.step F107，皮上到内部的条带。
- 皮上＋内部：76.step F11，两个闭合 wire 配成周期环带。
- 只有内部：008.step F11，几何条带，入口依赖邻面暴露。
- 侧边＋内部：C-7-2 HW150x150 F1，Side 和 Internal 配成轨道，其余 Side 作为起止界限。
- 只有侧边：AB52 小边管 F31，曲边和弦边围成小面，可构造端点退化扇形；不拉直曲边。

七例各取 17 条直母线、每条查询 33 个点，到原始裁剪 face 的最大采样距离为 7.844e-9 mm，查询无失败。这只检查候选母线样本，不证明全覆盖或机床可加工。

同一组合还检查了三张复杂实样：H93AESC01113 F1 的全皮上 face 有 4 个 wire，候选方向同站位最多出现 4 段有效区间；AB52 F9 的三类组合有 8 个孔；028.step F3 四组边界的样条面在初始等弧长配对下偏差达 0.180610 mm。它们要求按裁剪区间分叉和拟合误差拆分，不能按角色掩码确定单元数量。

9 个解析检查全部通过，包括三碎边合成侧边、条钢四条 Skin、曲 Side、七种组合和排除管腔。实际全库还记录了 656 个非直线 Side 组、257 张完整分类的多 wire 面。

诊断参数：端点距离 0.001 mm，有向切线夹角 0.001 rad，整组直线偏差 0.001 mm，皮上距离 0.001 mm，材料域查询 1e-7 mm。它们分别设置，不以扩大容差来吞掉浅小特征。曲线的整组直线性使用解析线或正权控制多边形的包络判断；皮上关系仍是有限采样。

本次探针只检查源 edge 连接处的连续性，尚未完整切开单个源 edge 内部的非 G1 点。生产接入需补齐这一项、连续皮面检查、混合接触区间及 BoundaryChain 构造验收。现有结果验证用户指出的“先连续分组，后判侧边”确实必要，不代表全部切割单元或刀路已生成。

证据：[七类实样及具体拆分分析](../../../../output/tests/cut-unit-design-20261023/face-combinations/组合与切割单元分析.md)、[分组统计与实样索引](../../../../output/tests/cut-unit-design-20261023/face-combinations/summary.json)、[逐文件清单](../../../../output/tests/cut-unit-design-20261023/face-combinations/manifest.json)、[解析样本检查](../../../../output/tests/cut-unit-design-20261023/face-combinations/checks.json)、[诊断工具说明](../../../../src/tools/testing/cut_unit_design_probe/README.md)。

## 8. 实施边界

CLogicalEdgeBuilder 和连续性/曲线/截面关系查询属于中性几何能力；Skin/Side/Internal 的加工边界归类、Face 切割单元规则属于 CAM。拟在 CutRecognition 中添加 FaceCutTypes、FaceCutAnalyzer 和边界分类/分区类，业务 SDO 保存分析结果与源几何版本。

初次接入先交付 Face 的逻辑边与边界分区显示、查询和持久化。原有 ExtractSectionWires/Classify 继续提供精确截面与加工 Face；旧 AnalyzeFeatureToolpaths 不作为本流程的前置识别器。
