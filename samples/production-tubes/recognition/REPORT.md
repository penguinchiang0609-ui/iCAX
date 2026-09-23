# 47 个真实管材样件：轴向、轮廓和 fitting 验证

2026-09-23：47/47 轴向正确，47/47 轮廓通过检查；34 个匹配成功（27 个方/矩形管、7 个圆管），13 个特殊截面未匹配。

测试直接调用产品使用的 ReadBRepFile、RecognizeDirection、ExtractSectionWires、RecognizePythonFitters。使用 21 个直接反解模板，目录读取和反解时启用禁止正向生成的 guard。所有输入仍为厂商原始 STEP。

[打开轮廓对比与全部截面图](contours.html) · [逐项结果 CSV](summary.csv) · [结构化结果](summary.json)

## 发现并修复

- 密集孔阵列的短横向边/圆柱面投票压过纵向边，7 个 MAX Pattern 管件把 25.4 mm 壁间方向当成管轴。采用一致的跨度平方权重后，7 个全部恢复正确纵向；原有模板和回归测试均通过。
- 部分 STEP 样条投影后控制点共线，仍以样条参与图构建，未与重合直线合并。对控制点共线且单调、权重为正的曲线统一直线表示，再执行原有合边、端点拆边、合点流程，恢复带槽管完整外轮廓。非线性样条保持原始曲线。
- 方管先被通用多边形模板匹配，规范化后旋转成菱形。提高方/矩形管模板优先级，9 个方管现在均归入方管模板。
- 圆/椭圆截面快照只统计采样点，包络尺寸偏小；改为在规范化坐标中补算解析极值。

原生回归 32/32、产品接口回归 2/2 全部通过。原生回归含全部 21 个已实现模板，以及新增真实零件和投影样条回归。

## 判定方法与限制

- 47 个原始 CAD 均沿全局坐标轴放置，且目录长度与最长方向一致；据此核对轴向，允许轴正负等价。长度和截面包络与独立 OCCT 导入量测对照，容差 0.001 mm，方管宽高允许互换。
- 核对每个闭环的相邻端点连续、外环/内环方向、最低位置首边、截面尺寸，以及成功匹配是否返回参数；未匹配必须保持空参数。
- 12 个 REV 带导向槽/复杂内腔截面和 1 个 WCP 外圆内六角截面仍未匹配现有模板。其轴向及完整轮廓已获得，未强制套用普通方管或圆管。
- “47 个轮廓检查通过”不代表全部管型已被支持，也不代表已验证所有加工路径。尺寸以 STEP 实际几何为准，目录中的四舍五入标称值不覆盖原始 CAD 尺寸。

## 逐项结果

