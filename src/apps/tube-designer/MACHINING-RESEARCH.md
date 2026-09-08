# 加工区：竞品调研与下一阶段边界

整理日期：2026-09-08。以下依据厂商公开材料，不代表已实际操作竞品，也不推断其内部几何算法。当前实施范围是刀路解析和独立编辑；本页作为后续工艺交互设计输入。

| 厂商 | 官方材料可确认的能力 | 对本项目下一阶段的启示（设计判断） |
| --- | --- | --- |
| 维宏 VHTube | 文档涉及空间引线、补偿、微连、切割头模型和仿真 | 引线需要独立三维几何及姿态；切割头应有安装坐标和碰撞体，不能只是装饰模型。 |
| 柏楚 TubesT | 普通引线随表面保持间隙；板外引线按外侧定高处理；补偿区分随动/定高；微连有间距、数量、位置和避角等条件 | 用户所说“贴面 / 空间”应作为两类明确的几何约束；微连需保存弧长区间，不能仅隐藏一段显示线。 |
| Lantek Flex3d Tubes | 自动/手动引线与微连，二维/三维加工界面，切割头交互仿真及机床相关输出 | 把轮廓编辑、工艺生成、机床运动求解分层；二维是同一三维刀路的编辑投影，不另建一份二维数据。 |
| Alma Almacam Tube / Space Cut | 特征识别、轨迹生成、机床运动学与仿真；Space Cut 文档涉及引线位置、类型、尺寸及轮廓/程序组织 | 按“排样实体—独立轮廓—工艺段—执行顺序”组织数据；工艺层不能直接修改或绑定原 CAD 拓扑。 |

资料来源：

- [维宏 VHTube 官方说明](https://www.weihong.com.cn/en/profile/20251015/VHTube%E5%8F%98%E6%88%AA%E9%9D%A2%E5%9E%8B%E9%92%A2%E8%BD%AF%E4%BB%B6%E8%AF%B4%E6%98%8E_1760529668613.pdf)
- [柏楚：引线](https://www.bochu.com/tubest_help/tubest-%E5%BC%95%E7%BA%BF/)、[补偿](https://www.bochu.com/tubest_help/tubest-%E8%A1%A5%E5%81%BF/)、[微连](https://www.bochu.com/tubest_help/tubest-%E5%BE%AE%E8%BF%9E/)、[手动排序](https://www.bochu.com/tubest_help/tubest-%E6%89%8B%E5%8A%A8%E6%8E%92%E5%BA%8F/)
- [Lantek 官方产品资料](https://campaign.lantek.com/hubfs/Documents/Catalogo%202020/Company_Dossier_Lantek_EN_full.pdf)
- [Alma 管材加工说明](https://almacam.com/software/3d-cam/faq-tube-profile-cutting-cad-cam-nesting-software/)、[Space Cut 官方资料](https://almacam.com/wp-content/uploads/2020/07/Almacam-spacecut-en_web.pdf)

## 待开展的交互设计

1. 贴面引线：在承载表面/局部坐标系中定义形状、长度、方向和间隙；空间引线：独立三维曲线与进刀姿态约束，不能简单理解为二维轮廓的延长线。
2. 微连：沿轮廓记录留料区间及长度，显示实际开/关切割段；考虑起点、转角、孔和端部的避让。
3. 刀补：保留目标轮廓，以有效刀具半径及三维接触/刀姿约束计算实际轨迹；不是向某个固定全局方向平移线条。需独立处理凹角、自交、过切和不可达情况。
4. 排序：先按母材、零件和轮廓组织，再允许调整起点、方向和顺序；由机床约束校验，不仅按列表索引输出。
5. 切割头：模型资源、刀尖基准、安装变换、运动自由度和碰撞体分开保存。

以上是后续设计建议，本阶段未实现这些工艺行为。先验收 BRep → CamPath 与独立编辑，再确定工艺界面。
