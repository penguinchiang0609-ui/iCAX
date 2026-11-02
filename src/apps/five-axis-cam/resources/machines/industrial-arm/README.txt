iCAX IR-1600 六轴激光加工机械臂（SDF 展示模型）

这是本项目自行制作的工业造型展示资产，不是任何品牌的原厂数字样机。
安装 ../requirements-build.txt 后，用 Python 3 运行 build_model.py 生成 SDF 和分材质 STL。
同一部件先求实体并集再按表面材质输出，共 42 份网格、14,384 个三角面。
工作台底座低于台面条，螺栓只生成一次；各分色表面合起来构成无内部重面的闭合外表面。
模型包含锚固底座、六轴机械臂、减速器、电机、法兰、连接螺栓、线缆、装夹台及控制柜。
首次使用时，定义列表已内置此模型。选择该定义后，从机床实例区创建实例。
手动导入 IR-1600.sdf 时保留 meshes 子目录。

运动与碰撞
运动链：锚固底座 -> J1 回转腰座 -> J2 大臂 -> J3 小臂 -> J4 腕部回转 -> J5 腕部俯仰 -> J6 激光加工头。
行程为 ±170、±110、±150、±180、±120、±360 度。
模型长度单位为米，内部显示与运动规划采用 mm，关节角采用弧度。
各部件提供独立 BoxCollider；在实例页可修改尺寸、位置、角度。
装夹部件选择“工件装夹台”，工具选择“J6 激光加工头”。
TCP 局部位置为 [280, 0, 0] mm，光束局部方向为 [1, 0, 0]。
TCP 位于喷嘴物理尖端前方 10 mm，为演示焦点间距；实际使用须按真实工具标定。

SDF 支持范围
支持一个 model、link/joint、fixed/revolute/prismatic 关节、frame、pose relative_to、
joint axis expressed_in，以及 box/sphere/cylinder/mesh 形状和局部视觉/碰撞坐标。
支持六个 Euler 位姿数值及 degrees 属性；关节默认坐标系按 SDF child link 语义处理。
未支持 nested model、include、多模型 world、axis2 和非 Euler rotation_format；明确拒绝而不静默忽略。
坐标系语义参考：https://sdformat.org/tutorials/specification/pose_frame_semantics/1.7/

演示流程
创建实例，设置行程和后置，在作业页选择工件、调整摆放与工具标定，再生成仿真。
后置绑定实例；只有运动求解与碰撞检查通过后才能导出。
程序展示关节运动与激光轨迹，不代表已通过某家机器人控制器或实机加工验收。
