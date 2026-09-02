# TubeOne STEP 体验样件

这些样件由 OpenCascade 直接构造并导出，用于体验 TubeOne 的 BRep → 中性 CSG 识别。

| 文件 | 主要观察点 |
| --- | --- |
| `01_round_tube_plain.step` | 标准圆管，单内腔、稳定圆周 Side Atlas |
| `02_double_cavity_rectangular_tube.step` | 双腔矩形管，截面包含两个独立内孔 |
| `03_round_tube_through_hole.step` | 圆管上的垂直支管贯切 |
| `04_round_tube_oblique_hole.step` | 圆管上的斜贯切 |
| `05_double_cavity_blind_hole.step` | 从外壁开口、终止于材料内部的盲孔 |
| `06_opened_chamber_internal_wall_hole.step` | 一个腔体外壁被打开后，对腔间壁继续加工 |
| `07_round_tube_stepped_uv_end.step` | 圆管端部阶梯式 UV 法向截断 |
| `08_double_cavity_beveled_hole.step` | 直孔与局部锥形坡口的组合 |
| `09_double_cavity_mitered_ends.step` | 两端不同方向的斜截断 |

建议先体验 `03`、`07` 和 `08`。导入后点击“识别 CAD 意图”，再从左侧 CSG 树选择减材节点查看参数。
