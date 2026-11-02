# Classify 切割单元设计诊断工具

## 整件查看与导出（2026-10-25）

当前消侧边／合并规则为 v2：完整重合的公共直侧边，只有在两个单元整体共面且该侧边两端的切割边界都通过有向 G1 检查时才可消去。共面检查读取原始 BREP 全支撑面证据，对整组固定基准平面核验，距离及法向角容差分别为 1e-7 mm、1e-7 rad；侧边端点匹配仍为 0.001 mm，G1 容差仍为 0.001 rad。平面 BSpline/Bezier 使用全部正权控制点证书，解析平面使用有限 UV 包络，不能以显示网格或只有边界共面替代。证据不充分就保留侧边。新合并可形成 OpenBand、ClosedBand 或条钢端面 UnpairedRegion；直线和相切圆弧仍保留各自逻辑边。

`refresh_merge_evidence.py` 为现有边界分组补充与输入哈希绑定的共面证据，不重跑轮廓/皮上角色采样。`run_stage1_units.py` 在原输入及几何组织完全相同时复用既有导出；仅分组变化时复用经过源哈希、面号、面积和网格内容核对的原 0.03 mm 显示网格，原面仍逐一执行 BREP 回读。`axisConstraints` 保存所有未消侧边的精确源边引用与方向约束，`mergeDecisions` 保留共面／两端相切的通过和拒绝依据。它们是几何方向约束，实际刀轴朝向与机床姿态留待摆放及机构阶段确定。

检查 STEP 的复用还必须由 `inspection-audit.json.inspectionExporterSha256` 证实实际生成版本与当前显示导出器一致；几何未变或外层 stamp 已更新，均不能替代这项证据。没有生成版本记录的旧显示文件不复用。旧样条扫管在 CAD Assistant 中曾产生跨空白区域的伪线，当前样条粗线使用已有显示专用分段描边；原始 BREP 与单元规则不受影响。`run_unit_inspection.py --only <模型编号...> --workers 3 --publish` 可仅刷新显示 STEP，逐项核对完整 BREP 哈希不变。

CAD Assistant 检查导出使用 `export_unit_inspection.cpp` 和 `run_unit_inspection.py`，从已有组织结果生成，不重算单元合并。检查 STEP 保持一个模型根节点，内部按单元分组；`FACE_F` 原面与 `DISPLAY_` 标记分开，可逐项隐藏。青色皮上粗标记按模型对角线、单元面宽（2×面积/外边界长度）和当前边界长度自适应缩放，半径分别不超过其 0.15%、6%、2.5%，上限 0.3 mm。橙色侧边为沿原曲线弧长裁出的较细断续标记，虚线间距跟随实际尺寸，每链约 6–55 段；紫色内部边为普通曲线，粉色内部接缝为单独断续标记。小于内核分辨率的标记保留精确曲线，不抬高显示半径。粗线与断续线使用显示几何表达，避免 CAD Assistant 丢弃 STEP 线宽与虚线属性；这些标记不属于加工几何，不进入原始 BREP，也不能当作刀路或被再次 Classify。`inspection-audit.json` 的 `markerStyles` 逐组记录实际半径、虚线段长、局部面宽与采样精度。原面回读独立检查面数、面积、颜色、有效性，模型根节点及单元层级另外核验。

`TestData/Tube/CutUnits/<原模型名>.step` 发布已核验的检查 STEP；`.brep` 仍是同一份纯加工面模型。查看器下载检查 STEP，去掉原先按单元、原面或边界分文件的 BREP 下载入口。历史诊断中间文件留在 `output/tests`。

全库生成后运行 `publish_stage1_catalog.py`，在同名 JSON 中补齐原面、单元和待处理原因，并生成 `TestData/Tube/CutUnits/检查清单` 中的模型、单元、待处理面 CSV。最后运行 `validate_stage1_units.py` 核对实际交付的检查 STEP、整件 BREP、单元成员归属与发布目录的一对一覆盖。`repair_stage1_batch.py` 可指定独立修复版原生程序，只补跑失败导出；它不重算已成功模型。

