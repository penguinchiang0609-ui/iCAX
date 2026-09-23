# 真实厂商管材 STEP 测试集

采集日期：2026-09-23。共 47 个不同文件，原始 STEP 合计 125.03 MiB，来自 5 家厂商的公开产品 CAD；含 30 个开孔/加工管材零件、17 个管材原料模型。

保留厂商原始几何，不重建、不简化、不更改坐标和单位声明。goBILDA 样件从厂商提供的 ZIP 中原样解压；其余直接下载 STEP。文件名按型号统一，原名/来源/下载地址/大小/SHA-256 保存在 [manifest.json](manifest.json)。

本批覆盖方管、矩形管、圆管、外圆内六角管、内壁带螺母槽的异形管，以及密集孔阵列、MAXSpline 轮廓孔、轴承孔、薄壁、厚壁、短件与长件。部分同系列型号共享截面，但孔位、壁厚或长度不同，适合检查识别稳定性。

## 来源与实际用途

| 厂商 | 数量 | 内容 | 来源 |
|---|---:|---|---|
| AndyMark | 6 | 开孔方/矩形管、NanoTube 传动底盘管梁 | [开孔管](https://andymark.com/products/pre-drilled-box-tube-extrusion)、[NanoTube](https://www.andymark.com/products/nano-tube-20-in-aluminum-black-finish) |
| REV Robotics | 24 | MAXTube 1×1、2×1 管，孔阵列、MAX Pattern、内壁螺母槽 | [2×1 目录](https://revrobotics.eu/MAXTube-2x1/?setCurrencyId=1)、[1×1 目录](https://revrobotics.eu/MAXTube-1x1/?setCurrencyId=3) |
| Tubecon | 12 | 矩形、方形、圆形钢管原料模型 | [厂商 CAD 目录](https://tubecon.co.za/technicalinfo/tubeconcaddrawings) |
| West Coast Products | 4 | 圆管和外圆内六角管 | [CTR Electronics 产品/CAD 页](https://store.ctr-electronics.com/products/wcp-aluminum-round-tube-stock) |
| goBILDA | 1 | 12 mm 内径、14 mm 外径、300 mm 长铝管 | [ServoCity 产品/CAD 页](https://www.servocity.com/4100-series-aluminum-tube-12mm-id-x-14mm-od-300mm-length/) |

REV 的 31 英寸及 5 英寸 MAXTube 在[官方 West Coast 传动底盘装配说明](https://docs.revrobotics.com/ion-build/build-guides/rev-ion-west-coast-drivetrain-assembly)中有明确装配用途。NanoTube 是真实目录旧产品，原网页标记已停产。Tubecon 是原料截面模型，不将其计为已加工零件。

本批全部采用 STEP，来自公开产品 CAD；每个文件保留来源，以便核对实际型号和用途。

## 建议先测试的 10 个样件

| 文件 | 重点 |
|---|---|
| [am-2200.step](andymark/am-2200.step) | 真实底盘管梁；大轴承孔、安装孔、偏心孔位；检查加工孔对轴向判断的干扰。 |
| [am-5180.step](andymark/am-5180.step) | 厚壁矩形管及多面孔阵列；检查轮廓边合并、端点拆边。 |
| [am-5001-4700.step](andymark/am-5001-4700.step) | 细长小方管、密集孔；检查尺度与孔阵列干扰。 |
| [REV-21-2163.step](rev/REV-21-2163.step) | 3 英寸短管、大 MAXSpline 轮廓孔、内部螺母槽；检查短件轴向与异形内轮廓。 |
| [REV-21-2160.step](rev/REV-21-2160.step) | 1×1 英寸开孔管，内壁带螺母槽；不能只按外轮廓拟合普通方管。 |
| [REV-21-2289.step](rev/REV-21-2289.step) | 长薄壁管及密集孔阵列，内壁带槽；检查完整轮廓保留。 |
| [REV-21-3580.step](rev/REV-21-3580.step) | 47 英寸 MAX Pattern 厚壁管；检查复杂加工孔与长件处理耗时。 |
| [WCP-1142.step](wcp/WCP-1142.step) | 外圆内六角管；检查内外轮廓必须同时匹配，不能误识别为普通圆管。 |
| [round_-_38.1_x_0.90.step](tubecon/round_-_38.1_x_0.90.step) | 薄壁圆钢管；检查内外圆半径、壁厚与公差。 |
| [4100-1214-0300.step](gobilda/4100-1214-0300.step) | 内径 12、外径 14、长度 300 mm 的圆管基线样本。 |

## 验证范围

- 已核对 47 个文件的 STEP 文件头/结尾、文件大小与 SHA-256，无完全重复文件。
- 原生几何导入检查已记录 47/47 个结果，详见 [import-check.csv](import-check.csv)。使用项目同版本 Open CASCADE 8.0.0-p1，读取 STEP、转移实体、统计实体/面数、检查 BRep 有效性，并记录毫米制包围盒和体积。
- 其中 47 个通过读取、单实体和 BRep 有效性三项检查。
- 已完成 47 个样件的原生轴向识别、轮廓提取和直接 fitting：47 个轴向和轮廓检查通过，34 个匹配成功，13 个特殊截面未匹配。详见[完整测试报告](recognition/REPORT.md)、[轮廓对比图](recognition/contours.html)和[逐项结果](recognition/summary.csv)。
- 识别结果记录了轴向、轮廓闭合及孔洞、最低位置首边、模板匹配、参数和位姿、处理耗时。发现的问题已修复并部署，32 项原生回归和 2 项产品接口回归通过。
- 有螺母槽或内六角的样件必须检查完整内外轮廓；没有相应模板时应如实报告不匹配或 unsupported，不能强套普通方管/圆管。

## 全部文件

| 文件 | 厂商规格/型号 | 测试特征 |
|---|---|---|
| [andymark/am-2200.step](andymark/am-2200.step) | am-2200 NanoTube20 Rev4-1.STEP | 轴承孔/偏心孔位 |
| [andymark/am-5001-4700.step](andymark/am-5001-4700.step) | am-5001-4700 Robits 0.5x0.5x47.0 Tube.STEP | 孔阵列 |
| [andymark/am-5177.step](andymark/am-5177.step) | am-5177 1x1x0.063 Wall Pre-Drilled Box Tube.STEP | 孔阵列 |
| [andymark/am-5178.step](andymark/am-5178.step) | am-5178 1x1x0.125 Wall Pre-Drilled Box Tube.STEP | 孔阵列 |
| [andymark/am-5179.step](andymark/am-5179.step) | am-5179 2x1x0.063 Wall Pre-Drilled Box Tube.STEP | 孔阵列 |
| [andymark/am-5180.step](andymark/am-5180.step) | am-5180 2x1x0.125 Wall Pre-Drilled Box Tube.STEP | 孔阵列 |
| [gobilda/4100-1214-0300.step](gobilda/4100-1214-0300.step) | 4100 Series Aluminum Tube (12mm ID x 14mm OD, 300mm Length) | 圆管、薄壁、圆孔 |
| [rev/REV-21-2160.step](rev/REV-21-2160.step) | MAXTube - 1x1 - 47in | 方管、内壁螺母槽 |
| [rev/REV-21-2161.step](rev/REV-21-2161.step) | MAXTube - 2x1 - Light - 47in | 矩形管、薄壁、内壁螺母槽、窄侧面安装孔 |
| [rev/REV-21-2162.step](rev/REV-21-2162.step) | MAXTube - 2x1 - 47in | 矩形管、内壁螺母槽、窄侧面安装孔 |
| [rev/REV-21-2163.step](rev/REV-21-2163.step) | MAXTube - 2x1 - MAX Pattern - 1-Pos (3in) | 矩形管、安装孔、内壁螺母槽、MAXSpline 轮廓孔 |
| [rev/REV-21-2164.step](rev/REV-21-2164.step) | MAXTube - 2x1 - MAX Pattern - 2-Pos (5in) | 矩形管、安装孔、内壁螺母槽、MAXSpline 轮廓孔 |
| [rev/REV-21-2165.step](rev/REV-21-2165.step) | MAXTube - 2x1 - MAX Pattern - 3-Pos (7in) | 矩形管、安装孔、内壁螺母槽、MAXSpline 轮廓孔 |
| [rev/REV-21-2169.step](rev/REV-21-2169.step) | MAXTube - 2x1 - MAX Pattern - 7-Pos (15in) | 矩形管、安装孔、内壁螺母槽、MAXSpline 轮廓孔 |
| [rev/REV-21-2173.step](rev/REV-21-2173.step) | MAXTube - 2x1 - MAX Pattern - 11-Pos (23in) | 矩形管、安装孔、内壁螺母槽、MAXSpline 轮廓孔 |
| [rev/REV-21-2177.step](rev/REV-21-2177.step) | MAXTube - 2x1 - MAX Pattern - 15-Pos (31in) | 矩形管、安装孔、内壁螺母槽、MAXSpline 轮廓孔 |
| [rev/REV-21-2185.step](rev/REV-21-2185.step) | MAXTube - 2x1 - MAX Pattern - 23-Pos (47in) | 矩形管、安装孔、内壁螺母槽、MAXSpline 轮廓孔 |
| [rev/REV-21-2289.step](rev/REV-21-2289.step) | MAXTube - 2x1 - Light - Grid Pattern - 47in | 矩形管、孔阵列、薄壁、内壁螺母槽 |
| [rev/REV-21-2590.step](rev/REV-21-2590.step) | MAXTube - 2x1 - Grid Pattern - 47in | 矩形管、孔阵列、内壁螺母槽 |
| [rev/REV-21-3540.step](rev/REV-21-3540.step) | MAXTube - 1x1 - 1/16in Wall - Grid Pattern - 47in | 方管、孔阵列 |
| [rev/REV-21-3543.step](rev/REV-21-3543.step) | MAXTube - 1x1 - 1/8in Wall - Grid Pattern - 47in | 方管、孔阵列 |
| [rev/REV-21-3552.step](rev/REV-21-3552.step) | MAXTube - 2x1 - 1/16in Wall - Grid Pattern - 47in | 矩形管、孔阵列 |
| [rev/REV-21-3555.step](rev/REV-21-3555.step) | MAXTube - 2x1 - 1/8in Wall - Grid Pattern - 47in | 矩形管、孔阵列 |
| [rev/REV-21-3558.step](rev/REV-21-3558.step) | MAXTube - 2x1 - 1/8in Wall - MAX Pattern - 1-Pos (3in) | 矩形管、安装孔、MAXSpline 轮廓孔 |
| [rev/REV-21-3559.step](rev/REV-21-3559.step) | MAXTube - 2x1 - 1/8in Wall - MAX Pattern - 2-Pos (5in) | 矩形管、安装孔、MAXSpline 轮廓孔 |
| [rev/REV-21-3560.step](rev/REV-21-3560.step) | MAXTube - 2x1 - 1/8in Wall - MAX Pattern - 3-Pos (7in) | 矩形管、安装孔、MAXSpline 轮廓孔 |
| [rev/REV-21-3564.step](rev/REV-21-3564.step) | MAXTube - 2x1 - 1/8in Wall - MAX Pattern - 7-Pos (15in) | 矩形管、安装孔、MAXSpline 轮廓孔 |
| [rev/REV-21-3568.step](rev/REV-21-3568.step) | MAXTube - 2x1 - 1/8in Wall - MAX Pattern - 11-Pos (23in) | 矩形管、安装孔、MAXSpline 轮廓孔 |
| [rev/REV-21-3572.step](rev/REV-21-3572.step) | MAXTube - 2x1 - 1/8in Wall - MAX Pattern - 15-Pos (31in) | 矩形管、安装孔、MAXSpline 轮廓孔 |
| [rev/REV-21-3580.step](rev/REV-21-3580.step) | MAXTube - 2x1 - 1/8in Wall - MAX Pattern - 23-Pos (47in) | 矩形管、安装孔、MAXSpline 轮廓孔 |
| [rev/REV-21-3586.step](rev/REV-21-3586.step) | MAXTube - 2x1 - 3/16in Wall - Grid Pattern - 47in | 矩形管、孔阵列 |
| [tubecon/rectangle_-_100.0_x_25.0_x_1.60.step](tubecon/rectangle_-_100.0_x_25.0_x_1.60.step) | Rectangle - 100.0 X 25.0 X 1.60.step | 矩形管、未开孔原料 |
| [tubecon/rectangle_-_100.0_x_25.0_x_3.00.step](tubecon/rectangle_-_100.0_x_25.0_x_3.00.step) | Rectangle - 100.0 X 25.0 X 3.00.step | 矩形管、未开孔原料 |
| [tubecon/rectangle_-_100.0_x_50.0_x_1.60.step](tubecon/rectangle_-_100.0_x_50.0_x_1.60.step) | Rectangle - 100.0 X 50.0 X 1.60.step | 矩形管、未开孔原料 |
| [tubecon/rectangle_-_80.0_x_40.0_x_4.50.step](tubecon/rectangle_-_80.0_x_40.0_x_4.50.step) | Rectangle - 80.0 X 40.0 X 4.50.step | 矩形管、未开孔原料 |
| [tubecon/rectangle_-_80.0_x_60.0_x_1.60.step](tubecon/rectangle_-_80.0_x_60.0_x_1.60.step) | Rectangle - 80.0 X 60.0 X 1.60.step | 矩形管、未开孔原料 |
| [tubecon/round_-_34.1_x_2.50.step](tubecon/round_-_34.1_x_2.50.step) | Round - 34.1 X 2.50.step | 圆管、未开孔原料 |
| [tubecon/round_-_34.9_x_1.20.step](tubecon/round_-_34.9_x_1.20.step) | Round - 34.9 X 1.20.step | 圆管、未开孔原料 |
| [tubecon/round_-_38.1_x_0.90.step](tubecon/round_-_38.1_x_0.90.step) | Round - 38.1 X 0.90.step | 圆管、未开孔原料 |
| [tubecon/square_-_100.0_x_100.0_x_2.50.step](tubecon/square_-_100.0_x_100.0_x_2.50.step) | Square - 100.0 X 100.0 X 2.50.step | 方管、未开孔原料 |
| [tubecon/square_-_100.0_x_100.0_x_6.00.step](tubecon/square_-_100.0_x_100.0_x_6.00.step) | Square - 100.0 X 100.0 X 6.00.step | 方管、未开孔原料 |
| [tubecon/square_-_70.0_x_70.0_x_2.00.step](tubecon/square_-_70.0_x_70.0_x_2.00.step) | Square - 70.0 X 70.0 X 2.00.step | 方管、未开孔原料 |
| [tubecon/square_-_75.0_x_75.0_x_5.00.step](tubecon/square_-_75.0_x_75.0_x_5.00.step) | Square - 75.0 X 75.0 X 5.00.step | 方管、未开孔原料 |
| [wcp/WCP-0910.step](wcp/WCP-0910.step) | 0.196in ID x 0.375in OD - 36in Length | 圆管、圆孔 |
| [wcp/WCP-0913.step](wcp/WCP-0913.step) | 0.196in ID x 0.500in OD - 36in Length | 圆管、圆孔 |
| [wcp/WCP-1142.step](wcp/WCP-1142.step) | 0.500in Hex ID x 0.75in OD - 36in Length | 圆管、内六角 |
| [wcp/WCP-1269.step](wcp/WCP-1269.step) | 0.625in ID x 0.75in OD - 47in Length | 圆管、圆孔 |
