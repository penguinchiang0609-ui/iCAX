# OpenCascadeGeometryServices

可复用的基础几何服务插件，使用项目已有 OCCT 8.0.0-p1。公共接口为 `iCAX-Engine/framework/GeometryServices/CurveOffset3DService.h`；纯数据定义在 `foundation/GeometryData/CurveOffset3D.h`。接口不暴露 OCCT 类型，不依赖 CAM、产品、资源池、SDO 或场景。

提供单段参数偏置求值，以及复合曲线的偏置、交点裁剪、角点连接和自交去环。使用 OCCT 求原始曲线与导数，输出保留偏置表达式，不将拟合样条或显示折线冒充精确偏置。服务可独立调用；FiveAxisCAM 的 CamPlan.Generate 已接入复合偏置、实际弧长遍历及补偿后引线。

通过现有 `AUTO_REGIST_SERVICE` 注册 `ICurveOffset3DService`，应用加载模块后按模块路径回放到 `CServiceProvider`。测试动态加载 DLL 并只回放该模块，验证没有加载 FiveAxisCAM 或 ProcessPlanning。插件不持有用户数据，各请求的 OCCT 对象在调用内创建，可并发求值。

## 当前实现

`Evaluate(law, parameters, options)` 对下列几何偏移定义求位置和一阶导数：

```text
Q(u) = P(u) + r * normalize(D(u) × P'(u))
```

- `P` 覆盖当前中性 `Curve3` 的全部 12 种类型：Line、Ray、Segment、Circle / Arc、Ellipse / EllipseArc、Polyline、Bezier、BSpline、NURBS、Clothoid。BSpline / NURBS 同时支持周期与非周期，保留原生参数。
- `D` 支持固定方向、同参数的 Bezier / BSpline / NURBS 向量场（包含周期），以及 SurfaceNormalField3 的精确曲面法向场。向量场极点表示向量分量，刚体变换时只旋转、不平移。
- 固定方向的光滑 OCCT 曲线（周期样条除外）委托 `Geom_OffsetCurve`，对 OCCT 的叉乘约定转换距离符号。其他情况在曲线及微分求值之上计算完整的偏置导数，包括方向场导数。
- 单段求值要求请求区间内部原曲线 C2、方向场 C1。使用裁剪区间的单侧导数，区间外的低连续性结点不阻止求值。复合构建会按源样条、折线和方向场的低连续性结点拆段，仍保持原始参数与源段编号；折线作为一条输入即可完成拆段、偏置和接续。
- `Range.First < Range.Last`。`Range.Reversed` 仅表达遍历，返回导数始终对递增原生参数求导。输入曲线本身重新参数化为反向时，保持物理偏移侧需要调用方同时翻转 `r`。
- 检查非有限数、非法样条、参数域、零切向、零方向、近似平行方向和采样点处偏移尖点/塌缩；错误或取消原子返回，不残留部分样本。

单段求值成功状态为 **SamplesEvaluated / RequestedParametersOnly**；它只说明请求参数处求值成功，不证明采样之间无奇异或自交。

### 参数与新增曲线类型

| 类型 | 参数及边界 |
| --- | --- |
| Line / Ray | 沿单位方向的距离：`P(u)=Location+u*Direction`。Ray 要求 `u >= First`，First 不会被重新置零。 |
| Segment / Bezier | 原生区间 `[0,1]`。 |
| Circle / Ellipse | 弧度；Arc / EllipseArc 保持原弧的裁剪域。 |
| Polyline | 点下标参数，`[i,i+1]` 对应相邻两点。闭合折线未重复首点时追加闭合边；已重复则不重复添加。以精确一次样条求值，无拟合；返回 law 仍保留 Polyline。尖角没有唯一偏置点，须通过 BuildComposite 取得两侧分段与连接。 |
| 周期 BSpline / NURBS | 采用 OCCT 的周期极点／结点规则。允许跨接缝、整体平移若干周期的有限参数范围，不将返回的 u 取模。内部仅在求值时定位对应周期，并按区间端点选择左／右导数；拆段与采样使用平移后的结点。 |
| Clothoid | `u=s` 为弧长，`[0,Length]`；位于 Placement 的 XY 平面，起点为 Location、起始切向为 XDirection。曲率线性变化，`theta(s)=k0*s+(k1-k0)*s²/(2*Length)`。位置为余弦／正弦积分，导数直接解析求值。任意三维放置及变刀轴均可用。 |

所有构建输入仍须提供有限的 `Range`，不生成无限长刀路。回旋线保留解析定义，位置数值求值采用限制相位变化的自适应 Gauss 积分；`IntegrationTolerance` 默认 `1e-10`，每次请求的 `MaxIntegrationEvaluations` 默认 65536，预算耗尽／取消会明确失败。恒定曲率直接用稳定的圆弧／直线极限式，接近恒定曲率不通过两个大 Fresnel 值相减。积分误差估计不等于严格区间证明。

