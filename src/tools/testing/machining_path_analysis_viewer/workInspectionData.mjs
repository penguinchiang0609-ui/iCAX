// Read-only presentation adapters for the saved outputs of each pipeline Work.
// A shared preview format does not change the stage's geometry or its decisions.
export const WORK_LABELS={import:'原始导入',heal:'Heal · 几何修复',align:'Align · X 轴对齐',contour:'Contour · 截面轮廓',classify:'Classify · 加工面分类',edgeAnalysis:'EdgeAnalysis · 边分析',pathAnalysis:'PathAnalysis · 路径与刀轴',faceCutCoverage:'FaceCutCoverage · 逐面覆盖',penetration:'切透不切透的判定'};
const STATUS={passed:'完成',partial:'部分完成',failed:'失败',missing:'产出缺失','not-run':'未执行',running:'未完成'};
const SKIN={OnSkin:'在皮上',OffSkin:'不在皮上',PartialOnSkin:'部分在皮上'};
const CONVEXITY={Convex:'凸',Concave:'凹',Coplanar:'共面／光顺',Free:'自由边'};
const SKIN_COLORS={OnSkin:'#00B8D9',OffSkin:'#F59E0B',PartialOnSkin:'#A855F7'};
const PATTERNS={Convex:'solid',Concave:'dashed',Coplanar:'dash-dot',Free:'dotted'};
const COVERAGE={Through:'切透',Partial:'半切透',Pending:'判定待定'};
const EVIDENCE={SkinExit:'到达另一处皮边',SkinExitBoundarySet:'已证明所有可能出口均在皮上',ConvexBoundary:'到达非皮凸边',SideCompletion:'端点相邻侧边补全',ConcaveTermination:'到达凹边',OtherBoundary:'到达其他边界'};
const COVERAGE_COLORS={Through:'#1fe05c',Partial:'#f59e0b',Pending:'#94a3b8'};
const PENETRATION={through:'Through',blind:'Partial',pending:'Pending'};
const PENETRATION_EXIT={stockSkin:'截面出口',continuedStockSkin:'两侧皮面延伸所得出口',
 fullSectionTermination:'完整截面封端',machiningFaceSkin:'加工面上的实际皮边出口',none:'出口待确定'};