显示粗线扫管失败时，保留精确源曲线并增加显示专用分段圆柱描边；请求采样偏差跟随标记半径缩小，最大 0.002 mm，实际请求、中点偏差与源边编号记录在检查审计 `displayStrokeFallbacks`，不得将其作为原始加工几何误差证书。合并边界导出独立检查源曲线端点在 0.001 mm 内闭合；没有共享拓扑顶点时使用保持原边的诊断 Compound，`chainTopology` 记录几何闭合与拓扑状态，不修改原 edge/vertex 容差。

用户检查用的整件结果固定发布到 `TestData/Tube/CutUnits/`，与 `Classify` 同级，采用原模型名的 `.brep`、`.step`、`.json` 三个文件；不按单元分文件。生成脚本自动发布，也可用 `publish_stage1_artifacts.py --only 3` 发布已经生成的完整结果。`output/tests` 继续保存诊断中间数据及网页资源。

“单元总览”按原坐标显示该模型的全部 Classify 加工面；面色区分单元，图例可点选且不退出全局视图。侧边为橙色虚线（1.8 px），皮上为青色粗实线（3.5 px），内部边为紫色普通实线（1.25 px）。三种检查层共用角色线型。网页虚线按每条逻辑路径的实际弧长独立归一化，约 8 段，缩放模型或视角不会把短侧边画成整条实线；不相连的路径不连线，反向重合侧边保持一致的间隙。已合并的公共侧边从单元外边界中移除；内部接缝单独开关，不改写原面边界角色。线宽通过匹配 r185 的 Three.js LineSegments2 实际绘制，避免 WebGL 原生线宽在设备上固定为 1 px。

当前仅 case 3 新增 `cut-units.brep`：一个根 Compound，两个一级单元 Compound，完整保留 3 个加工原面及位置。不是完整原始实体，不补造不加工皮面。回读验证子组、面数、面积、各面有效性和重心。`cut-units.colors.json` 按一级子组遍历序号记录单元编号、原面编号、颜色和源边角色，含 BREP/源文件哈希；BREP 本身不存名称、颜色或线型。`whole-model.zip` 只含一份完整 BREP、上述显示信息、完整加工面彩色 STEP 和说明。其他 269 个模型的几何结果未重算。

入口：`http://127.0.0.1:8768/?revision=whole-model-20261025#case=3&face=3&view=whole`。已实查 case 3 的全局两单元选择、case 18 / F5 的侧边虚线及局部放大、case 151 / F11 的内部普通实线与皮上粗线。

## 单元合并原型（2026-10-25，当前仅应用 case 3）

`cut_unit_assembly.py::CutUnitAssembler` 位于原面边界分析之后。Face 仍是分析对象；CutUnit 可以引用多个原始面。原面上的 Skin / Side / Internal 保持不变，单元另外保存外边界 `boundaries` 与内部接缝 `internalSeams`，二者不可混为一种分类。

当前完整直侧边合并流程：建立几何重合对应（0.001 mm 原位置容差，要求已证明直线的逻辑边，两端完整匹配）→ 检查每条接边只对应一个邻面 → 构建相连面组 → 公共 Side 转内部接缝 → 用剩余原边重新拼闭环 → 要求两个闭合 Skin 环、无分叉，区域欧拉数为 0。重复外边界、未决边、残留不支持的边界角色、缺口、多重邻面会拒绝本次合并，并记录 `mergeDecisions`。不按面积大小删面，不要求两面共面，不自动决定刀路能否连续。

case 3 的结果为 **A001 = F1 + F3**，外边界 C1/C2，内部接缝 S1；F2 另为一个单面单元。3 个原面共得到 2 个单元、0 个待处理面。A001 的 BREP 原样保存两张曲面，面积约 2415.28208 mm²；两条新边界 wire 均做闭合、有效性及 BREP 回读检查。源边使用共 123 次：121 次归外边界、2 次归接缝两侧，完整保留。

