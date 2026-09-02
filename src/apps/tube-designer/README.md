# TubeDesigner

TubeDesigner 是 iCAX 上的管材产品参数化设计与制造拆单产品。

它负责回答“产品是什么、由哪些装配构件组成、最终拆成哪些制造零件”；TubeOne 负责回答“制造零件如何排样、编程和加工”。两者共享 iCAX 的 Project、Scene、Entity-Component、Resource、View 和 OpenCascade 能力，但保持独立产品边界。

## P0 范围

- 产品 ID：`icax.tube-designer`
- 工程扩展名：`.tubedesigner`
- 模板：`security-window-1@1.0.0`
- 截面：矩形空心管
- 输入：界面手工参数
- 输出：装配预览、制造零件表、显式连接意图、逐件 STEP
- 默认案例：两根外框、一根中间竖管、四根横管，共七个制造零件

Excel 批量订单、防盗窗 2/3 号、圆管、安装孔、吊装孔、公母口、V 槽折弯和直接送入 TubeOne 属于后续阶段。

## 领域边界

- `CProductInstanceComponent`：模板实例与参数快照。
- `CAssemblyMemberComponent`：装配语义、位置和预览几何。
- `CManufacturingPartComponent`：实际下料对象与制造几何。
- `CJointIntentComponent`：目标构件、插入构件、连接模式和间隙。
- `CGenerationRunComponent`：一次可追踪的模板求值结果。

装配构件和制造零件使用不同实体与不同几何资源，避免在 V 槽折弯、拼焊、合并下料等场景中错误假设两者始终一一对应。
