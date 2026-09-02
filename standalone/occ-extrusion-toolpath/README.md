# OCC Extrusion Toolpath

这是一个独立的 C++20 / Open CASCADE 项目，用于把拉伸类管材 BRep 拆解为加工特征、可扫掠单元和连续变姿态刀路。项目只依赖 OCCT，不依赖 iCAX 中的任何现有工程或业务数据结构。

完整算法步骤、判定条件、失败规则和当前工程边界见 [`SOLUTION.zh-CN.md`](SOLUTION.zh-CN.md)。

## Work / Job / Pipe

整零件分析现在是显式流水线。`AnalysisJob` 保存源 Shape、当前 Shape、正反规范化变换，以及各阶段产生的轴向面、截面分段点、FacePatch、FeatureGroup 和诊断。`Work` 是单一阶段接口，`Pipe` 负责按顺序执行、失败短路和耗时报告。

默认 Pipe 包含七步：

1. `PreprocessWork`：使用 `ShapeFix_Shape` 修复 Wire、PCurve 和 Face 方向，执行 `SameParameter` 并验证 BRep；不调用 `BRepAlgoAPI_Cut/Fuse` 或 `BOPAlgo`。
2. `NormalizeWork`：识别拉伸轴，把轴旋转到 +Y，将零件中心移到原点，并执行统一尺寸缩放；Job 同时保存 `SourceToNormalized / NormalizedToSource`。
3. `SectionProfileWork`：收集所有沿拉伸轴不变的 Face，从它们的横向边投影、去重和闭环得到 outer/inner section wires；完整端面只是闭环失败时的回退。
4. `FaceSplitClassifyWork`：提取截面 Edge 的分段点；当分段点落在轴向宽面的横向 Rail 内部时，沿 Y 生成分割 Edge，并通过 `BRepFeat_SplitShape` 物化为多个 FacePatch；再分类为轮廓面或加工面。
5. `FeatureGroupingWork`：把全部加工 Face 交给 OCC `BRepBuilderAPI_Sewing`；用 `Modified()` 保留来源映射，并直接把 `SewedShape()` 中每个独立 Face/Shell 作为一个加工特征组。几何重合但原本 `IsSame=false` 的边会在这里统一为共享拓扑。
6. `SweepFaceDecomposeWork`：在每个已 Sewing 的特征组内部，一次性合并同域碎 Edge，再在边界/曲面的 C2 语义断点处实体分割，以保留组内相邻 Face 的共享 Edge。一个隐藏为单 Face 的 C1 跑道侧壁会得到直线、半圆、直线、半圆四个 Face。
7. `ToolpathWork`：目标实现先保留 raw `TopoDS_Edge` 身份并建立 Face 局部 `EdgeUse/MachiningSegment`。每个就绪任务以 `Face + 加工轨迹 + 有序关键点` 分析直纹面，为关键点逐个输出母线并找到被加工边；随后建立有向 `EdgeTransition`，把相邻关键母线之间的区域逻辑标记为 Through/Partial SweepSlice，再生成临时刀路并在特征组内连接、合并为零到多条刀路链。当前 0.1 源码仍是单 Face 逐 Edge 试验兼容实现，特征级状态求解尚待落地。

每个 Work 都能单独替换或插入自定义阶段，`ExtrusionToolpathAnalyzer::Analyze` 只是默认 Pipe 的兼容封装。

## “可扫掠单元”的定义

本项目中的可扫掠单元不是“刀姿恒定的平面”，而是同时满足下列条件的加工面单元：

- 拓扑上存在一条横切母线族的 CuttingRail；另一端 Rail 允许退化为点，两条 Rail 也允许在两个端点相交。侧边不是必需条件。
- 几何上，在两条 Rail 的对应参数之间存在一族直线 ruling，并且拟合误差小于容差。
- 工艺上，每条 ruling 的内部采样点都在截面材料域中，不能穿过内孔或材料外部。
- 侧边按几何直线性判断。它在 OCC 中可以是 `Geom_Line`，也可以是几何上共线的 B-spline/NURBS Edge。
- Rail 不要求是直线，可以是一般曲线或多段复合曲线。
- 加工面不要求是平面。刀具方向取每个采样点对应的 ruling 方向，因此梯形面会作为一个单元输出连续变化的刀姿。
- “三边/四边 Face”指外 Wire 的逻辑边界段数，不是 raw Edge 数。一个逻辑 Rail 可以由 Sewing 分出的多个原始 EdgeUse 组成。
- 当前范围不考虑内 Wire。普通分解结果不应超过四条逻辑段；三段对应 Fan，四段对应 Strip，无侧边只允许已证明的 DoublePinched/Periodic 等专门类型。

当前输出的拓扑类型：