数据包含 `sourceRegion.faceIds`、`assemblyId/key`、`boundaries`、`internalSeams`、`mergeRule`、回读记录与原面 `initialOrganization`。每张原面显示相同单元归属；汇总按唯一单元计数，CSV 的 `unitCount` 是面关联单元数，`countedUnitCount` 用于避免合并单元被重复计数。

查看器支持合并前/后对照、成员面同色、选任一原面时查看整个单元、合并 BREP 下载和内部接缝开关；原始逻辑边仍可追溯。例如 F3 的 E179 仍为原面 Side，但在 A001 中归入 S1。`test_cut_unit_assembly.py` 验证实际 F1/F3、源边覆盖、坐标/遍历顺序不变性、面积非阈值、未决/内部/皮上边不吞并、多重邻面、缺口、重复区域及非环带拒绝。

当前只应用于 case 3，其他 269 文件保留上一轮结果；页面明确显示“合并规则已更新 1 个模型”。全库索引当前为 12,491 个唯一单元、1,890 个待处理面。完整区间的曲线接边、部分重合、带孔/分支/开口区域合并、全面自交检查等仍待实现，未将此原型视为通用生产合并器。下文 12,490 / 1,892 是合并前的基线记录。

## 当前第一步：边界组织与可视检查（2026-10-25）

新输出独立保存在 `output/tests/cut-unit-stage1-20261025`，不覆盖下文历史 UV 分片结果。查看入口为 `http://127.0.0.1:8768/?revision=stage1-20261025#case=18&face=5`。

本轮完整重跑 270 个当前 Classify 输入和 version 4 逻辑边分类，共 14,382 个原面。当前几何组织规则得到 12,490 个单元：12,133 个 OpenBand、204 个 ClosedBand、153 个 UnpairedRegion；另有 1,892 个原面待处理。这里的“单元”表示已组织的覆盖区域与边界，尚未证明直母线可加工，也没有实际刀路。当前实现只接受整体原面满足组织规则的情形；复杂面如何拆成多个区域仍待实现，不把每个原面一律当作成功单元。

核心结构和调用关系：

1. `face_boundary_probe.cpp`：先按几何连续性组织 TopoEdge 使用次序，再分类 Skin / Side / Internal。Side 不要求直线；整组 Skin 优先。保留原 Edge ID、使用方向及轮廓查询证据。
2. `cut_unit_stage1.py::FaceBoundaryAnalyzer`：建立带 `W:G` 标识的逻辑边，保留原始边，检查边界覆盖、闭环连接，排除接缝与退化事件。
3. `BoundaryChain`：沿环顺序把连续的 Side 组成 EndBoundary，把连续的非 Side 组成 CutBoundary；环首尾相同角色合并。只组合引用，不合并底层直线/曲线几何，不引入面内分界。
4. `CutUnitBuilder`：单环、两条 EndBoundary 与两条 CutBoundary 交替 → OpenBand；两闭环、无 EndBoundary、有皮上锚点 → ClosedBand；全 Skin 无 Side → UnpairedRegion。其余边界组织、缺口、无皮上锚点等保留 Pending 和明确原因。Internal 始终保留为终止约束，不自动标记半切透或打标。
5. `export_stage1_units.cpp`：将原面原样导出为独立单元或待处理 BREP，按原有方向导出链的 wire BREP，生成按 F/U 标识着色的 STEP。单元 BREP 逐面回读、有效性及面积校验；0.03 mm 三角网格仅用于显示。
6. `stage1_viewer.mjs`：切割单元 / 边界链 / 逻辑边 / 原始碎边四层查看，点选追溯、单独显示、筛选待处理、下载 BREP、同步视角的新旧对照。历史几何必须匹配同一源文件哈希才展示。窄窗口提供“模型列表”和“面信息”抽屉。

