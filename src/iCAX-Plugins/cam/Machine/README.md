# Machine

公共机床组件、SDF/URDF 描述加载、运动约束和碰撞行为模块。源文件与头文件均位于本目录，由 Machine 工程独立编译。CamRuntime 通过 Machine 的接口消费这些能力，不包含机床实现。产品级机床目录与场景实例操作由业务 SDO 管理。

URDF 的显式端点声明：在所属 `link` 内用 `<icax_tool tcp_mm="0 0 -220" beam="0 0 -1" />` 描述物理喷嘴与光束，用 `<icax_work_support />` 标记装夹工作台；不按部件名称推断业务角色。关节内可用 `<icax_home position="0.15" />` 声明初始装料位置，单位沿 URDF 关节（直线 m、旋转 rad），实例化后换算成 mm/rad 的 `HomePosition`。初始装料位不等同于数控坐标零位，也不表示实机已标定。

FiveAxisCAM 的 `CamSimulation.MountMachine` 用真实工件与启用夹具的网格组合外形居中放到声明工作台的水平碰撞盒顶面，保留各零件原有局部姿态；超出台面或装夹后当前机床姿态碰撞时拒绝提交。切割离板高度由 CAM 的 `followHeightMm` 单独作用于目标喷嘴位置，不能通过修改物理 TCP 代替。