- `Strip`：两条非退化 Rail，例如梯形加工面。
- `FanToPoint`：出口 Rail 退化为点，例如两个侧边在三角形顶点相交；`FanFromPoint` 已预留给后续的单元反向和特征级拼接。
- `DoublePinched`：无侧边的两条 Rail 在两个端点相交，端点材料深度为零。
- `Periodic`：无侧边的闭合轨迹，例如完整圆柱面的圆形 CuttingRail。

## 核心 API

最贴近特征识别阶段的入口正是 `featureGroup + sectionWires`：

```cpp
#include <etp/Analyzer.hxx>

etp::ExtrusionToolpathAnalyzer analyzer;

etp::FeatureAnalysis feature = analyzer.CompileFeatureGroup(
    featureGroup,
    sectionWires);
```

当上游已经知道拉伸轴方向时，建议使用显式截面坐标系，避免仅凭 Wire 平面恢复轴正负方向时的二义性：

```cpp
etp::FeatureAnalysis feature = analyzer.CompileFeatureGroup(
    featureGroup,
    sectionWires,
    sectionFrame,
    featureIndex);
```

当上游已经选定单个 Face 的加工轨迹时，公共接口只返回成功或失败；失败时会清空输出，成功时 `toolpath` 包含完整的 XYZ、IJK 和材料深度：

```cpp
etp::Toolpath toolpath;
if (analyzer.TryBuildToolpath(
        face,
        cuttingTrajectory,
        sectionWires,
        sectionFrame,
        toolpath))
{
    // toolpath.Poses 已完整生成
}
```

轨迹使用 OCC 复合曲线按弧长精确采样，不会把圆弧采样点再用直线插值成弦。

特征组目标实现中的直纹面分析由已经选定的加工段驱动。输入是 Face、加工段精确复合轨迹，以及需要显式求母线的有序关键点；被加工边是输出：

```cpp
bool AnalyzeRuledFace(
    const TopoDS_Face& face,
    const TopoDS_Wire& machiningTrajectory,
    const std::vector<TrajectoryKeyPoint>& keyPoints,
    RulingAnalysis& outAnalysis);
```

成功时 `outAnalysis.KeyRulings` 与输入关键点数量和顺序完全一致，并包含 MachinedBoundary 和可继续补算母线的 RulingMap。此阶段不含切透/半切透语义。

语义阶段按相邻关键母线构造逻辑 SweepSlice，再调用：

```cpp
bool GenerateToolPathSlice(
    const TopoDS_Face& face,
    const RulingAnalysis& ruling,
    double firstRulingParameter,
    double lastRulingParameter,
    CutMode mode,
    Toolpath& outToolpath);
```

两个内部接口仍只返回 `true/false`，失败时输出清空。

## 特征组内的 Edge 语义

特征级解析严格区分三层数据：

- raw `TopoDS_Edge` 只表示最终 Sewing/拆分后的拓扑身份。
- `EdgeUse = (EdgeId, FaceId, Wire occurrence, Orientation)` 保存同一 Edge 在当前 Face 中是入口、出口还是侧边。
- `EdgeTransition = (EdgeId, SourceEdgeUse, TargetEdgeUse)` 保存源到目标是 `Bevel`、`HalfCutBottom` 还是 `Continuation`。

因此，同一 raw Edge 在 Face A 中可以是出口，在 Face B 中是入口，在 Face C 中是侧边；A→B 可以是坡口，A→C 可以是半切透底。不能给 raw Edge 写一个全局唯一业务角色。

解析从有完整轮廓逻辑段的 Face 开始。后继 Face 只有在它自己的某一整条逻辑段被 incoming Transition 完整覆盖后才会进入分析。多个上游出口 `{e1}`、`{e2}`、`{e3}` 可以共同补齐目标 Face 的逻辑入口 `{e1,e2,e3}`；前两部分到达时必须等待。纯 `HalfCutBottom` 覆盖的目标是内底面，不生成刀路。

每张非底面可扫掠 Face 先产生临时刀路；其 `Through/Partial` 来自出口 Transition，而不是单看曲面。随后只有端点、拓扑顺序、IJK、CutMode 和工艺参数都兼容的路径才连接或合并。分叉和互不连通部分保留为多条 `ToolpathChain`。

关键点不是普通刀路采样点，而是“必须显式输出母线”的逻辑事件：加工轨迹首尾、组成加工段的 raw EdgeUse 公共顶点，以及上游指定的分段点。相邻 KeyRuling 围出最小 SweepSlice。若被加工边的语义后来出现新分界，使用已有 RulingMap 补算一根关键母线即可。