F5 实例：32 条原始边 → 12 条逻辑边 → 2 条切割边界链与 2 条侧边链 → 1 个完整 OpenBand；同一输入的旧算法为 3 片。F2 仍为 44 原始边 → 16 逻辑边 → 1 个完整区域。曲线侧边示例为 case 43 / F1。

2026-10-25 局部核查：case 3（001-E-22-2#1.step）F3 的 C1 对应真实直边 E179，长 2.4027326 mm，两端在外皮，最大离皮采样距离 0.0189953 mm。它与 F1 的 E1 直线几何重合，F1 面积仅 0.0304286 mm²。故 Side 判定符合当前位置规则，但 Side 不自动证明加工起止边。数据内部沿用 `EndBoundary` 名称；查看器显示为侧边链并说明端部作用未证实。该例仍待处理，未通过放宽 0.001 mm 容差吞掉小面，也未自动合并 F1/F3。

`stage1_edge_evidence.mjs` 为可视检查补充直边重合查询、局部尺寸与小平面轮廓图。只匹配已由几何探针确认为直线的开放逻辑边，核对两端位置；这是几何对应证据，不冒充共享 TopoEdge 或流形实体邻接。局部图若放大偏离方向，会明确显示相对倍率；三维放大保持真实比例。点击边界后可“放大边界及重合邻面”。`test_stage1_edge_evidence.mjs` 用实际 E179/E1 检查匹配、尺寸、刚体变换、不重合端点及非直线排除。新模块发布为 `.js`，避免 Windows 本地服务器将 `.mjs` 作为纯文本提供。

```powershell
cmake --build output/tests/cut-unit-design-20261023/build --config Release --target face_boundary_probe export_stage1_units audit_colored_units --parallel 2
<python> src/tools/testing/cut_unit_design_probe/run_stage1_units.py --prepare --workers 4
<python> src/tools/testing/cut_unit_design_probe/run_stage1_units.py --reuse --workers 4
<python> src/tools/testing/cut_unit_design_probe/test_stage1_units.py
<python> src/tools/testing/cut_unit_design_probe/validate_stage1_units.py
<python> src/tools/testing/cut_unit_design_probe/publish_stage1_viewer.py
<python> -m http.server 8768 --bind 127.0.0.1 --directory output/tests/cut-unit-stage1-20261025
```

`validation.json` 本轮检查 270 文件全部通过：原始/快照哈希、14,382 个原面完整覆盖、52,789 条链的源使用次序归属覆盖、14,376 个显示网格索引，以及全部彩色 STEP 的面数/名称/颜色/面积/有效性回读。6 面三角化失败，保留可点选源边界和 BREP。10 项核心回归包括环起点循环变换、世界坐标旋转平移、曲线 Side、碎边不丢失、内部约束和缺口/多端部组织拒绝。链 BREP 在此核验中检查文件存在和源使用映射，尚未逐条执行独立 BREP 回读审计。

限制：皮面接触是每条原始边 49 点采样，不是连续证书；当前只记录共享 TopoEdge 的邻接，分离面的几何对应尚未求解。边界配对、近似加工、摆放、机构、工艺和刀路连接均在后续阶段。以下内容是保留的历史诊断与算法，勿与本轮计数混用。

## 全库着色检查

恢复查看时先运行 `refresh_changed_classify.py`：按源文件哈希只更新变化项，先在 `refresh-*` 目录生成并核验，全部通过后将旧输入、边界和单元保存到 `previous-*`，再替换对应输出。随后运行 `reindex_classify_units.py` 重建完整索引，再运行核验与报告脚本。`--stage <已有 refresh 目录>` 可继续已核验的分项，避免中断后重复计算。

查看器提供“正视选中面”（非平面按局部法向）、独立单元缩放，以及无网格原面的可点选边界。区域候选会明确显示尚未逐面确认划分，母线默认关闭。页面不会把几何分区成功等同于加工单元语义通过。