| 文件 | 轴向 | 闭环数 | 长度 mm | 管型匹配 | 总耗时 s |
|---|---|---:|---:|---|---:|
| [andymark/am-2200.step](verified/andymark__am-2200.step.json) | X ✓ | 2 | 508.000 | 方/矩形管 | 6.00 |
| [andymark/am-5001-4700.step](verified/andymark__am-5001-4700.step.json) | Z ✓ | 2 | 1193.800 | 方/矩形管 | 12.10 |
| [andymark/am-5177.step](verified/andymark__am-5177.step.json) | Z ✓ | 2 | 1193.800 | 方/矩形管 | 12.06 |
| [andymark/am-5178.step](verified/andymark__am-5178.step.json) | Z ✓ | 2 | 1193.800 | 方/矩形管 | 11.65 |
| [andymark/am-5179.step](verified/andymark__am-5179.step.json) | Z ✓ | 2 | 1193.800 | 方/矩形管 | 31.06 |
| [andymark/am-5180.step](verified/andymark__am-5180.step.json) | Z ✓ | 2 | 1193.800 | 方/矩形管 | 31.07 |
| [gobilda/4100-1214-0300.step](verified/gobilda__4100-1214-0300.step.json) | Y ✓ | 2 | 300.000 | 圆管 | 0.18 |
| [rev/REV-21-2160.step](verified/rev__REV-21-2160.step.json) | Z ✓ | 6 | 1193.800 | 未匹配特殊截面 | 16.95 |
| [rev/REV-21-2161.step](verified/rev__REV-21-2161.step.json) | Z ✓ | 6 | 1193.800 | 未匹配特殊截面 | 9.74 |
| [rev/REV-21-2162.step](verified/rev__REV-21-2162.step.json) | Z ✓ | 6 | 1193.800 | 未匹配特殊截面 | 10.30 |
| [rev/REV-21-2163.step](verified/rev__REV-21-2163.step.json) | Z ✓ | 6 | 76.200 | 未匹配特殊截面 | 3.34 |
| [rev/REV-21-2164.step](verified/rev__REV-21-2164.step.json) | Z ✓ | 6 | 127.000 | 未匹配特殊截面 | 4.62 |
| [rev/REV-21-2165.step](verified/rev__REV-21-2165.step.json) | Z ✓ | 6 | 177.800 | 未匹配特殊截面 | 6.35 |
| [rev/REV-21-2169.step](verified/rev__REV-21-2169.step.json) | Z ✓ | 6 | 381.000 | 未匹配特殊截面 | 12.54 |
| [rev/REV-21-2173.step](verified/rev__REV-21-2173.step.json) | Z ✓ | 6 | 584.200 | 未匹配特殊截面 | 18.59 |
| [rev/REV-21-2177.step](verified/rev__REV-21-2177.step.json) | Z ✓ | 6 | 787.400 | 未匹配特殊截面 | 24.37 |
| [rev/REV-21-2185.step](verified/rev__REV-21-2185.step.json) | Z ✓ | 6 | 1193.800 | 未匹配特殊截面 | 36.52 |
| [rev/REV-21-2289.step](verified/rev__REV-21-2289.step.json) | Z ✓ | 6 | 1193.800 | 未匹配特殊截面 | 46.23 |
| [rev/REV-21-2590.step](verified/rev__REV-21-2590.step.json) | Z ✓ | 6 | 1193.800 | 未匹配特殊截面 | 40.84 |
| [rev/REV-21-3540.step](verified/rev__REV-21-3540.step.json) | Z ✓ | 2 | 1193.800 | 方/矩形管 | 11.75 |
| [rev/REV-21-3543.step](verified/rev__REV-21-3543.step.json) | Z ✓ | 2 | 1193.800 | 方/矩形管 | 11.69 |
| [rev/REV-21-3552.step](verified/rev__REV-21-3552.step.json) | Z ✓ | 2 | 1193.800 | 方/矩形管 | 31.18 |
| [rev/REV-21-3555.step](verified/rev__REV-21-3555.step.json) | Z ✓ | 2 | 1193.800 | 方/矩形管 | 31.57 |
| [rev/REV-21-3558.step](verified/rev__REV-21-3558.step.json) | Z ✓ | 2 | 76.200 | 方/矩形管 | 1.42 |
| [rev/REV-21-3559.step](verified/rev__REV-21-3559.step.json) | Z ✓ | 2 | 127.000 | 方/矩形管 | 2.50 |
| [rev/REV-21-3560.step](verified/rev__REV-21-3560.step.json) | Z ✓ | 2 | 177.800 | 方/矩形管 | 3.70 |
| [rev/REV-21-3564.step](verified/rev__REV-21-3564.step.json) | Z ✓ | 2 | 381.000 | 方/矩形管 | 8.44 |
| [rev/REV-21-3568.step](verified/rev__REV-21-3568.step.json) | Z ✓ | 2 | 584.200 | 方/矩形管 | 13.72 |
| [rev/REV-21-3572.step](verified/rev__REV-21-3572.step.json) | Z ✓ | 2 | 787.400 | 方/矩形管 | 19.31 |
| [rev/REV-21-3580.step](verified/rev__REV-21-3580.step.json) | Z ✓ | 2 | 1193.800 | 方/矩形管 | 31.69 |
| [rev/REV-21-3586.step](verified/rev__REV-21-3586.step.json) | Z ✓ | 2 | 1193.800 | 方/矩形管 | 31.00 |
| [tubecon/rectangle_-_100.0_x_25.0_x_1.60.step](verified/tubecon__rectangle_-_100.0_x_25.0_x_1.60.step.json) | Z ✓ | 2 | 1000.000 | 方/矩形管 | 0.30 |
| [tubecon/rectangle_-_100.0_x_25.0_x_3.00.step](verified/tubecon__rectangle_-_100.0_x_25.0_x_3.00.step.json) | Z ✓ | 2 | 1000.000 | 方/矩形管 | 0.29 |
| [tubecon/rectangle_-_100.0_x_50.0_x_1.60.step](verified/tubecon__rectangle_-_100.0_x_50.0_x_1.60.step.json) | Z ✓ | 2 | 1000.000 | 方/矩形管 | 0.30 |
| [tubecon/rectangle_-_80.0_x_40.0_x_4.50.step](verified/tubecon__rectangle_-_80.0_x_40.0_x_4.50.step.json) | Z ✓ | 2 | 1000.000 | 方/矩形管 | 0.32 |
| [tubecon/rectangle_-_80.0_x_60.0_x_1.60.step](verified/tubecon__rectangle_-_80.0_x_60.0_x_1.60.step.json) | Z ✓ | 2 | 1000.000 | 方/矩形管 | 0.28 |
| [tubecon/round_-_34.1_x_2.50.step](verified/tubecon__round_-_34.1_x_2.50.step.json) | Z ✓ | 2 | 1000.000 | 圆管 | 0.20 |
| [tubecon/round_-_34.9_x_1.20.step](verified/tubecon__round_-_34.9_x_1.20.step.json) | Z ✓ | 2 | 1000.000 | 圆管 | 0.22 |
| [tubecon/round_-_38.1_x_0.90.step](verified/tubecon__round_-_38.1_x_0.90.step.json) | Z ✓ | 2 | 1000.000 | 圆管 | 0.22 |
| [tubecon/square_-_100.0_x_100.0_x_2.50.step](verified/tubecon__square_-_100.0_x_100.0_x_2.50.step.json) | Z ✓ | 2 | 1000.000 | 方/矩形管 | 0.27 |
| [tubecon/square_-_100.0_x_100.0_x_6.00.step](verified/tubecon__square_-_100.0_x_100.0_x_6.00.step.json) | Z ✓ | 2 | 1000.000 | 方/矩形管 | 0.30 |
| [tubecon/square_-_70.0_x_70.0_x_2.00.step](verified/tubecon__square_-_70.0_x_70.0_x_2.00.step.json) | Z ✓ | 2 | 1000.000 | 方/矩形管 | 0.34 |
| [tubecon/square_-_75.0_x_75.0_x_5.00.step](verified/tubecon__square_-_75.0_x_75.0_x_5.00.step.json) | Z ✓ | 2 | 1000.000 | 方/矩形管 | 0.30 |
| [wcp/WCP-0910.step](verified/wcp__WCP-0910.step.json) | X ✓ | 2 | 914.400 | 圆管 | 0.17 |
| [wcp/WCP-0913.step](verified/wcp__WCP-0913.step.json) | X ✓ | 2 | 914.400 | 圆管 | 0.19 |
| [wcp/WCP-1142.step](verified/wcp__WCP-1142.step.json) | X ✓ | 2 | 914.400 | 未匹配特殊截面 | 0.65 |
| [wcp/WCP-1269.step](verified/wcp__WCP-1269.step.json) | X ✓ | 2 | 1193.800 | 圆管 | 0.19 |

## 部署

已将验证通过的 ExtrusionRecognition 模块、原生测试程序和方管优先级描述符部署到 `src/x64/Debug`。部署后 4 项原生重点测试和 2 项产品接口测试通过；部署文件哈希与测试产物一致。详见 [deployment.json](deployment.json)。