周期数据约束参见 [OCCT Geom_BSplineCurve 官方文档](https://occt3d.com/dev/doc/refman/html/class_geom___b_spline_curve.html)；回旋线的基准算例采用 [NIST DLMF Fresnel 积分定义](https://dlmf.nist.gov/7.2#iii)。

## 复合曲线构建

```cpp
auto result = service->BuildComposite(laws, closed, options);
if (result.Status != ECompositeOffsetStatus3::Built) {
    // No partial curve is returned. Report result.Diagnostic.
}
// result.Curve.Segments contains trimmed offset laws and explicit connectors.
// Evaluate any returned piece at its native u with EvaluatePiece(piece, {u}).
```

`laws` 每段包含源曲线、原生参数区间、刀轴方向场和有符号偏置距离。距离是实际偏置量；切缝宽度是否除以二、偏置物理侧等由 CAM 业务决定。`Range.Reversed` 不改变几何侧，调用方若按遍历方向定义左右侧，应在建立各段 law 时转换距离符号。

构建顺序：

1. 检查原始段连续性，按低连续性结点分段。
2. 对偏置表达式自适应细分，用三维包围盒与空间弦段距离查找交点候选。在两个参数域内细化求解 `Q1(u)=Q2(v)`，不做二维投影。
3. 相邻段有交点时裁剪原生参数区间；无交点时生成独立连接段。支持有长度上限的尖角、斜接、圆弧。尖角两切线异面或超过限长时采用斜接；圆弧要求等半径且转向可确定。
4. 检测包括同一源段内部在内的自交，切分、去环后重新检测，直到无交点或达到预算。沿曲线距离小于容差邻域的相邻位置不作为自交，避免短连接段误判。
5. 检查输出接续和闭合。出错、取消、预算耗尽时原子返回，不输出残缺曲线。

输出 `CompositeOffsetCurve3` 是可重新求值的复合曲线：偏置段保留 `DirectionalCurveOffset3` 和被裁剪的原始 `u`；连接段为 `CurveSegment3`，使用独立参数，且没有伪造源段编号。

开放曲线去环保留首尾；闭合曲线默认 `LoopRetention=Longest`，保留最长分支，也可用 `Start` 保留包含原起点的分支。被移除的闭环通过 `RemovedLoops` 返回供调用方检查。空间曲线没有通用的平面内外绕向判据；多分支的材料保留含义仍由 CAM 决定，不能把所有被去除的环自动解释为废料。

交点检测是容差控制的数值算法，默认距离容差 `1e-5`，不是整个参数区间的严格无交证明。极小特征、重合段、奇异点和超预算会返回错误或需要调用方选用更严格的容差。没有曲面贴合、刀头碰撞或可加工性认证。

CAM 层负责把切缝测量模型、实际工具方向和物理废料侧转换为 `D/r`；SDO 负责版本、失效和事务。CamPlan.Generate 已接入本服务，圆形局部工艺检查仍是独立入口。支持范围和产品验证见 src/apps/five-axis-cam/README.md 的 2026-10-15 记录。

## 构建与验证

已加入 `src/iCAX.slnx`。提供 Debug / Release x64 项目；Win32 解决方案配置不构建这两个 OCCT x64 项目。复用 `OpenCascade.props` 和现有运行库部署目标，不引入第二套 OCCT。

构建服务后构建 `src/tests/icax-plugins/geometry/OpenCascadeGeometryServicesTest/OpenCascadeGeometryServicesTest.vcxproj`，运行同名测试程序。依赖 Services DLL 和 OCCT 运行库；测试程序只引用公共中性接口。

原生测试位于 `CurveOffset3DTests.cpp`，覆盖非共面三次曲线、变方向及导数、原生参数、NURBS 权重、内外角接续、同段与跨段自交、偏置新生环、三维异面投影交叉、短连接段、闭环保留策略、样条结点拆段、反向、刚体变换、退化、取消与预算。报告：`output/tests/curve-offset-composite-tests.xml`。

曲线类型扩展的回归包含周期跨缝、非均匀小数结点的单侧导数、周期有理圆、周期刀轴场、折线开闭合及全局参数、射线下界、回旋线 Fresnel 基准／独立 Simpson 积分／恒定曲率极限／变刀轴／积分预算。完整报告：`output/tests/curve-offset-types-tests.xml`。

`FiveAxisCAMTest` 的 `CurveOffsetModel.*` 测试从原厂 `Pololu-2257-stamped-bracket.step` 读取精确边界；12 条轮廓各取正负 0.1 mm 偏置，共 24 案例，包含跨折弯面的空间外轮廓。测试检查精确 law 的偏置距离、原生参数求值和接续；报告 `output/tests/curve-offset-model-tests.xml`，查看样本 `output/tests/curve-offset-stamped-bracket.csv`。这不是产品 UI、完整 CAM 工艺或实机验收。

选型依据及后续接口边界见 `doc/iCAX-Engine/Foundation/三维曲线偏移服务与开源库选型.md`。