边界探针复用精确曲线投影器，并按完全相同的截面坐标缓存距离与材料域结果，不作坐标舍入。整组已经满足 Skin 判定时直接采用皮上优先规则，不再计算不影响角色的材料内外状态；此时 `bodyStatesEvaluated=false`，零计数不表示已确认全部在外部。该优化通过 4 个更新文件的角色和几何指标对比、9 个角色样例验证。进度写入 `<边界 JSON>.progress.txt`。

`run_classify_units.py` 读取本轮快照的边界分组，调用 `batch_cut_units` 生成独立单元 BREP、彩色 STEP 和逐面查看数据。先 `--prepare` 建立当前 Classify 的快照与哈希，再运行全库；`--reuse` 仅复用已经完成的同一快照输出。当前不输出生产刀路。单个源面无法导出或三角化时保留为待处理，不中断整份文件。

```powershell
cmake --build output/tests/cut-unit-design-20261023/build --config Release --target batch_cut_units audit_colored_units --parallel 2
<python> src/tools/testing/cut_unit_design_probe/run_classify_units.py --prepare --workers 4
<python> src/tools/testing/cut_unit_design_probe/run_classify_units.py --workers 4
<python> src/tools/testing/cut_unit_design_probe/validate_classify_units.py
<python> src/tools/testing/cut_unit_design_probe/publish_classify_viewer.py
<python> src/tools/testing/cut_unit_design_probe/report_classify_units.py
<python> -m http.server 8767 --bind 127.0.0.1 --directory output/tests/cut-unit-design-20261023/classify-cut-units
```

浏览器打开 `http://127.0.0.1:8767/`。页面支持文件与面筛选、原面 F／单元 U 点选、旋转／平移／缩放、整件／单面／单元隔离、边界角色和采样母线开关，以及独立 BREP／彩色 STEP 下载。窄窗口使用文件下拉框和上下布局。

平面双环面优先检查绕孔的周期扫掠，避免将笛卡尔 UV 极值制造的窄条直接当成独立单元。`planar-radial-ring` 保留原始平面裁剪区域，以内环 UV 包围盒中心为径向候选中心；使用精确 pcurve／射线相交检查 128 个均匀方向，以及每个源边端点方向两侧的事件探针，要求每条射线与内外环各交一次且两交点间位于原面内。母线检查仍是有限采样，不是连续证书。三环及以上、多区间、交线不确定的情况不会由此规则合并；继续使用既有诊断路径。`radial_ring_tests` 覆盖同心、偏心、薄环、椭圆、双孔排除及非星形区域排除。

修复既有快照时，可运行 `refresh_radial_rings.py` 只重跑含完整平面双环的文件，再运行 `reindex_classify_units.py` 更新全库统计，并重新运行回读核验和报告脚本。重跑后会移除该输出目录中已被替代的单元／待处理面文件，避免旧 BREP 与新清单混淆；不清理原始 Classify 输入。

结果在 `output/tests/cut-unit-design-20261023/classify-cut-units`。`summary.json` 为最终统计；`validation.json` 为全库源哈希、面覆盖、网格索引、STEP 回读有效性／面数／名称／颜色／面积核验；`README.md` 为检查索引。原始 Classify 文件不修改，`inputs` 是本轮副本，编号以副本原始 face 遍历顺序为准。

当前全库查看快照为 270 文件、14,382 面、15,286 个区域候选；其中只有本轮 F2 被按下述逻辑边规则复核。其余旧几何分片不得据此当作已验收切割单元。浏览器实查覆盖 F2 的 16 个边组编号、逐组高亮和每处理由。导出回读结论见输出目录的 validation.json；它只证明导出几何的一致性，不代替单元划分语义验收。