Through/Partial 混合只做逻辑拆解：即使对面 Rail 只有一个 raw Edge，也只把它引用为多个参数区间，不创建新 OCC Edge/Vertex/Face，因此不会破坏 EdgeUse、Transition 和直纹面缓存。实体拆分只用于修复几何不可扫掠、凹域或母线多区间。

整模型入口会先拆 Solid：

```cpp
TopoDS_Shape model = etp::ReadCadFile("part.step");
auto parts = analyzer.AnalyzeAssembly(model);
etp::WriteAnalysisJson(parts, "toolpaths.json");
```

需要控制或检查中间结果时直接使用 Pipe：

```cpp
#include <etp/Pipeline.hxx>

etp::AnalysisJob job;
job.Id = "part-0";
job.SourceShape = part;
job.Options.InputLengthScale = 1.0; // inch 输入可设为 25.4

auto pipe = etp::MakeDefaultPipe();
etp::PartAnalysis result = pipe.Run(job);

// 可检查 job.SectionBreakPoints、job.FacePatches、
// job.FeatureGroups 和每个 Work 的 job.Reports。
```

## 构建

要求：CMake 3.24+、支持 C++20 的编译器、OCCT 8.x（7.8/7.9 也可按目标名调整）。

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DOpenCASCADE_DIR="C:/OpenCASCADE/cmake" `
  -DETP_BUILD_TESTS=ON
cmake --build build --config Debug --parallel
ctest --test-dir build -C Debug --output-on-failure
```

命令行：

```powershell
build/Debug/etp_cli.exe input.step output.json `
  --tolerance 0.001 --samples 33 --scale 1.0
```

本仓库的 CMake 不硬编码 OCCT 位置；`OpenCASCADE_DIR` 由使用者或父构建环境传入。

## 已验证场景

构造测试覆盖：

- 矩形管拉伸轴与 outer/inner 截面 Wire 恢复。
- 梯形加工面保持为一个 `Strip`，刀具方向沿路径变化。
- 三角形加工面生成点 Rail 的 `Fan` 单元。
- 侧边在 OCC 中是 B-spline、但几何上共线时仍能识别。
- 七阶段 Pipe 的顺序、中间状态、耗时报告和 2 倍尺寸规范化。
- 截面轮廓中间分段点将一个轴向宽面物化拆成两个轮廓 FacePatch。
- 两个独立构造、几何邻接但原本不共享 `TopoDS_Edge` 的 Face 经 OCC Sewing 后成为一个组，并真正共享 Edge。
- 上下分割错位的七 Face T 形网格经 Sewing 后为一个组，组内 Sweep 后仍保持七个 Face，不因 T 点额外拆面。
- 一个 C1 复合 B-spline 跑道侧壁单 Face 按 C2 语义断点物化为四个四边 Face，并仍按共享 Edge 归为同一加工特征组。
- 半圆柱面配半圆轨迹成功输出恒定轴向 IJK；同一圆柱配轴向轨迹因平行母线而返回 `false`，且不泄漏半成品刀路。
- 无侧边圆柱 Face 的两条 Rail 在两个端点相交时生成 `DoublePinched` 刀路。

## 当前工程边界

这是可运行的第一版算法内核，而不是已经覆盖所有脏 STEP 的生产版 CAM：

- 自动截面优先由轴向面的横向 Edge 闭环；当模型存在完整端面时可回退使用其 Wire。两种方式都失败时返回 `section.not-closed`，不会退回布尔截面。
- 当前语义分割能处理显式拓扑断点以及 C2 连续性断点。若上游把直线/圆弧用一张全局 C2 光顺近似 NURBS 模糊掉，仍需要后续加入按公差的分段拟合器。
- 已支持解析圆柱、圆锥、线性拉伸面、平面侧边插值，以及存在 degree-1 参数方向的 B-spline/Bezier 直纹面；参数方向不沿母线的一般自由直纹面尚未加入渐近方向场求解。
- 单 Face 的周期和 DoublePinched 已接入；多个子 Face 刀路自动拼成一条特征级闭合路径、机床轴限位、碰撞、割缝补偿和进退刀段仍不在这一版内。
- 加工段关键点驱动的 RulingAnalysis、EdgeUse/EdgeTransition 固定点求解、逻辑 SweepSlice、K 坡传播、Through/Partial/InnerBottom 判定、多个上游边补齐目标逻辑段及组内路径连接/合并已经形成完整设计，但尚未写入当前 0.1 代码。
- 只有模型本身不能唯一确定拉伸轴时（例如无孔正方体），结果会带 `axis.ambiguous` 诊断；生产接口应允许上游固定轴。

当前已接入的失败边界会以结构化诊断返回；本次新增的特征级目标方案也为未覆盖逻辑段、Transition 歧义和未解析 Face 规定了诊断语义，不允许静默生成看似完整的刀路。
