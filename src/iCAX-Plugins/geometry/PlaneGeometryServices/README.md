# PlaneGeometryServices

`PlaneGeometryServices.dll` 注册框架 `IPlaneGeometryService`，经 `Scene.Services().Resolve<IPlaneGeometryService>()` 注入。接口在 [PlaneGeometryService.h](../../../iCAX-Engine/framework/GeometryServices/PlaneGeometryService.h)，基础数据解码在 [PlaneGeometryData.h](../../../iCAX-Engine/framework/GeometryServices/PlaneGeometryData.h)。

服务接收几何快照与参数，返回中性曲线、路径、尺寸、吸附点或阵列候选。它不访问 Scene、Repository、EC、历史或项目资源，不判断零件/板材/底图身份；业务校验、版本、锁、事务及发布由产品 SDO/Logic 处理。

| 接口 | 作用 |
| --- | --- |
| `CreateGeometry` | 直线、多段线、矩形、圆、圆弧、椭圆、椭圆弧、贝塞尔、B样条、三角形、正多边形、星形；仅保留几何字段 |
| `BuildGeometry` | 由解析曲线构造中性几何，按需校验闭合材料、相交及内孔 |
| `ImportGeometry` | 将已有中性 CAD 文档映射为上述曲线，校验平面与单位 |
| `TransformGeometry` | 移动/复制、带中心旋转/缩放、镜像、闭合、尺寸及既有节点坐标修改 |
| `ArrayGeometry` | 矩形/圆周阵列候选，含一份恒等实例 |
| `DescribeDrawingClosure` | 只读派生闭合可用性 |
| `DescribeDrawingDimensions` | 只读长度、半径、局部矩形尺寸和弧角描述 |
| `PlaceGeometry` | 按排样姿态投影显示路径 |

矩形阵列使用整数 `rows`、`columns` 和 `spacingX`、`spacingY`；圆周阵列使用整数 `count`、中心 `cx/cy` 和 `angleDeg`（默认360）。总数为2至500，整周避免重复终点，部分圆周包含两端。节点编辑要求单一直线/多段线且点数不变，保留原闭合及 bulge 属性。

独立图元由底图和所属草图共用同一 `CreateGeometry`，返回一条权威曲线：

| primitive | 参数 | 精确输出 |
| --- | --- | --- |
| `ellipse` | `center/radiusX/radiusY/rotationDeg` | 单段 `polycurve/ellipseArc`，从0至360度，闭合 |
| `ellipseArc` | 同上，加 `startAngleDeg/sweepAngleDeg` | 单段精确椭圆弧，保留有符号扫角；整周闭合 |
| `bezier` | `controlPoints`，可选正 `weights` | 单段精确贝塞尔，保存控制点及权重 |
| `bspline` | `controlPoints`，`degree`默认3，可选完整夹紧 `knots`、正 `weights` | 单段精确B样条，保存次数、控制点、节点及权重；默认至少4个控制点 |
| `triangle` | 恰3个不共线 `points` | 闭合直边多段线 |
| `polygon` | `center/radius/sides/rotationDeg`，边数默认6 | 正多边形的精确顶点，边数为3至64整数 |
| `star` | `center/outerRadius/innerRadius/tips/rotationDeg`，角数默认5 | 交替外、内半径的精确顶点，角数为3至64整数，`0<innerRadius<outerRadius` |

椭圆参数角相对于其局部X轴；旋转和参数角均为度。新图元使用现有混合曲线/OCCT构造、变换、误差界、材料校验和持久化，不把显示采样折线当作精确曲线。业务只发布一个对象及其EC；无效有限数、退化图形或非法计数在发布资源与提交事务前拒绝。

`BuildGeometry` 和 `ArrayGeometry` 支持取消回调；取消抛出 `PLANE_GEOMETRY_CANCELLED`，业务层将其转换为任务状态。服务函数本身同步，由框架 Task 决定后台执行和提交时机。

几何算法在 [PlaneGeometryAlgorithms.cpp](PlaneGeometryAlgorithms.cpp)，尺寸描述/修改在 [PlaneGeometryDimensions.cpp](PlaneGeometryDimensions.cpp)，服务注册在 [PlaneGeometryService.cpp](PlaneGeometryService.cpp)。材料相交验证复用 OpenCascade；独立排样内核仍由 `PlaneNestingServices` 封装，此服务不执行排样。

混合曲线在 [PlaneMixedCurves.cpp](PlaneMixedCurves.cpp) 实现。`primitive: "polyline"` 可传 `segments`，输出单一 `type: "polycurve"`，连续保存 `line`、`arc`、`ellipseArc`、`bezier`、`bspline`。圆锥曲线使用有符号 `sweepAngleDeg`；B样条保存阶次、控制点、完整展开节点向量和可选正权重。非周期夹紧B样条支持1至25次、最多256个控制点；贝塞尔最多26个控制点，一条混合曲线最多512段（含闭合段）。

精确曲线由 OpenCascade 构造，显示与排样路径另行采样，弦误差上限0.05mm，连接误差另外计入 `approximationBoundMm`。整体变换、阵列、闭合、材料校验、包围尺寸及保存重开保留精确数据；混合曲线的单段尺寸与控制点编辑暂不开放。

本轮原生中性输出、中心旋转、镜像弧/bulge、阵列、节点和取消由 [GeometryServiceTests.inc](../../../tests/icax-plugins/product/iPlaneCAM/GeometryServiceTests.inc) 验证，并在产品测试中比较直接调用前后的 EC、资源和历史，保证服务不写业务状态。