F2 复核运行 `review_logical_face.py --case 4 --face 2`，输出 `f2-logical-proof`。44 条源边 → 12 条直线＋4 条圆弧（每弧 8 个碎段）→ 1 个无侧边单元、16 条皮上切割边、0 条新增面内分界。`logical_grouping_tests` 验证相切直线/圆弧仍分组、共线碎段合并、同圆碎弧合并及不同相切圆分开，并读取实际 F2 检查 44/16/12/4 计数。`logical_boundary_tests` 验证 UV 方向旋转和世界坐标旋转不改变单元结论、BREP 双向差区域为零，以及侧边、内部边、未决和带孔输入不会被无侧边规则接受。只重做选中的 F2，不重跑其他面的区域划分。

用途：直接读取现有 Classify STEP 中的加工面和精确截面轮廓，给详细方案提供可复核的几何证据。工具不运行已有的直纹面刀路识别器，不修改 Classify 文件，不输出可生产刀路。

本工具依赖仓库已有 OCCT 8.0 安装，使用 Windows / Visual Studio 2022。Python 汇总脚本需要 NumPy 和 Pillow；Codex 桌面所带 Python 已提供这两个库。

从仓库根目录执行（将 `<python>` 替换为可用的 Python 3.12 解释器）：

```powershell
cmake -S src/tools/testing/cut_unit_design_probe -B output/tests/cut-unit-design-20261023/build -G 'Visual Studio 17 2022' -A x64 -DOpenCASCADE_DIR=D:/penguinchinage0609/iCAX/src/third_party/opencascade/install/vs2022-x64/cmake
cmake --build output/tests/cut-unit-design-20261023/build --config Release --parallel 2
<python> src/tools/testing/cut_unit_design_probe/run_probe.py --workers 4
<python> src/tools/testing/cut_unit_design_probe/verify_fixtures.py
<python> src/tools/testing/cut_unit_design_probe/summarize.py
<python> src/tools/testing/cut_unit_design_probe/validate_artifacts.py
```

结果位于 `output/tests/cut-unit-design-20261023`。`manifest.json` 保存原文件哈希和逐例状态；`cases/NNN.json` 是工作副本面边索引、原面索引映射、轮廓距离、二面角和曲率／弦线采样。重跑失败例可用 `--retry-failed`，此前失败保存在 `priorAttempts`。

`fixtures.cpp` 单独构造 K、X、带底槽、不带底面槽和条钢端面，验证定向凹凸及缺底面规则。这些解析样本不冒充实际 Classify 零件。

采样偏差不是严格误差上界。缝合可能修整边界，权威源文件保持不变。二维 UV 内外判定使用 OCCT 的工作副本分类器；完整精确裁剪、区间证书和机床可进入性属于详细方案的正式实现内容。

探针对退化／无三维曲线的边统一走保护分支；正式算法必须进一步区分合法退化顶点和缺失曲线。图中的颜色表示边界几何关系，不直接表示最终切透／半切透模式。

## Face 优先、先连续分组后分类的诊断

`face_boundary_probe.cpp` 是独立入口，直接遍历原始 face/wire，不 Sewing。先按几何种类、端点相接和有向切线连续性建立逻辑边；直线与相切圆弧之间断组。再对完整组作角色诊断，分组函数不接收截面或角色标签。

当前 `version=4` 使用几何种类边界＋G0/G1 分组，角色规则为：整组在皮上优先归 Skin；开放组至少一端接皮、主体在材料内部归 Side；整组在材料内部归 Internal。**Side 不限制直线性。** 原生 BSpline/Bezier 的直线辨认使用正权控制多边形共线证据（1e-7 mm），不能将直线与相切曲线混成一组；解析圆弧合并核对圆心、半径和法向。材料域由精确截面闭环的内外查询确定。旧版全库 JSON 不冒充已按 version 4 重跑；本轮仅更新 F2 的边组记录。

`chordSampledError`、`sampledBacktrack`、`chordUpperBound` 独立评估直线替代，不影响角色。有限采样误差不是上界；只有输出 `chordBoundKnown=true` 时控制多边形包络上界才可使用。