const PENETRATION_REASON={
 'material-cleared-to-wall-exit':'出口之前的扫掠范围已清空',
 'retained-material-before-wall-exit':'所需出口之前仍有材料',
 'coaxial-skin-openings-and-cleared-bore':'同轴的两个皮上孔口之间已清空',
 'closed-bore-floor-before-wall-exit':'截面出口之前有封闭孔底',
 'entry-or-fixed-geometric-axis-unproved':'该入口或固定几何刀轴缺少证明',
 'source-curve-or-swept-face-outside-certified-geometry':'当前几何证明尚未覆盖该来源曲线或加工面',
 'cylindrical-bore-proof-unavailable':'圆柱孔出口或孔底的几何证明未完成',
 'stock-exit-unproved':'截面出口未证实',
 'retained-material-unproved':'出口前的剩余材料分布未证实',
 'machining-face-covers-entire-stock-section':'加工面完整覆盖名义截面',
 'machining-face-covers-stock-exit-strip':'加工面完整覆盖从入口到出口的扫掠带',
 'machining-face-covers-projected-stock-strip':'加工面覆盖截面限制内的整个扫掠带',
 'first-machining-face-boundary-is-source-stock-skin':'沿切割方向的首个加工面边界是原始皮边',
 'entire-machining-face-boundary-is-source-stock-skin':'加工面的全部物理边界都已确认在原始皮上',
 'stock-section-not-a-closed-linear-contour':'现有出口求解尚未覆盖该闭合截面曲线组合',
 'stock-entry-or-first-exit-not-unique':'截面上的入口或首个出口无法唯一确定',
 'finished-brep-does-not-certify-closed-material':'成品 BREP 尚未证明闭合，无法确认剩余材料',
};
// Presentation only: keep native evidence unchanged in the expandable records.
const COVERAGE_EXPLANATIONS={
 'first-boundary-exits-at-stock-skin-including-same-logical-edge-far-span':'首达边界在皮上；同一闭合皮边的远侧区间也可作为出口',
 'first-boundary-is-convex-including-convex-logical-side':'首达边界为非皮凸边，包含满足侧边定义的凸边',
 'first-boundary-side-component-reaches-a-shared-real-skin-trace-endpoint':'首达侧边的来源原生区间或目标侧边原生区间已证连续，并连接到皮边与该侧边共享的真实端点',
 'target-native-component':'补全依据：目标侧边原生区间的精确连通分量；只证明已有到达记录，不填补来源区间的正间隙',
 'whole-source-summary-not-cell-local':'以下为整条来源路径的诊断汇总，不代表本区间的已证确切原因',
 'positive-native-interval-below-kernel-edge-resolution':'严格正长的原生参数区间低于内核可构造展示子边的分辨能力，STEP仅用原曲线起点的点标记表达',
 'native-unique-spline-inverse-refinement-has-invalid-original-domain-or-request':'原生样条求逆细化的原始参数域或请求坐标无效',
 'native-unique-spline-inverse-refinement-requires-an-original-in-range-kernel-seed':'原生样条求逆细化需要位于原参数域内的内核候选值',
 'native-unique-spline-inverse-refinement-original-endpoint-intervals-are-not-strictly-opposite':'原生样条两端的投影外包界未能严格夹住请求坐标',
 'native-unique-spline-inverse-refinement-requires-independent-original-root-uniqueness':'原生样条求逆细化尚缺独立的原域唯一根证明',
 'native-unique-spline-inverse-refined-inside-original-domain-by-D1-newton-and-outward-interval-sign-bisection':'已在原生样条原参数域内，以原生导数修正和外包界符号二分细化唯一根；所得点的投影外包界包含请求坐标',
 'native-unique-spline-inverse-refinement-has-no-representable-interior-parameter':'剩余原生参数夹逼区间内已无可表示的内部参数值',
 'native-unique-spline-inverse-refinement-trial-is-not-strictly-interior':'求逆细化的候选参数未严格位于当前原生夹逼区间内',
 'native-unique-spline-inverse-refinement-original-point-enclosure-failed':'求逆细化未能得到原生曲线点的投影外包界',
 'native-unique-spline-inverse-refinement-point-sign-is-unproved':'求逆细化尚未证明该原生点的投影位于请求坐标哪一侧',
 'native-unique-spline-inverse-refinement-exhausted-iterations-with-positive-native-bracket':'求逆细化已达到迭代上限，仍保留严格正长的原生参数夹逼区间',
 'native-unique-spline-inverse-refinement-original-curve-query-failed':'求逆细化查询原生曲线失败',
 'native-unique-spline-inverse-refinement-original-data-query-failed':'求逆细化查询原始几何数据失败',
 'shared-topological-vertex':'端点身份依据：共享同一个拓扑顶点',
 'shared-native-edge-parameter-event':'端点身份依据：同一原边上完全相同的原生参数事件',
 'ignored-zero-measure-native-source-endpoint-contact':'零长度来源端点接触已排除，不作为区间覆盖证据',
 'bounded-planar-all-skin-boundary-exit':'有限平面域的全部真实边界均为原始皮边，可独立证明切透',
 'bounded-ruled-all-skin-boundary-exit':'有限直纹面域的全部真实边界均为原始皮边，横截母线的区间可独立证明切透',
 'bounded-valid-native-ruled-domain':'原生直纹面域有效且有界',
 'native-degree-one-two-pole-u-generator':'原生 U 参数方向已证明为一次、两排控制点的直母线',
 'native-degree-one-two-pole-v-generator':'原生 V 参数方向已证明为一次、两排控制点的直母线',
 'positive-weights-nondegenerate-ruling-and-full-trim-jacobian-certified':'在完整实际裁剪域内已连续证明权重为正、母线及曲面均不退化',
 'plane-certified-normal-strip-first-boundary':'平面曲边：以原生样条连续证明面内法线族及首达边界',
 'bounded-valid-native-plane-domain':'原生平面域有效且有界',
 'all-physical-native-boundary-intervals-source-attributed-OnSkin':'所有真实边界的完整原生区间均已证明原始皮面归属',
 'source-regular-on-open-native-knot-intervals':'来源曲线在各原生节点开区间内正则',
 'source-regular-between-finitely-many-native-generator-critical-parameters':'除有限个原生母线临界参数点外，来源曲线均正则并横截母线',
 'finite-isolated-source-generator-parallel-parameters-retained':'孤立的母线平行参数点原样保留，不增造刀轴',
 'original-coedge-inward-normal-family':'使用原生有向边界确定的面内向法线族',
 'original-coedge-inward-generator-family':'按原生有向边界在每个横截区间分别确定面内母线方向',
 'skin-exit-exists-on-regular-interior':'正则区间内的切割线必然从皮边退出',
 'exit-correspondence-not-expanded':'切透分类已有证明，具体出口参数对应尚未展开',
 'original-endpoint-axis-records-retained':'原始首尾刀轴状态原样保留',
 'first-boundary-is-concave-and-has-no-valid-end-side-completion':'首达边界为凹边，且没有有效的端点侧边补全',
 'first-boundary-has-no-skin-convex-or-valid-end-side-through-evidence':'首达边界没有皮边、凸边或有效端点侧边补全的切透证据',
 'first-boundary-has-no-proven-exit-or-material-retention':'首达边界为自由边或共面连接，尚未证明是出口还是保留材料，不能据此标为半切透',
 'first-boundary-attributes-are-not-complete-for-coverage-classification':'首达边界属性尚未完整，无法判定此区间',
 'native-skin-source-has-a-local-first-boundary-proof-gap':'此皮边仍有区间缺少切透或保留材料的证明',
 'skin-native-interval-covered-by-geometric-through-evidence':'此皮边原生区间已有几何切透证据',
 'skin-native-interval-fully-analyzed-without-through-evidence':'旧版无切透证据的区间；此依据不足以证明保留材料',
 'skin-native-interval-has-concave-material-retention-evidence':'此皮边原生区间已证明终止于凹边并保留材料',
 'skin-native-interval-has-no-proven-first-boundary-arrival':'此皮边原生区间尚未证明到达哪条首边界',
 'skin-native-interval-has-incomplete-boundary-classification':'此皮边原生区间的首达边界分类尚未完成',
 'plane-line-first-physical-boundary':'平面直线路径：沿已记录刀轴映射到当前面的第一条真实边界',
 'straight-generator-uv-first-physical-boundary':'直母线曲面：沿母线在当前面参数域内映射到第一条真实边界',
 'plane-circle-radial-first-physical-boundary':'平面原生圆弧：沿径向映射到当前面的第一条真实边界',
 'first-positive-signed-radial-boundary-on-current-finite-face':'沿已记录刀轴朝向的径向到达当前面的第一条真实边界（允许经过圆心）',
 'all-native-circle-source-intervals-reach-first-physical-boundary':'全部原生圆弧来源区间均已映射到当前面的第一条真实边界',
 'recorded-endpoint-axis-sense-preserved':'保持已记录的首尾刀轴朝向',
 'native-g1-parallel-corner-end-events-canonicalized':'依据原 BREP 的相切关系和声明公差统一端点事件',
 'plane-mixed-line-circle-logical-driver-pending':'平面来源逻辑边同时包含直线与圆弧，当前尚未完成这类混合路径映射',
 'plane-circle-boundary-native-3d-curve-is-not-line-or-circle':'圆弧径向映射遇到非原生直线或圆弧的目标边界，当前尚未支持',
 'plane-circle-boundary-does-not-preserve-native-pcurve-parameter':'圆弧径向映射的边界未保持三维曲线与面上曲线的原生参数一致',
 'plane-circle-boundary-has-no-native-pcurve':'圆弧径向映射的边界缺少原生面上参数曲线',
 'plane-circle-native-boundary-range-outside-pcurve':'圆弧径向映射的原生边界区间超出面上参数曲线范围',
 'plane-circle-boundary-pcurve-is-not-matching-native-line-or-circle':'目标边界的面上参数曲线与原生直线或圆弧类型不匹配',
 'circle-native-boundary-spans-more-than-one-period':'圆弧目标边界的原生参数区间超过一个周期，当前尚未支持',
 'circle-native-start-vertex-outside-declared-brep-tolerance':'原生边起点与拓扑顶点的偏差超出该边／顶点声明的 BREP 容差',
 'circle-native-end-vertex-outside-declared-brep-tolerance':'原生边终点与拓扑顶点的偏差超出该边／顶点声明的 BREP 容差',
 'circle-topological-endpoint-source-parameter-not-unique':'拓扑端点对应多个来源圆弧原生参数',
 'circle-target-native-parameter-at-angular-event-not-unique':'圆弧角度分界处未求得唯一的目标边原生参数',
 'circle-source-native-parameter-at-center-not-unique':'目标位置位于来源圆心，不能对应唯一的来源圆弧参数',
 'circle-source-native-parameter-at-target-event-not-unique':'目标边分界位置对应多个来源圆弧原生参数',
 'circle-source-native-parameter-at-target-event-not-solved':'目标边分界位置对应的来源圆弧原生参数未求出',
 'circle-first-boundary-collapses-to-a-native-point':'径向首达边界在该区间内退化为单个原生点，未形成目标边区间',
 'circle-target-native-interval-has-no-complete-edge-analysis-attributes':'前序边分析属性未覆盖圆弧映射目标的完整原生参数区间',
 'circle-driver-requires-native-plane-and-recorded-plane-axis-mode':'圆弧径向映射要求原生平面及前序记录的平面刀轴模式',
 'circle-physical-target-native-edge-missing':'圆弧径向映射的目标真实边界对应的原生边缺失',
 'circle-driver-has-no-proven-ordered-source-wire-or-target-boundary':'尚未建立圆弧来源的有序线环或目标真实边界',
 'circle-source-relative-wire-direction-not-proven':'尚未证明来源圆弧在当前面线环中的唯一行走方向',
 'circle-source-native-edge-missing':'来源圆弧对应的原生边缺失',
 'circle-source-radius-below-position-resolution':'来源圆弧半径小于当前位置分辨率',
 'recorded-circle-axis-is-not-in-the-plane':'前序记录的圆弧刀轴不在当前平面内',
 'recorded-circle-axis-is-not-native-radial':'前序记录的圆弧刀轴与原生圆的径向不一致',
 'recorded-circle-endpoint-axis-senses-disagree':'来源圆弧首尾记录的径向刀轴朝向不一致',
 'recorded-circle-endpoint-source-use-not-found':'前序记录的圆弧端点未找到对应的来源原生边段',
 'circle-ray-overlaps-a-physical-boundary':'径向切割线与一条真实边界重合，尚未建立唯一到达位置',
 'circle-first-physical-boundary-is-geometrically-ambiguous':'径向切割存在多个竞争的首个边界交点，尚未确定唯一到达边界',
 'circle-no-positive-physical-boundary-in-recorded-radial-direction':'沿已记录刀轴朝向的径向，没有找到正向真实边界',
 'plane-curved-driver-normal-family-pending':'平面曲边的刀轴随位置变化，当前尚未完成这类法线族映射',
 'source-has-no-two-recorded-ready-tool-axes':'前序没有给出两个有效端点刀轴',
 'geometrically-planar-nonplane-parameterization-pending':'该面几何上为平面，但采用其他曲面参数表达，当前尚未完成这类映射',
 'boundary-does-not-preserve-native-edge-pcurve-parameter':'边的三维曲线与面上参数曲线未保持同一原生参数',
 'physical-boundary-has-no-face-pcurve':'真实边界缺少在当前面上的参数曲线',
 'native-source-or-target-range-is-outside-face-pcurve':'来源边或目标边的原生参数区间超出面上参数曲线范围',
 'native-boundary-projection-extrema-not-solved':'边界投影的极值求解未完成',
 'nonconstant-boundary-projection-has-continuous-extrema':'边界投影存在连续极值，当前无法建立单调映射区间',
 'boundary-projection-branch-not-certifiably-monotone':'尚未证明该边界投影分支在区间内单调',
 'source-endpoint-native-edge-missing':'来源路径起点对应的原生边缺失',
 'source-endpoint-has-no-native-parameter-pcurve':'来源路径起点缺少保持原生参数的面上曲线',
 'source-recorded-tool-axis-is-zero':'前序记录的刀轴向量为零',
 'source-has-no-native-uses':'来源路径没有记录组成它的原生边段',
 'source-has-no-directed-line-tangent':'来源路径缺少有效的有向直线切向',
 'source-native-edge-missing':'来源路径对应的原生边缺失',
 'logical-plane-source-not-one-certified-straight-line':'尚未证明该平面逻辑边的所有组成边属于同一条直线',
 'certified-line-source-has-inconsistent-recorded-axis-senses':'同一直线路径的首尾刀轴朝向不一致',
 'plane-axis-chart-is-singular':'平面路径与刀轴无法建立有效的二维映射坐标',
 'recorded-plane-axis-not-perpendicular-to-driver':'前序记录的平面刀轴不垂直于路径行走方向',
 'source-surface-has-no-recorded-straight-generator':'前序没有为该曲面记录直母线刀轴',
 'native-support-has-no-certified-recorded-generator-family':'尚未证明原生曲面存在与已记录刀轴一致的直母线族',
 'source-start-at-generator-singularity':'路径起点处的母线方向退化',
 'recorded-axis-not-native-generator':'前序记录的刀轴与原生曲面的母线方向不一致',
 'source-end-native-edge-missing':'来源路径终点对应的原生边缺失',
 'source-end-has-no-native-parameter-pcurve':'来源路径终点缺少保持原生参数的面上曲线',
 'recorded-generator-endpoint-senses-disagree-or-are-singular':'母线首尾朝向不一致，或端点处的母线方向退化',
 'target-native-event-parameter-not-solved':'目标边分界位置的原生参数未求出',
 'source-native-parameter-at-target-attribute-event-not-solved':'目标边属性分界对应的来源边原生参数未求出',
 'first-positive-physical-boundary-on-current-finite-face':'沿切割方向到达当前有限面内的第一条真实边界',
 'target-native-interval-has-no-complete-edge-analysis-attributes':'前序边分析属性未覆盖目标边的完整原生参数区间',
 'native-target-attribute-event-crosses-or-reverses-the-source-event-cell':'原生属性分界越过当前来源区间或使对应反向，已保留待定，未扩大覆盖',
 'face-has-unordered-physical-boundary-wire':'当前面的真实边界线环尚未完成排序',
 'physical-target-native-edge-missing':'目标真实边界对应的原生边缺失',
 'face-has-no-real-physical-target-boundaries':'当前面未找到可作为目标的真实边界',
 'source-interior-generator-parallel-or-changing-axis-sense-pending':'来源路径内部存在与母线平行或刀轴朝向变化的区间，当前尚未完成这类映射',
 'logical-source-not-continuous-in-generator-chart':'来源逻辑边在母线映射坐标中不连续',
 'logical-source-reverses-non-generator-coordinate':'来源逻辑边在垂直母线的坐标上发生折返',
 'selected-source-has-no-native-range':'已选来源路径没有有效的原生参数区间',
 'periodic-face-has-excessive-unwrapped-boundary-multiplicity':'周期面展开后的边界重数超出当前处理范围',
 'source-native-event-parameter-not-solved':'来源边分界位置的原生参数未求出',
 'physical-target-ray-intersection-not-solved':'切割方向与目标真实边界的交点未求出',
 'first-physical-boundary-is-geometrically-ambiguous':'当前存在多个竞争的首个边界交点，尚未确定唯一到达边界',
 'no-positive-physical-boundary-before-face-generator-end':'沿当前切割方向到达面母线末端前，没有找到正向真实边界',
 'cone-apex-reached-before-a-physical-edge-interval':'沿母线先到达圆锥顶点，尚未到达可覆盖的真实边区间',
 'source-native-interval-not-fully-mapped-to-physical-boundaries':'来源边的原生参数区间尚未全部映射到真实边界',
 'native-conic-projection-has-invalid-original-domain':'原生直线、圆或椭圆的参数域缺失、非有限或没有正长度',
 'native-circle-projection-has-invalid-radius':'原生圆的半径不是有效的有限正数',
 'native-ellipse-projection-has-invalid-radii':'原生椭圆的长短半径无效，无法建立投影',
 'native-conic-projection-coefficients-are-not-finite-or-regular':'原生直线、圆或椭圆的投影系数非有限或发生退化',
 'native-line-projection-has-no-interior-critical-parameters':'原生直线的投影没有内部极值参数',
 'native-conic-projection-critical-parameter-count-or-index-is-not-representable':'原生圆或椭圆的投影极值参数数量或周期编号超出可准确表示的范围',
 'native-conic-projection-critical-parameter-is-not-finite':'原生圆或椭圆的投影极值参数不是有限数',
 'native-conic-projection-distinct-critical-parameters-cannot-be-represented':'两个不同的原生投影极值参数无法用当前数值精度分别表示，未将其合并',
 'native-conic-projection-analytic-alpha-plus-k-pi-events-in-original-domain':'已解析求出原始参数域内圆或椭圆的全部投影极值事件',
 'native-conic-projection-original-curve-query-failed':'读取原生直线、圆或椭圆的投影几何失败',
 'native-conic-projection-bound-is-outside-original-positive-interval':'请求的投影界区间无效，或越出原始正长参数域',
 'native-conic-projection-bound-endpoint-is-not-finite':'投影界区间的端点计算得到非有限坐标',
 'native-conic-projection-outward-bound-is-not-finite':'原生区间投影的保守外包界不是有效的有限区间',
 'native-conic-original-interval-projection-enclosed-by-two-endpoints-and-global-derivative-bound-with-outward-rounding':'依据首尾坐标、全域导数上界及向外舍入，已包住整个原生区间的投影',
 'native-conic-projection-bound-original-curve-query-failed':'计算投影外包界时读取原生曲线失败',
 'native-conic-projection-inverse-coordinate-is-not-finite':'待反求的投影坐标不是有限数',
 'native-line-projection-inverse-is-not-single-valued':'原生直线在该投影方向上退化，无法反求唯一原生参数',
 'native-conic-projection-inverse-has-no-real-native-root':'该投影坐标超出原生圆或椭圆的有限解析实根范围，未夹到端点',
 'native-conic-projection-inverse-root-count-or-index-is-not-representable':'投影反求的候选参数数量或周期编号超出可准确表示的范围',
 'native-conic-projection-inverse-root-is-not-finite':'投影反求得到非有限原生参数',
 'native-conic-projection-inverse-has-no-root-in-original-branch':'当前未改动的原生分支内没有对应投影参数',
 'native-conic-projection-inverse-has-multiple-roots-in-original-branch':'当前原生分支内有多个不同投影参数，尚未建立唯一对应',
 'native-conic-projection-inverse-root-exceeds-floating-point-evaluation-error':'解析反求参数的回代偏差超出浮点计算误差界，未作为有效对应',
 'unique-native-line-or-conic-analytic-projection-root-in-unchanged-original-branch':'已在未改动的原生分支内解析求得唯一投影参数',
 'native-conic-projection-inverse-original-curve-query-failed':'反求投影参数时读取原生曲线失败',
 'kernel-native-projection-has-multiple-roots':'几何内核在当前原生分支内求得多个投影参数',
 'kernel-native-projection-has-no-root':'几何内核在当前原生分支内没有求得对应投影参数',
 'kernel-unique-spline-root-candidates-do-not-enclose-requested-native-coordinate':'原域严格单调已证明，但内核候选的点值外包界均未包含请求的投影坐标，未接受候选参数',
 'kernel-spline-candidate-selected-by-original-strict-monotonicity-and-outward-point-bound':'依据原始控制网的严格单调证明和点值向外包界，已选取回代残差最小的内核候选参数',
 'native-spline-projection-interval-arithmetic-is-not-finite':'原生样条投影的区间运算得到非有限数',
 'native-spline-projection-interval-product-is-not-finite':'原生样条投影的区间乘积不是有限数',
 'native-spline-projection-interval-denominator-is-not-proven-positive':'原生样条投影的区间分母尚未证明为正，未执行除法',
 'native-spline-projection-interval-quotient-is-not-finite':'原生样条投影的区间商不是有限数',
 'native-spline-projection-has-too-few-original-poles':'原生样条控制点不足，无法建立投影外包界',
 'native-spline-projection-requires-finite-original-poles-and-positive-weights':'原生样条投影外包界要求原始控制点有限且权重为正',
 'native-bezier-projection-bound-exceeds-original-domain':'Bezier 投影外包界请求越出原始参数域',
 'native-bspline-projection-periodic-domain-is-not-supported':'当前样条投影外包界尚未支持周期 B 样条原生域',
 'native-bspline-projection-bound-exceeds-original-domain-or-degree':'B 样条投影外包界请求越出原始域，或原始次数无效',
 'native-bspline-projection-original-domain-has-an-unproven-discontinuity':'原始 B 样条域内存在未证明可连续处理的间断',
 'native-bspline-projection-flat-knot-and-pole-counts-disagree':'原始 B 样条的展开节点数量与次数、控制点数量不一致',
 'native-bspline-projection-original-flat-knots-are-invalid':'原始 B 样条的展开节点非有限或没有按序排列',
 'native-bspline-projection-original-flat-knot-domain-is-inconsistent':'原始 B 样条的展开节点域与曲线支持域不一致',
 'native-bspline-projection-original-knot-spans-do-not-cover-request':'原始 B 样条节点区间未完整覆盖请求的投影参数域',
 'native-spline-projection-bound-is-outside-original-positive-interval':'样条投影外包界请求无效，或越出原始正长参数区间',
 'native-bspline-projection-use-exceeds-original-support-domain':'B 样条投影请求越出原始曲线支持域',
 'native-bezier-projection-use-exceeds-original-support-domain':'Bezier 投影请求越出原始曲线支持域',
 'native-spline-projection-bound-support-is-not-bspline-or-bezier':'当前投影外包界仅支持原生 B 样条或 Bezier 曲线',
 'native-spline-projection-original-interval-bound-is-not-finite':'原生样条全区间投影外包界不是有效的有限区间',
 'native-spline-original-interval-projection-enclosed-by-outward-homogeneous-de-boor-or-de-casteljau-on-original-positive-knot-spans':'在原始正长节点区间内，通过齐次 de Boor／de Casteljau 运算和向外舍入，已包住整个原生区间的投影',
 'native-spline-projection-original-curve-query-failed':'计算全区间投影外包界时读取原生样条失败',
 'native-spline-projection-point-is-outside-unchanged-original-domain':'样条点值投影请求无效，或参数越出未改动的原始域',
 'native-bspline-projection-point-use-exceeds-original-support-domain':'B 样条点值投影参数越出原始曲线支持域',
 'native-bezier-projection-point-use-exceeds-original-support-domain':'Bezier 点值投影参数越出原始曲线支持域',
 'native-spline-projection-point-support-is-not-bspline-or-bezier':'当前点值投影外包界仅支持原生 B 样条或 Bezier 曲线',
 'native-spline-projection-point-enclosure-is-not-finite':'原生样条点值投影的外包界不是有效的有限区间',
 'unchanged-original-native-parameter-projection-enclosed-by-outward-homogeneous-spline-point-evaluation':'在未改动的原生参数处，通过齐次样条点值运算和向外舍入，已包住该点的真实投影',
 'native-spline-projection-point-original-curve-query-failed':'计算点值投影外包界时读取原生样条失败',
 'native-polynomial-spline-projection-has-too-few-original-poles':'原始样条控制点不足，无法证明投影严格单调',
 'native-polynomial-spline-projection-requires-equal-positive-original-weights':'该严格单调证明要求原始样条权重完全相等且为正',
 'native-polynomial-spline-projection-requires-finite-poles-and-exactly-equal-original-weights':'该严格单调证明要求原始控制点有限，且所有原始权重精确相等',
 'native-polynomial-spline-projection-original-adjacent-S-poles-reverse-direction':'原始相邻 S 控制点发生反向，未证明投影严格单调',
 'native-polynomial-spline-projection-has-invalid-original-domain':'严格单调证明所用的原始样条参数域缺失、非有限或没有正长度',
 'native-polynomial-bspline-projection-requires-nonperiodic-original-support-domain':'该严格单调证明要求请求位于原始非周期 B 样条支持域内',
 'native-polynomial-bspline-projection-original-degree-is-invalid':'原始 B 样条次数无效，未证明投影严格单调',
 'native-polynomial-bspline-projection-original-support-has-a-discontinuity':'原始 B 样条支持域存在间断，未证明投影严格单调',
 'native-polynomial-bspline-projection-original-flat-knot-count-is-invalid':'原始展开节点数量与 B 样条次数、控制点数量不一致，未证明严格单调',
 'native-polynomial-bspline-projection-original-flat-knots-are-invalid':'原始展开节点非有限或没有按序排列，未证明严格单调',
 'native-polynomial-bspline-projection-original-flat-knot-domain-is-inconsistent':'原始展开节点域与 B 样条支持域不一致，未证明严格单调',
 'native-polynomial-bspline-projection-original-active-knot-span-is-a-real-platform':'实际原生节点区间存在正长投影平台，不能反求唯一参数',
 'native-polynomial-bspline-projection-original-domain-has-no-positive-active-span':'请求的原始域内没有实际正长节点区间',
 'native-polynomial-bezier-projection-use-exceeds-original-support-domain':'严格单调证明的 Bezier 请求越出原始支持域',
 'native-polynomial-spline-projection-support-is-not-bspline-or-bezier':'当前严格单调证明仅支持原生 B 样条或 Bezier 曲线',
 'native-polynomial-spline-projection-has-no-strict-original-pole-direction':'原始 S 控制点未能证明一致的严格变化方向',
 'strict-original-polynomial-spline-projection-by-exactly-equal-positive-weights-and-same-way-original-S-poles-with-no-active-native-platform':'原始权重完全相等且为正，S 控制点同向且各实际原生节点区间没有平台，已证明原域投影严格单调',
 'native-positive-rational-spline-projection-requires-positive-original-weights':'该有理样条严格单调证明要求所有原始权重为正',
 'native-positive-rational-spline-projection-requires-finite-poles-and-positive-original-weights':'该有理样条严格单调证明要求原始控制点有限且所有原始权重为正',
 'strict-original-positive-rational-spline-projection-by-TP2-positive-original-weights-and-same-way-original-S-poles-with-no-active-native-platform':'原始权重均为正，S 控制点同向且各实际原生节点区间没有平台，已依据有理基函数的保序性质证明原域投影严格单调',
 'native-polynomial-spline-projection-original-curve-query-failed':'检查原始控制网的严格单调性时读取曲线失败',
 'target-native-projection-uncertainty-is-local':'目标原生曲线的投影不确定性已限定到记录的局部带，仅相应来源区间保留待定',
 'positive-source-projection-cell-below-native-event-resolution':'来源投影分区仍有正长度，但小于当前原生事件分辨率，保留待定',
 'original-native-pcurve':'原始面上参数曲线坐标',
 'affine-source-native-face-is-invalid':'原生加工面的有效性检查未通过，未执行直线或母线首达映射',
 'normal-strip-native-trimmed-face-is-invalid':'原生裁剪面的有效性检查未通过，未执行曲边法线条带映射',
 'skin-exit-boundary-set-native-face-is-invalid':'原生加工面的有效性检查未通过，未使用完整皮边集合证明切透',
 'skin-exit-boundary-set-requires-native-plane-and-real-skin-normal-family-source':'当前面或刀轴类型不满足平面皮边出口证明的条件',
 'skin-exit-boundary-set-physical-boundary-is-not-wholly-OnSkin':'真实边界中存在未全部在皮上的区间，不能使用完整皮边集合证明',
 'skin-exit-boundary-set-physical-boundary-source-skin-membership-not-proven':'该边的皮关系枚举已是“在皮上”，但尚缺原始皮面归属证书，不能用于完整出口集合证明',
 'skin-exit-boundary-set-physical-boundary-attribute-exceeds-native-range':'边属性区间越出了原生边界范围，未用于出口证明',
 'normal-strip-requires-one-ordered-wire-without-holes':'曲边直接映射暂只处理一个有序边界线环的条带面',
 'normal-strip-physical-wire-is-not-exactly-four-original-edges':'当前边界不是由四条完整原生边组成的条带',
 'normal-strip-requires-exactly-two-skin-chains-and-two-open-sides':'当前边界未形成两条皮边与两条开放侧边的对应关系',
 'normal-strip-normal-coincides-with-a-physical-boundary':'该处面内法线与真实边界重合，尚未证明正长映射区间',
 'normal-strip-first-boundary-tie-on-certified-component':'该区间存在竞争的首达边界，尚未证明唯一对应',
 'normal-strip-source-has-no-two-recorded-ready-axes':'原生曲边直接映射缺少两个有效的原始端点刀轴',
 'shared-native-vertex-event-exceeds-original-declared-cad-tolerances':'同一拓扑顶点的曲线端点偏差超过原始面、边、顶点共同声明的公差，未统一事件',
 'shared-native-vertex-event-would-collapse-or-reverse-positive-projection-branch':'统一共享顶点事件会压掉或反转真实正长分支，已拒绝该处理',
 'shared-native-vertex-event-requires-a-valid-original-face':'原始面的有效性检查未通过，未统一共享顶点事件',
 'all-source-intervals-reach-the-first-current-face-physical-boundary':'来源路径的全部区间均已映射到当前面的第一条真实边界',
 'classification-complete-by-exact-final-native-source-coverage-union':'最终完整几何证据已精确覆盖全部原生来源区间，判定完成；局部求解诊断仍保留',
 'classification-incomplete-by-exact-final-native-source-coverage-union':'最终几何证据尚未完整覆盖全部原生来源区间，判定未完成',
 'exit-correspondence-expanded-by-exact-final-native-source-arrival-union':'真实到达记录已精确覆盖全部原生来源区间，出口对应已展开',
 'exit-correspondence-not-expanded-by-exact-final-native-source-arrival-union':'真实到达记录尚未完整覆盖全部原生来源区间，出口对应未全部展开',
 'final-source-state-from-exact-native-unions':'来源路径的最终状态按原生区间的精确证据并集确定',
 'mapper-diagnostics-retained':'局部映射求解的原始诊断仍保留',
 'current-face-or-edge-analysis-boundary-missing':'当前加工面或前序边分析边界记录缺失',
 'some-face-source-destinations-remain-explicitly-pending':'部分皮边区间尚未完成边界映射，仅这些区间保留待定'
};
const COVERAGE_DETAIL_FIELDS={
 'source-edge':'来源原边', 'target-edge':'目标原边', 'edge':'原边',
 'original-native':'原始原生参数域', 'inverse-native':'细化后的原生参数', 'point-projection':'该点的投影外包界',
 'requested-s':'请求投影坐标', 'iterations':'细化迭代次数',
 'source-s':'来源投影区间', 'native':'原生参数区间', 'original-s':'原生曲线端点投影',
 'projection-s':'当前端点投影', 'target-native':'目标原生参数区间', 'projection-band':'目标投影不确定性带',
 'target-branch-native':'目标分支原生参数', 'target-branch-s':'目标分支投影区间',
 'target-root-native':'求得的目标原生参数', 'target-root-s':'目标参数回代投影',
 'source-cell':'来源原生参数分区', 'source-mapped':'映射得到的来源原生参数',
 'chart-cell':'当前来源投影分区', 'attribute-s':'目标属性分界投影',
 'conic-CAB':'投影表达系数 [C,A,B]', 'coordinates':'坐标依据', 'projection-bound-proof':'投影外包界证明',
 'canonical-vertex-events':'已统一的共享顶点事件数',
 'classification-restored':'按最终证据确认判定完成的来源数',
 'classification-downgraded':'按最终证据恢复待定的来源数',
 'correspondence-restored':'按真实到达并集确认出口对应完整的来源数',
 'correspondence-downgraded':'按真实到达并集恢复出口对应未完整的来源数'
};
function coverageExplanation(value){
 if(typeof value!=='string')return value;
 if(COVERAGE_EXPLANATIONS[value])return COVERAGE_EXPLANATIONS[value];
 if(value.includes(';'))return value.split(';').map(coverageExplanation).join('；');
 if(value.startsWith('native-positive-rational-')){
  const shared='native-polynomial-'+value.slice('native-positive-rational-'.length);
  if(COVERAGE_EXPLANATIONS[shared])return COVERAGE_EXPLANATIONS[shared];
 }
 const field=/^([^=]+)=(.*)$/.exec(value);
 if(field){
  const [,key,detail]=field;
  if(key==='first-reason'||key==='last-reason')return (key==='first-reason'?'起点求解原因：':'终点求解原因：')+coverageExplanation(detail);
  if(key==='first-solved'||key==='last-solved')return (key==='first-solved'?'起点参数求解：':'终点参数求解：')+(detail==='1'?'已求得':detail==='0'?'未求得':detail);
  if(key==='native-line')return '目标为原生直线：'+(detail==='1'?'是':detail==='0'?'否':detail);
  if(key==='guard')return '越界检查坐标：'+(detail==='native'?'原生参数':detail==='chart'?'投影坐标':detail);
  if(COVERAGE_DETAIL_FIELDS[key])return COVERAGE_DETAIL_FIELDS[key]+'：'+coverageExplanation(detail);
 }
 const edgeReason=/^([^:]+):E(\d+)$/.exec(value);
 if(edgeReason&&COVERAGE_EXPLANATIONS[edgeReason[1]])return COVERAGE_EXPLANATIONS[edgeReason[1]]+`（原边 E${edgeReason[2]}）`;
 if(value.startsWith('native-face-boundary-mapping-failed: '))return '原生面边界映射求解失败：'+value.slice('native-face-boundary-mapping-failed: '.length);
 if(value.startsWith('unavailable-endpoint-axes='))return '仍待处理的原始端点刀轴数：'+value.slice('unavailable-endpoint-axes='.length);
 if(value.startsWith('direct-map-status='))return '逐点出口对应状态：'+coverageExplanation(value.slice('direct-map-status='.length));
 if(value.startsWith('boundary-set-status='))return '完整皮边出口证明：'+coverageExplanation(value.slice('boundary-set-status='.length));
 if(value.startsWith('final-source-state-missing-selected-source-record:'))return '最终来源状态缺少已选皮边路径记录：'+value.slice('final-source-state-missing-selected-source-record:'.length);
 if(value.startsWith('shared-TopoVertex-event['))return '同一原生拓扑顶点的事件证据（原边与参数、偏差与公差）：'+value.slice('shared-TopoVertex-event'.length);
 if(value.startsWith('optional-shared-TopoVertex-event-kept-native['))return '可选共享顶点统一超出原声明预算；保留原生参数继续映射（偏差与预算）：'+value.slice('optional-shared-TopoVertex-event-kept-native'.length);
 return value;
}
const esc=value=>String(value??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const id=value=>String(value??'');
const validPoint=p=>Array.isArray(p)&&p.length===3&&p.every(Number.isFinite);
const number=value=>Number.isFinite(value)?value!==0&&Math.abs(value)<1e-6?value.toExponential(5):Number(value.toPrecision(9)).toLocaleString('zh-CN',{maximumFractionDigits:9}):'未记录';
const list=value=>Array.isArray(value)?value.map(id):[];
const present=value=>value!==undefined&&value!==null&&value!=='';
const rows=pairs=>`<dl>${pairs.filter(([,v])=>present(v)).map(([k,v])=>`<dt>${esc(k)}</dt><dd>${esc(v)}</dd>`).join('')}</dl>`;
const raw=value=>`<pre class="evidence">${esc(JSON.stringify(value,null,2))}</pre>`;
const links=artifacts=>(artifacts??[]).filter(a=>typeof a.url==='string'&&(/^(?:\/|\.\/|https?:\/\/)/.test(a.url))).map(a=>`<a href="${esc(a.url)}" download>${esc(a.label??'产出文件')}</a>`).join('');

function matrix(value){
 const result=Array.isArray(value?.[0])?value.flat():value;
 return Array.isArray(result)&&result.length===16&&result.every(Number.isFinite)?result:null;
}
function transformPoint(point,transform){
 if(!validPoint(point)||!transform)return Array.isArray(point)?[...point]:point;
 const [x,y,z]=point,m=transform,w=m[12]*x+m[13]*y+m[14]*z+m[15];
 if(!Number.isFinite(w)||Math.abs(w)<1e-15)throw Error('对齐展示矩阵不能变换该预览点');
 return [(m[0]*x+m[1]*y+m[2]*z+m[3])/w,(m[4]*x+m[5]*y+m[6]*z+m[7])/w,(m[8]*x+m[9]*y+m[10]*z+m[11])/w];
}
function meshFaces(source,transform){
 return (source?.geometry?.faces??[]).map(face=>{
  const result={...face,faceId:id(face.faceId),indices:face.indices?[...face.indices]:[]};
  const positions=face.positions??[];
  if(!transform)result.positions=[...positions];
  else{result.positions=[];for(let i=0;i<positions.length;i+=3)result.positions.push(...transformPoint(positions.slice(i,i+3),transform));}
  return result;
 });
}
function extent(faces){
 const lo=[Infinity,Infinity,Infinity],hi=[-Infinity,-Infinity,-Infinity];
 for(const face of faces)for(let i=0;i<(face.positions?.length??0);i+=3)for(let j=0;j<3;j++){const value=face.positions[i+j];if(Number.isFinite(value)){lo[j]=Math.min(lo[j],value);hi[j]=Math.max(hi[j],value);}}
 const diagonal=Math.hypot(...hi.map((value,index)=>value-lo[index]));
 return Number.isFinite(diagonal)&&diagonal>0?diagonal:1;
}
function referenceEdge(key,ownerIds,points,record={},extra={}){
 const faceIds=list(ownerIds);
 return {pathKey:key,displaySpanKey:key,faceId:faceIds[0]??'',faceIds,curveKind:record.curveKind??record.curveType??'边',groupId:record.groupId??record.edgeId??key,partIndex:1,
  previewSegments:[{points:points.filter(validPoint).map(p=>[...p]),previewOnly:true}],path:false,side:false,reference:true,
  sourceEdgeIds:record.edgeId!==undefined?[id(record.edgeId)]:[],sourceUses:[],endpoints:[],analysisRecord:record,workPattern:'solid',workColor:'#849ab2',...extra};
}
function ordinaryEdges(source,transform,workId){
 return (source?.edges??[]).map((edge,index)=>referenceEdge(`${workId}:E${edge.edgeId??index+1}`,edge.faceIds??[],(edge.points??[]).map(point=>transformPoint(point,transform)),edge,{label:`原边 E${edge.edgeId??index+1}`}));
}
function classifiedEdges(source){
 return (source?.edges??[]).map((edge,index)=>{
  const key=edge.pathKey??`classify:E${index+1}`,faceIds=edge.faceIds??[edge.faceId];
  return {...referenceEdge(key,faceIds,[],{edgeId:edge.sourceEdgeIds?.[0],curveKind:edge.curveKind},{label:`原边 ${(edge.sourceEdgeIds??[]).map(x=>'E'+x).join('、')||index+1}`}),
   displaySpanKey:edge.displaySpanKey??key,sourceEdgeIds:list(edge.sourceEdgeIds),previewSegments:(edge.previewSegments??[]).map(s=>({...s,points:(s.points??[]).map(p=>[...p])}))};
 });
}
function contourEdges(source){
 const edges=[],x=source?.endX;
 if(!Number.isFinite(x))return edges;
 (source.wires??[]).forEach((wire,wi)=>(wire.edges??[]).forEach((edge,ei)=>{
  const key=`contour:W${wi+1}:E${ei+1}`,record={...edge,wireId:wi+1,wireInner:wire.inner===true,wireClosed:wire.closed===true,wireArea:wire.area,wireValid:wire.valid,edgeId:edge.sourceEdgeId};
  const points=(edge.previewSamples??[]).filter(p=>Array.isArray(p)&&p.length===2&&p.every(Number.isFinite)).map(p=>[x,p[0],p[1]]);
  edges.push(referenceEdge(key,edge.sourceFaceId&&id(edge.sourceFaceId)!=='0'?[edge.sourceFaceId]:[],points,record,{label:`${wire.inner?'内':'外'}轮廓 W${wi+1} · E${edge.sourceEdgeId??ei+1}`,workColor:wire.inner?'#ff303b':'#1fe05c'}));
 }));
 return edges;
}
function analysisEdges(source){
 return (source?.edges??[]).map((edge,index)=>{
  const key=edge.edgeKey??`edgeAnalysis:E${edge.edgeId??index+1}`;
  return referenceEdge(key,edge.faceIds??edge.sourceFaceIds??[],edge.samples??[],edge,{label:`${key} · ${SKIN[edge.skinRelation]??'皮关系未记录'} · ${CONVEXITY[edge.convexity]??'凹凸未记录'}`,
   skinRelation:edge.skinRelation,skinValid:edge.skinValid,convexity:edge.convexity,convexityValid:edge.convexityValid,
   workColor:edge.skinValid===false?'#a1aab8':SKIN_COLORS[edge.skinRelation]??'#a1aab8',workPattern:edge.convexityValid===false?'solid':PATTERNS[edge.convexity]??'solid'});
 });
}
function coverageEdges(source){
 const references=(source?.referenceEdges??source?.edges??[]).map((edge,index)=>{
  const key=`faceCutCoverage:reference:${edge.edgeKey??edge.edgeId??index+1}:${index+1}`;
  return {...referenceEdge(key,edge.machiningFaceIds??edge.faceIds??[edge.faceId],edge.points??edge.samples??[],edge,{label:`原边 E${edge.edgeId??index+1} · 灰色参考${edge.unresolvedBoundaries?.length?' · 边界关系未完成':''}`,workReference:true}),
   ...(Array.isArray(edge.previewSegments)?{previewSegments:edge.previewSegments}:{}),sourceEdgeIds:list(edge.sourceEdgeIds??[edge.edgeId])};
 });
 const spans=(source?.coverage??[]).map((span,index)=>{
  const sourceEdgeId=span.sourceEdgeId??span.targetEdgeId;
  const key=span.key??`faceCutCoverage:F${span.faceId}:E${sourceEdgeId}:C${index+1}`;
  return {...referenceEdge(key,[span.faceId],[],span,{label:`皮边 E${sourceEdgeId} · ${COVERAGE[span.kind]??span.kind}`,workCoverage:true,workColor:COVERAGE_COLORS[span.kind]??'#94a3b8'}),
   sourceEdgeIds:list([sourceEdgeId]),previewSegments:span.previewSegments??[]};
 });
 return [...references,...spans];
}
function penetrationEdges(source){
 return (source?.spans??[]).map((span,index)=>{
  const key=`penetration:${span.pathKey}:${span.edgeId}:${index+1}`;
  const kind=PENETRATION[span.kind];
  return {...referenceEdge(key,[span.faceId],[],span,{
    label:`E${span.edgeId} · ${COVERAGE[kind]??span.kind}`,workCoverage:true,
    workColor:COVERAGE_COLORS[kind],workPattern:span.kind==='pending'?'dashed':'solid'}),
    previewSegments:span.previewSegments??[]};
 });
}
function normalizeFaces(workId,source,meta,geometry){
 const sourceRecords=Array.isArray(source?.faces)?source.faces:[],metaRecords=Array.isArray(meta?.faces)?meta.faces:[];
 const existing=new Map(sourceRecords.map(f=>[id(f.faceId),f]));
 const meshMetadata=new Map(geometry.map(({positions,indices,...face})=>[id(face.faceId),face]));
 const metaFaces=new Map(metaRecords.filter(f=>f.faceId!==undefined).map(f=>[id(f.faceId),f]));
 const ids=new Set([...existing.keys(),...metaFaces.keys(),...geometry.map(f=>id(f.faceId))]);
 const machining=new Set(list(meta?.machiningFaceIds));
 if(!machining.size)for(const face of sourceRecords)if(face.machining)machining.add(id(face.faceId));
 const history=new Map();
 if(workId==='heal')for(const record of metaRecords)for(const faceId of record.resultFaceIds??[])history.set(id(faceId),record);
 let machiningIndex=0;
 return [...ids].map((faceId,index)=>{
  const previous=existing.get(faceId)??metaFaces.get(faceId)??meshMetadata.get(faceId)??{},isMachining=['classify','edgeAnalysis','faceCutCoverage','penetration'].includes(workId)&&machining.has(faceId),record=history.get(faceId);
  const workColor=workId==='classify'?(isMachining?'#ffad57':'#64758a'):workId==='heal'&&record?(record.status==='converted'?'#52c8a2':record.status==='rejected'?'#f1ab60':'#72869c'):'#72869c';
  return {...previous,faceId,faceIndex:index+1,machining:isMachining,machiningIndex:isMachining?++machiningIndex:undefined,pathKeys:[],unresolvedBoundaryKeys:[],workColor,inspectionRecord:record??metaFaces.get(faceId)??meshMetadata.get(faceId)??null};
 });
}

export async function loadWorkData(workId,entry,fetchJson){
 if(!WORK_LABELS[workId])throw Error(`未支持的 Work：${workId}`);
 const requests=new Map();
 const request=async url=>{if(!url)return null;if(!requests.has(url))requests.set(url,fetchJson(url));return requests.get(url);};
 const diagnostics=[];
 const fetchSaved=async(url,label)=>{try{return await request(url);}catch(error){diagnostics.push(`${label}：${error.message}`);return null;}};
 const [savedMeta,source]=await Promise.all([fetchSaved(entry?.dataUrl,'阶段数据读取失败'),fetchSaved(entry?.geometryUrl,'预览几何读取失败')]);
 const meta=savedMeta?.report&&typeof savedMeta.report==='object'&&!Array.isArray(savedMeta.report)
  ?{...savedMeta.report,...savedMeta,diagnostics:[...new Set([...(savedMeta.report.diagnostics??[]),...(savedMeta.diagnostics??[])])]}:savedMeta;
 const inspection={workId,entry:entry??{},label:WORK_LABELS[workId],rawData:meta,coordinateNote:entry?.coordinateNote??'',identityGroup:entry?.identityGroup??`${workId}:unknown`};
 if(workId==='pathAnalysis'){
  const data=meta??source;
  if(data?.stage==='pathAnalysis'&&Array.isArray(data.edges)&&Array.isArray(data.faces))return {...data,inspectionStage:workId,inspection,diagnostics:[...(data.diagnostics??[]),...diagnostics]};
 }
 if(workId==='faceCutCoverage'&&(meta??source)&&((meta??source).stage!=='faceCutCoverage'||!Array.isArray((meta??source).coverage)||!Array.isArray((meta??source).sources)||!Array.isArray((meta??source).arrivals)||(meta??source).schema!==3||(meta??source).coverageDomain!=='source-skin-native-intervals'||!Array.isArray((meta??source).evidence)))throw Error('逐面覆盖结果格式不完整；需要本 Work 的 schema 3、sources、arrivals、evidence 和 coverage');
 if(workId==='penetration'&&meta&&(meta.stage!=='penetration'||meta.schema!==1||!Array.isArray(meta.spans)))
  throw Error('切透判定数据格式不完整');
 // Never borrow geometry from another stage when this Work has no output.
 const transform=matrix(entry?.transform4x4);
 const geometry=meshFaces(source,transform);
 const faces=normalizeFaces(workId,source,meta,geometry);
 const edges=workId==='penetration'?penetrationEdges(meta):workId==='faceCutCoverage'?coverageEdges(meta??source):workId==='edgeAnalysis'?analysisEdges(meta):workId==='contour'?contourEdges(meta):workId==='classify'?classifiedEdges(source):ordinaryEdges(source,transform,workId);
 const geometryIssues=source?.geometry?.issues??[];
 const passed=['passed','partial'].includes(entry?.status),complete=entry?.status==='passed'&&diagnostics.length===0
  &&meta?.complete!==false&&(workId==='penetration'||source?.complete!==false)
  &&source?.success!==false&&geometryIssues.length===0&&(!entry?.geometryUrl||!!source);
 const hasOutput=!!meta||!!source;
 const inheritedDiagnostics=Array.isArray(meta?.diagnostics)?meta.diagnostics:[];
 return {schema:1,stage:'workInspection',inspectionStage:workId,inspection,success:passed&&hasOutput&&meta?.success!==false,complete,
  name:meta?.name??entry?.name??'',faces,edges,geometry:{faces:geometry,previewOnly:true,completeModel:true,issues:geometryIssues},
  counts:{...(['faceCutCoverage','penetration'].includes(workId)?meta?.counts:{}),wholeFaces:faces.length,machiningFaces:faces.filter(f=>f.machining).length,edgeOccurrences:edges.length,logicalEdgeParts:edges.length,pathOccurrences:0,sideOccurrences:0,ready:0,pendingParallel:0,unsupported:0,invalid:0,unresolvedBoundaries:0},
  display:{axisLengthMm:extent(geometry)*.012},unresolvedBoundaries:[],diagnostics:[...(entry?.reason?[entry.reason]:[]),...inheritedDiagnostics,...diagnostics,
   ...geometryIssues.map(issue=>typeof issue==='string'?issue:`预览几何：${JSON.stringify(issue)}`)]};
}

function provenanceHtml(data){
 const entry=data.inspection?.entry??{},provenance=entry.provenance??{},artifacts=entry.artifacts??[];
 return `<h3>本 Work 的产出</h3><div id="workArtifactLinks">${links(artifacts)||'<span class="muted">本阶段没有可下载的产出。</span>'}</div><details><summary>输入与产出来源</summary>${rows([
  ['运行批次',provenance.run],['输入来源',provenance.source],['输入 SHA-256',provenance.sourceSha256],['坐标展示',entry.coordinateNote],['面号范围',entry.identityGroup]
 ])}${artifacts.map(a=>`<div class="evidence">${esc(a.label??'产出')}<br>${esc(a.sourcePath??'')}<br>${esc(a.sha256??'')}</div>`).join('')}${provenance.producer?raw(provenance.producer):''}</details>`;
}
const nativeInterval=(first,last)=>`${Number.isFinite(first)?String(first):'未记录'} ～ ${Number.isFinite(last)?String(last):'未记录'}`;
function faceSources(data,faceId){return (data.inspection?.rawData?.sources??[]).filter(source=>id(source.faceId)===id(faceId));}
function sourceEvidence(sources,{title='本面来源路径与映射'}={}){
 return `<h3>${esc(title)}（${sources.length}）</h3>${sources.map(source=>`<details class="coverage-source"${source.complete===false?' open':''}><summary>${esc(source.sourcePathKey)} · ${source.complete?'判定完成':'判定未完成'}</summary>${rows([
  ['所属原面',source.faceId],['来源原边',list(source.sourceEdgeIds).map(v=>'E'+v).join('、')],['判定／映射方法',coverageExplanation(source.method)],['具体出口参数对应',source.exitCorrespondenceExpanded===true?'已展开':source.exitCorrespondenceExpanded===false?'尚未展开':'未单独记录'],['映射说明／失败原因',coverageExplanation(source.reason)]
 ])}${(source.sourceUses??[]).map(use=>`<div class="evidence">皮边 E${esc(use.edgeId)} · ${esc(use.spanKey)}<br>原生参数 ${esc(nativeInterval(use.firstParameter,use.lastParameter))} · ${use.reversed?'反向':'正向'}</div>`).join('')}<details><summary>完整来源记录</summary>${raw(source)}</details></details>`).join('')||'<p class="muted">此面没有皮边走刀路径。</p>'}`;
}
function sideCorrespondenceEvidence(data,edge,owner){
 const meta=data.inspection?.rawData??{},span=edge.analysisRecord??{},targetId=span.sourceEdgeId??span.targetEdgeId??span.edgeId;
 const overlaps=(first,last)=>Math.min(Math.max(first,last),span.lastParameter)>Math.max(Math.min(first,last),span.firstParameter);
 // These are saved baseline and side-index facts. Filtering them for the
 // current face/edge interval never creates a correspondence or a new pass.
 const baseline=new Set(edge.workCoverage?span.baselineSourcePathKeys??[]:(meta.coverage??[]).filter(record=>id(record.faceId)===id(owner)&&id(record.targetEdgeId)===id(targetId)&&overlaps(record.firstParameter,record.lastParameter)).flatMap(record=>record.baselineSourcePathKeys??[]));
 const correspondences=(meta.sideCorrespondences??[]).map(record=>({record,arrival:meta.arrivals?.[record.arrivalIndex]})).filter(({arrival})=>arrival&&id(arrival.faceId)===id(owner)&&baseline.has(arrival.sourcePathKey)&&id(arrival.sourceEdgeId)===id(targetId)&&overlaps(arrival.sourceFirstParameter,arrival.sourceLastParameter));
 return `<h3>这条路径已对应的侧边（${correspondences.length}）</h3><p class="work-detail-note">仅展示本 Work 已保存的侧边对应记录；这些出向记录不增加来源路径或覆盖次数。</p>${correspondences.map(({record,arrival})=>`<div class="evidence"><b>${esc(record.sidePathKey)}</b>${rows([['所属原面 faceId',arrival.faceId],['来源路径',arrival.sourcePathKey],['原生到达索引',`arrival[${record.arrivalIndex}]`],['来源原边',`E${arrival.sourceEdgeId}`],['来源原生参数',nativeInterval(arrival.sourceFirstParameter,arrival.sourceLastParameter)],['目标原边',`E${arrival.targetEdgeId}`],['目标原生参数',nativeInterval(arrival.targetFirstParameter,arrival.targetLastParameter)],['侧边逻辑标识',record.sidePathKey],['侧边分析边段',record.sideSpanKey],['映射方法',coverageExplanation(arrival.method)],['映射说明',coverageExplanation(arrival.reason)]])}<details><summary>原始侧边对应记录</summary>${raw({sideCorrespondence:record,arrival})}</details></div>`).join('')||'<p class="muted">当前原生区间没有已保存的出向侧边对应记录。</p>'}`;
}
function coverageEvidenceHtml(record,index){
 if(!record)return `<p class="bad">evidence[${esc(index)}] 缺失；保留原始索引。</p>`;
 const common=[['所属原面 faceId',record.faceId],['来源皮边逻辑标识',record.sourcePathKey],['来源皮边',`E${record.sourceEdgeId}`],['来源原生参数',nativeInterval(record.sourceFirstParameter,record.sourceLastParameter)]];
 if(record.kind==='SkinExitBoundarySet'){
  const witnesses=record.boundaryWitnesses??[];
  return `<div class="evidence"><b>evidence[${esc(index)}] · ${esc(EVIDENCE[record.kind])}</b>${rows([...common,['切透分类已证明',record.complete&&record.through?'是':'否'],['具体出口参数对应',record.exitCorrespondenceExpanded?'已展开':'尚未展开'],['证明说明',coverageExplanation(record.reason)]])}<p class="work-detail-note">有效有界平面域的全部真实边界都已证明属于皮。内向切割线必从其中某条皮边退出，因此可以证明切透；这里没有伪造对面点或到达记录。原始端点刀轴状态保留在 PathAnalysis。</p><details><summary>全部可能出口的原始皮边区间（${witnesses.length}）</summary>${witnesses.map(witness=>`<div class="evidence"><b>E${esc(witness.edgeId)} · ${esc(witness.spanKey)}</b>${rows([['原生参数',nativeInterval(witness.firstParameter,witness.lastParameter)],['来源皮面',list(witness.sourceSkinFaceIds).join('、')||'未记录'],['皮归属依据',witness.skinReason]])}</div>`).join('')}</details><details><summary>原始判定证据</summary>${raw(record)}</details></div>`;
 }
 return `<div class="evidence"><b>evidence[${esc(index)}] · ${esc(EVIDENCE[record.kind]??record.kind)}</b>${rows([...common,['对应原边',`E${record.witnessEdgeId}`],['对应逻辑边',record.witnessLogicEdgeId],['对应分析边段',record.witnessSpanKey],['对应原生参数',nativeInterval(record.witnessFirstParameter,record.witnessLastParameter)],['首达记录',`arrival[${record.arrivalIndex}]`],['证明切透',record.through?'是':'否'],['证据完整',record.complete?'是':'否'],['说明',coverageExplanation(record.reason)]])}<details><summary>原始判定证据</summary>${raw(record)}</details></div>`;
}
function coveragePointMarkerEvidence(data,span){
 const markers=(data.inspection?.rawData?.display?.stepPointMarkers??[]).filter(marker=>marker.displayOnly===true
  &&marker.coverageKey===span.key&&id(marker.faceId)===id(span.faceId)
  &&marker.sourcePathKey===span.sourcePathKey&&id(marker.sourceEdgeId)===id(span.sourceEdgeId)
  &&marker.firstParameter===span.firstParameter&&marker.lastParameter===span.lastParameter);
 if(!markers.length)return '';
 return `<h3>STEP 点标记（仅展示）</h3><p class="work-detail-note">这个原生区间仍严格正长。STEP 用原曲线起点处的点标记展示；原生参数、覆盖判断和浏览器预览均保留，点标记不新增刀路。</p>${markers.map(marker=>`<div class="evidence">${rows([
  ['所属覆盖区间',marker.coverageKey],['原面／原边',`F${marker.faceId}／E${marker.sourceEdgeId}`],
  ['原生参数区间',nativeInterval(marker.firstParameter,marker.lastParameter)],['原曲线取点参数',String(marker.firstParameter)],
  ['点位置',(marker.position??[]).map(String).join('，')],['内核造边错误号',marker.builderError],
  ['内核原生参数分辨界',String(marker.nativeParameterResolution)],['展示说明',coverageExplanation(marker.reason)]
 ])}<details><summary>原始 STEP 展示标记</summary>${raw(marker)}</details></div>`).join('')}`;
}
function coverageEdgeDetails(data,edge,selectedFace=''){
 const owner=(edge.faceIds??[]).includes(id(selectedFace))?id(selectedFace):edge.faceId;
 const span=edge.analysisRecord??{},meta=data.inspection?.rawData??{},sources=faceSources(data,owner);
 const sourceEdgeId=span.sourceEdgeId??span.targetEdgeId??span.edgeId;
 const base=[['原面编号 faceId',owner],['全部所属原面',list(edge.faceIds).join('、')],['原生来源边',`E${sourceEdgeId}`],['记录标识',edge.pathKey],['原生参数区间',nativeInterval(span.firstParameter,span.lastParameter)]];
 if(!edge.workCoverage)return `<h2>${esc(edge.label)}</h2>${rows(base)}<p class="notice">原加工边参考保留；灰线本身不代表切透或半切透。</p>${(span.unresolvedBoundaries??[]).map(boundary=>`<div class="notice warning">原面 ${esc(boundary.faceId)} · W${esc(boundary.wireId)}<br>${esc(boundary.reason)}</div>`).join('')}${sideCorrespondenceEvidence(data,edge,owner)}${sourceEvidence(sources)}<details><summary>原加工边记录</summary>${raw(span)}</details>`;
 base.push(['覆盖判断',COVERAGE[span.kind]??span.kind],['来源皮边逻辑标识',span.sourcePathKey],['本区间判定完成',(span.classificationProofComplete??span.mappingComplete)?'是':'否'],['具体出口参数对应',span.exitCorrespondenceExpanded?'已展开':'尚未展开'],['本区间判定说明',coverageExplanation(span.reason)]);
 const evidence=(span.evidenceIndices??[]).map(index=>({index,record:meta.evidence?.[index]}));
 const arrivals=(span.arrivalIndices??[]).map(index=>({index,arrival:meta.arrivals?.[index]}));
 const siblings=data.edges.filter(other=>other.workCoverage&&other.pathKey!==edge.pathKey&&id(other.analysisRecord?.sourceEdgeId??other.analysisRecord?.targetEdgeId)===id(sourceEdgeId));
 return `<h2 class="coverage-${esc(span.kind)}">${esc(edge.label)}</h2>${rows(base)}<p class="notice">实际刀路只取皮边。沿该皮边的刀轴检查本面首达边界，将切透证据对应回来源皮边区间。凸边和侧边记录用于取证。${span.kind==='Pending'?' 此区间尚未完成映射，原因如下；其他已证明的区间保持各自判定。':''}</p>
  ${sideCorrespondenceEvidence(data,edge,owner)}
  ${coveragePointMarkerEvidence(data,span)}
  <h3>区间判定证据（${evidence.length}）</h3>${evidence.map(({index,record})=>coverageEvidenceHtml(record,index)).join('')||'<p class="muted">该区间尚无有效判定证据。</p>'}
  <h3>到达证据（${arrivals.length}）</h3>${arrivals.map(({index,arrival})=>arrival?`<div class="evidence"><b>arrival[${esc(index)}] · ${esc(arrival.sourcePathKey)}</b>${rows([['所属原面',arrival.faceId],['来源原边',`E${arrival.sourceEdgeId}`],['来源参数',nativeInterval(arrival.sourceFirstParameter,arrival.sourceLastParameter)],['目标原边',`E${arrival.targetEdgeId}`],['目标参数',nativeInterval(arrival.targetFirstParameter,arrival.targetLastParameter)],['目标分析边段',arrival.targetSpanKey],['映射方法',coverageExplanation(arrival.method)],['映射说明／失败原因',coverageExplanation(arrival.reason)]])}<details><summary>原始到达记录</summary>${raw(arrival)}</details></div>`:`<p class="bad">arrival[${esc(index)}] 缺失；保留原始索引。</p>`).join('')||'<p class="muted">此区间没有已展开的逐点到达记录。</p>'}
  ${sourceEvidence(sources.filter(source=>source.sourcePathKey===span.sourcePathKey),{title:'本皮边路径的映射方法与未完成原因'})}
  ${siblings.length?`<h3>同一来源原边的其他逐面／区间记录</h3><div class="edge-table">${siblings.map(other=>`<button class="edge-row" data-edge="${esc(other.pathKey)}">原面 ${esc(other.faceId)} · ${esc(other.label)}<small>${esc(nativeInterval(other.analysisRecord.firstParameter,other.analysisRecord.lastParameter))}</small></button>`).join('')}</div>`:''}
  <details><summary>完整皮边覆盖区间记录</summary>${raw(span)}</details>`;
}
function edgeDetails(data,edge,selectedFace=''){
 const record=edge.analysisRecord??{},workId=data.inspectionStage;
 if(workId==='faceCutCoverage')return coverageEdgeDetails(data,edge,selectedFace);
 if(workId==='penetration'){
  const depths=(first,last)=>[first,last].every(Number.isFinite)?`${number(first)} ～ ${number(last)} mm`:null;
  return `<h2>${esc(edge.label)}</h2>${rows([
   ['所属原面',record.faceId],['原边',record.edgeId],['刀路标识',record.pathKey],
   ['原生参数',nativeInterval(record.firstParameter,record.lastParameter)],
   ['出口依据',PENETRATION_EXIT[record.exit]??record.exit],['出口原边',record.exitWitnessEdgeId],
   ['出口深度（首／尾）',depths(record.firstExitDepthMm,record.lastExitDepthMm)],
   ['保留材料深度（首／尾）',depths(record.firstRetainedDepthMm,record.lastRetainedDepthMm)],
   ['挡料面 ID',record.blockingFaceId],['出口支撑面 ID',list(record.exitSupportFaceIds).join('、')],
   ['判定依据',PENETRATION_REASON[record.reason]??record.reason],
  ])}<details><summary>原生 Work 区间记录</summary>${raw(record)}</details>`;
 }
 const base=[['原边编号',edge.sourceEdgeIds?.map(v=>'E'+v).join('、')],['记录标识',edge.pathKey],['所属原面',(edge.faceIds??[]).join('、')]];
 if(workId==='edgeAnalysis')base.push(
  ['和皮的关系',SKIN[record.skinRelation]??record.skinRelation],['凹凸性',CONVEXITY[record.convexity]??record.convexity],
  ['皮关系有效',record.skinValid===undefined?'未记录':record.skinValid?'是':'否'],['凹凸性有效',record.convexityValid===undefined?'未记录':record.convexityValid?'是':'否'],
  ['参数范围',`${number(record.firstParameter)} ～ ${number(record.lastParameter)}`],['来源皮面',list(record.sourceSkinFaceIds).join('、')||'无来源归属记录'],
  ['最小离皮偏差',`${number(record.minSkinDistanceMm)} mm`],['最大离皮偏差',`${number(record.maxSkinDistanceMm)} mm`],
  ['皮关系依据',record.skinReason],['凹凸依据',record.convexityReason],['邻接依据',record.adjacencyMethod],['邻接偏差',`${number(record.maxAdjacencyGapMm)} mm`]);
 else if(workId==='contour')base.push(['轮廓类型',record.wireInner?'内轮廓':'外轮廓'],['线环',`W${record.wireId}`],['闭合线环',record.wireClosed?'是':'否'],['线环校验',record.wireValid===undefined?'未记录':record.wireValid?'通过':'未通过'],['轮廓面积',`${number(record.wireArea)} mm²`],['参数范围',`${number(record.first)} ～ ${number(record.last)}`]);
 else if(present(record.firstParameter))base.push(['参数范围',`${number(record.firstParameter)} ～ ${number(record.lastParameter)}`]);
 return `<h2>${esc(edge.label??'原始边')}</h2>${rows(base)}<details><summary>原始边记录</summary>${raw(record)}</details>`;
}
function faceDetails(data,face){
 const record=face.inspectionRecord??{},workId=data.inspectionStage;
 const edges=data.edges.filter(e=>(e.faceIds??[e.faceId]).map(id).includes(id(face.faceId)));
 const mesh=data.geometry?.faces?.find(f=>id(f.faceId)===id(face.faceId));
 const issues=(data.geometry?.issues??[]).filter(issue=>issue&&typeof issue==='object'&&id(issue.faceId)===id(face.faceId));
 if(!issues.length&&mesh?.displayIssue)issues.push({faceId:face.faceId,kind:'preview-unavailable',reason:mesh.displayIssue});
 const previewNotice=issues.length?`<div class="notice warning"><strong>该面${issues.some(issue=>issue.kind==='zero-area')?'未显示在三维预览中':'三维预览未能完整显示'}</strong>${issues.map(issue=>`<div>${esc(issue.kind==='zero-area'?'零面积面':issue.kind==='mesh-failed'?'网格生成失败':'预览问题')}：${esc(issue.reason??'未记录具体原因')}<br>${issue.topologyRetained===true?'原拓扑已保留；可下载本 Work 的原始产物查看。':'这条记录说明预览状态，不改变本 Work 的原始结果。'}</div>`).join('')}</div>`:'';
 const base=[['原面编号',face.faceId],['本阶段边记录',edges.length]];
 if(present(record.areaMm2))base.push(['原面面积',`${number(record.areaMm2)} mm²`]);
 if(present(record.surfaceKind??record.surfaceType))base.push(['原曲面类型',record.surfaceKind??record.surfaceType]);
 if(workId==='classify')base.push(['分类',face.machining?'加工面':'非加工面']);
 if(workId==='heal')base.push(['来源原面',record.sourceFaceId],['结果面',list(record.resultFaceIds).join('、')],['修复状态',record.status],['原曲面类型',record.sourceType],['结果曲面类型',record.resultType],['支撑偏差',present(record.supportDeviationMm)?`${number(record.supportDeviationMm)} mm`:null],['边界偏差',present(record.boundaryDeviationMm)?`${number(record.boundaryDeviationMm)} mm`:null],['修复依据',record.reason]);
 if(workId==='faceCutCoverage'){const sources=faceSources(data,face.faceId),spans=edges.filter(edge=>edge.workCoverage);base.push(['加工面',face.machining?'是':'否'],['来源路径',sources.length],['判定完成／未完成',`${sources.filter(source=>source.complete).length} / ${sources.filter(source=>!source.complete).length}`],['切透／半切透／待定区间',`${spans.filter(edge=>edge.analysisRecord.kind==='Through').length} / ${spans.filter(edge=>edge.analysisRecord.kind==='Partial').length} / ${spans.filter(edge=>edge.analysisRecord.kind==='Pending').length}`]);}
 if(workId==='penetration')base.push(['切透／半切透／待定',Object.keys(PENETRATION).map(kind=>edges.filter(edge=>edge.analysisRecord.kind===kind).length).join(' / ')]);
 return `<h2>${workId==='classify'?(face.machining?'加工面':'非加工面'):'原面'} ${esc(face.faceId)}</h2>${previewNotice}${rows(base)}${Object.keys(record).length?`<details><summary>本阶段面记录</summary>${raw(record)}</details>`:''}${workId==='faceCutCoverage'?sourceEvidence(faceSources(data,face.faceId)):''}<h3>${workId==='penetration'?'此面的判定区间':workId==='faceCutCoverage'?'此面的边与覆盖区间':'此面的边'}</h3><div class="edge-table">${edges.map(edge=>`<button class="edge-row${edge.workCoverage?' coverage-'+esc(PENETRATION[edge.analysisRecord.kind]??edge.analysisRecord.kind):''}" data-edge="${esc(edge.pathKey)}">${esc(edge.label??edge.pathKey)}${edge.workCoverage?`<small>原生参数 ${esc(nativeInterval(edge.analysisRecord.firstParameter,edge.analysisRecord.lastParameter))}</small>`:''}</button>`).join('')||'<p class="muted">该阶段未输出逐面边记录。</p>'}</div>`;
}
function summaryFacts(data){
 const meta=data.inspection?.rawData??{},workId=data.inspectionStage;
 const facts=[['模型面',data.faces.length],['边记录',data.edges.length]];
 if(workId==='heal')facts.push(['转换面',meta.convertedFaces],['合并面',meta.mergedFaces],['线性容差',present(meta.linearToleranceMm)?`${number(meta.linearToleranceMm)} mm`:null]);
 if(workId==='align')facts.push(['识别方向',Array.isArray(meta.direction)?meta.direction.map(number).join(', '):null],['候选方向数',meta.candidateCount],['置信度',number(meta.confidence)]);
 if(workId==='contour')facts.push(['闭合轮廓',meta.wires?.length],['截面 X',present(meta.endX)?`${number(meta.endX)} mm`:null]);
 if(workId==='classify')facts.push(['加工面',data.faces.filter(f=>f.machining).length],['非加工面',data.faces.filter(f=>!f.machining).length]);
 if(workId==='edgeAnalysis')facts.push(['源边',meta.counts?.sourceEdges],['分析边段',meta.edges?.length],['排除的参数接缝／退化边',meta.excludedEdges?.length],['邻接范围',meta.adjacencyScope==='machining-faces-only'?'仅加工面':meta.adjacencyScope]);
 if(workId==='faceCutCoverage')facts.push(['加工面',data.faces.filter(face=>face.machining).length],['来源路径',meta.sources?.length],['判定完成／未完成',`${(meta.sources??[]).filter(source=>source.complete).length} / ${(meta.sources??[]).filter(source=>!source.complete).length}`],['到达区间',meta.arrivals?.length],['切透区间',meta.coverage?.filter(span=>span.kind==='Through').length],['半切透区间',meta.coverage?.filter(span=>span.kind==='Partial').length],['判定待定区间',meta.coverage?.filter(span=>span.kind==='Pending').length],['未完成原面',list(meta.incompleteFaceIds).join('、')||'无']);
 if(workId==='penetration')facts.push(['切透区间',meta.counts?.through],['半切透区间',meta.counts?.blind],['待定区间',meta.counts?.pending]);
 return rows(facts);
}

export function renderWorkDetails(data,{selectedFace='',selectedEdge=''}={}){
 const workId=data.inspectionStage,inspection=data.inspection??{},entry=inspection.entry??{};
 const edge=selectedEdge?data.edges.find(e=>id(e.pathKey)===id(selectedEdge)):null;
 const face=selectedFace?data.faces.find(f=>id(f.faceId)===id(selectedFace)):null;
 const headline=`<div class="notice${['failed','missing'].includes(entry.status)?' warning':''}"><strong>${esc(inspection.label??WORK_LABELS[workId]??workId)}</strong> · ${esc(STATUS[entry.status]??entry.status??'未记录')}${entry.reason?`<br>${esc(entry.reason)}`:''}${['partial','failed'].includes(entry.previewStatus)?`<br><span class="bad">${entry.previewStatus==='partial'?'三维预览部分完成':'三维预览失败'}：${esc(entry.previewReason??'详见阶段诊断')}</span><br>本 Work 的原始产物与已成功的预览面仍保留。`:''}</div>`;
 const body=edge?edgeDetails(data,edge,selectedFace):face?faceDetails(data,face):`<h2>${esc(inspection.label??WORK_LABELS[workId]??workId)}</h2>${summaryFacts(data)}${workId==='align'&&Array.isArray(inspection.rawData?.transform4x4)?`<details><summary>原始对齐矩阵</summary>${raw(inspection.rawData.transform4x4)}</details>`:''}${workId==='edgeAnalysis'&&inspection.rawData?.excludedEdges?.length?`<details><summary>排除的参数接缝／退化边记录</summary>${raw(inspection.rawData.excludedEdges)}</details>`:''}`;
 const diagnostics=(data.diagnostics??[]).filter(present);
 return `${headline}${body}${provenanceHtml(data)}${diagnostics.length?`<details><summary>本阶段诊断（${diagnostics.length}）</summary><ul class="diagnostics">${diagnostics.map(message=>`<li>${esc(workId==='faceCutCoverage'?coverageExplanation(message):message)}</li>`).join('')}</ul></details>`:''}`;
}

export function workLegend(workId){
 if(workId==='penetration')return '<div><span class="coverage-Through">切透 · 绿实线</span><span class="coverage-Partial">半切透／盲孔 · 橙实线</span><span class="coverage-Pending">待定 · 灰虚线</span></div><div>点选区间查看所属原面、截面出口和挡料证据。</div>';
 if(workId==='faceCutCoverage')return '<div><span class="coverage-Through">切透 · 绿色</span><span class="coverage-Partial">半切透 · 橙色</span><span class="coverage-Pending">判定待定 · 灰色粗线</span><span style="color:#849ab2">原加工边 · 灰色细线</span></div><div>仅皮边生成实际刀路。到达皮边、非皮凸边或有效端点侧边补全的区间判为切透；已证明凹边终止并保留材料、且无有效侧边补全的区间判为半切透；其余待定。点击区间查看对应原边、原生参数和判定依据。</div>';
 if(workId==='pathAnalysis')return '<div><span style="color:#1fe05c">走刀路径 · 绿色实线</span><span style="color:#f4df91">刀轴方向 · 淡黄色箭头</span><span style="color:#ff303b">侧边 · 红色虚线</span><span style="color:#849ab2">其他加工边 · 灰色细线</span></div><div>实际刀路仅取皮边；非皮凸边保留为切透证据。每条逻辑边仅处理首尾两个端点，所有原边保留。</div>';
 if(workId==='edgeAnalysis')return '<div><span style="color:#00B8D9">在皮上 · 青色</span><span style="color:#F59E0B">不在皮上 · 橙色</span><span style="color:#A855F7">部分在皮上 · 紫色</span></div><div>凸 · 实线　凹 · 虚线　共面／光顺 · 点划线　自由边 · 点线</div>';
 if(workId==='classify')return '<div><span style="color:#ffad57">加工面 · 橙色</span><span style="color:#a1aab8">非加工面 · 灰色</span></div><div>完整模型保留；点击面查看原面编号及分类。</div>';
 if(workId==='contour')return '<div><span style="color:#1fe05c">外轮廓 · 绿色</span><span style="color:#ff303b">内轮廓 · 红色</span></div><div>轮廓位于该 Work 输出的截面位置；折线仅用于显示。</div>';
 if(workId==='heal')return '<div><span style="color:#52c8a2">转换面 · 青绿色</span><span style="color:#f1ab60">拒绝转换 · 橙色</span><span style="color:#a1aab8">保留面 · 灰色</span></div><div>点击面查看来源面、修复结果和几何偏差。</div>';
 return '<div><span style="color:#a1aab8">原始面和原始边 · 灰色</span></div><div>展示保存的该阶段产出；点击面或边查看编号与来源。</div>';
}