```powershell
cmake --build output/tests/cut-unit-design-20261023/build --config Release --target face_boundary_probe --parallel 2
<python> src/tools/testing/cut_unit_design_probe/run_face_combinations.py --export
<python> src/tools/testing/cut_unit_design_probe/analyze_example_sweeps.py
<python> src/tools/testing/cut_unit_design_probe/analyze_multiwire_examples.py
<python> src/tools/testing/cut_unit_design_probe/draw_face_combinations.py
<python> src/tools/testing/cut_unit_design_probe/validate_face_combinations.py
```

结果写入 `output/tests/cut-unit-design-20261023/face-combinations`：`cases` 保存每张 face/每个 wire 的逻辑边及源 edge 使用方向；`manifest.json` 保存原文件哈希；`faces.csv` 可逐面筛选；`summary.json` 保存七种组合统计；`checks.json` 保存 9 个解析样本检查；`examples` 保存实样原始单面 BREP、带截面的 STEP、角色图、候选母线及查询记录。`--reuse --export` 复用本次原始诊断 JSON 重新汇总和导出，不重新运行 270 个文件。

本次共诊断 270 文件、14,757 面；14,310 面全部物理逻辑边完成三分类，447 面保留未决。七类实样各生成候选母线；两张多孔实样检查扫掠区间分叉；028.step F3 检查样条曲面的初始配对偏差。候选母线使用原始 face 的点到裁剪面距离检查，17 条母线、每条 33 点；它不能替代连续误差和覆盖检查。`validate_face_combinations.py` 复核源文件哈希、遍历覆盖、汇总计数、导出证据和报告链接。

`face-boundaries` 保留的是旧版直线 Side 规则的历史证据，不用它统计当前定义。上面的 `run_probe.py` 则是原有凹凸/曲率诊断，和本次原始 face 编号的关系通过其 `sourceFaceIndices` 查询。

位置、直线、皮上容差各为 0.001 mm，切向角度容差为 0.001 rad，截面材料域查询容差为 1e-7 mm。整组直线性检查使用解析线或正权控制多边形；皮上检查每个源 edge 按参数采样 49 点。该探针只检查 edge 连接处的 G0/G1，未完成单条 edge 内部的非 G1 分段、连续皮面证书及正式切割单元生成，不能作为生产验收。首次运行组合脚本前，需要本目录基础诊断已生成文件清单和 `analytic-cases/bar-end.step`。

## 七个选定实样的单元 BREP

```powershell
cmake --build output/tests/cut-unit-design-20261023/build --config Release --target build_cut_unit_examples --parallel 2
<python> src/tools/testing/cut_unit_design_probe/materialize_seven_units.py
<python> src/tools/testing/cut_unit_design_probe/present_seven_units.py
```

输出在 `output/tests/cut-unit-design-20261023/seven-cut-units`，压缩包为同级 `seven-cut-units.zip`。每例分别有 `unit-01.brep`、`unit-01.json`、`source-face.brep`、`units-colored.step` 和原面/单元对照图。`--only 2` 可单独重新生成某个组合，不重复检查其余样本。

这是七个明确选例的几何构造器，不是全库自动拆分算法：按已选逻辑边的源 edge 和方向构造连续轨道，生成沿 v 方向一次的 BSpline 扫掠面；U 形面投影控制点，弓形面将一侧轨道退化成端点，其余配对原始轨道。七例各形成一个单元，所有 BREP 保持原坐标，并重新读取后绘图。核对 BREP 有效性、回读面积、双向布尔差区域、25×33 个母线样本，以及 STEP 回读的面数/面积/颜色。

七例本身均为平面；彩色 STEP 使用已核对覆盖相同的原始平面裁剪表达，生成的直纹参数化保存在 BREP 中。这样避免扇形两条相邻退化参数边在 STEP 回读时造成面积缩水。母线绘图不代表实际入射或工艺路径；本轮不包含原生 CAD 查看器和机床验收。
