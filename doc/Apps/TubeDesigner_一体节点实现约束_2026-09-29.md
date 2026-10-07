# TubeDesigner 一体节点：真实 2→1 下料的实现约束

## 当前状态

`node-v-notch-integrated` 和 `node-edge-arc-integrated` 已是两份独立的装配工艺模板。它们的示例均声明“两段逻辑管件 → 一根连续母材”，但 `productBinding.mode` 明确为 `unsupported`：目前只能看示例，不能将真实产品的两根下料件归并。现有 `bend` 也是示例，不能代替这两个节点模板。分件装配的真实绑定只支持在原有下料件上追加切削；界面上的模板选择或示例预览不能作为归并已经生效的证据。

示例现可切换“折弯后示意 / 下料件”：前者用单个连续空心网格表达绕保留底壁槽根折弯后的外观，不再摆放两根相交的完整直管；后者仍调用原生单件加工预览，显示实际开槽的直管母材。折弯后网格是理想外观示意，不是由槽后成形求得的 BRep，也不进入产品归并、制造清单或导出。

生成器基础能力允许显示输出和制造输出采用不同的构件键，且制造件数取自制造输出；`single_face_security_window` 已能生成一根带 V 槽的连续框母材。但该框在显示输出中也是**一个**构件，尚不构成“节点 A/B 两个可选择的逻辑管段共享一根下料件”的产品绑定闭环。正式产品模板目前没有可直接复用的这种 2→1 节点实例。

## 首个真实验收节点

选 `minimal-protective-grille` 的四边封框，设置 `frameType=closed_frame`、`barLayoutMode=fixed_count`、`barCount=0`、`handleEnabled=false`、`installHoleEnabled=false`，用已提交关系 `frame.corner.0001` 的左边框 `frame.left.0001` 与下边框 `frame.bottom.0001` 作首例。已用产品生成脚本确认该配置的制造输出恰为左、右、下、上四根边框，且存在四条 `frame.corner.*` 关系。先实现节点 V 槽；边弧槽复用相同的归并合同，但保持独立工艺模板与独立刀具几何。

验收应同时满足以下结果：

1. 应用前是四个独立物理下料件；应用后仍有四个可辨认的逻辑显示构件，但制造清单为**三件**，其中左边框与下边框指向同一根连续母材。不得将已切端的两个实体做布尔并集来冒充一根母材。
2. 该母材有从真实截面和节点尺寸计算的展开长度、明确的槽站与转管方向；原生制造 BRep 确实含一个 V 槽切口，另一端原有加工不丢失。装配角度、槽根避空与 K 补偿仍各守原有语义；V 槽圆角不能改成相切展开槽。
3. `PreviewProductAssemblyBinding` 给出真实截面、尺寸、槽位与材料校验及预计件数变化；`ApplyProductAssemblyBinding` 后下料视图、制造清单、排样引用与 STEP 导出均以三件为准。保存 `.ictd` 后重开、重新拆单仍得到同一三件与同一槽位；移除绑定恢复四件。
4. 产品原装配姿态与归一化下料件分别显示；若尚未求出折弯后实体及逆姿态映射，不把两根直管的产品视图标为已验证的成形实体。焦点、选区与左右侧栏滚动须在异步预览和应用后保持。

## 最小必须修改的合同

| 层 | 当前阻断 | 必须具备的结果 |
| --- | --- | --- |
| 装配模板运行时 `templates/_shared/assembly_template_runtime.py` | `productBinding` 只接受 `unsupported` / `incremental-cut`；后者强制 `realization=separate`，解析结果只能逐构件追加刀具。 | 增加独立的 `integrated-merge` 绑定模式，声明两个真实角色、唯一连续毛坯、截面兼容与节点锚点、槽具/参数、角色到毛坯的映射及预期物理件数。不能把示例长度/姿态当真实值。 |
| 原生预览/提交 `ProductAssemblyBindings.inc` | 预览要求每个角色都有一对一现成下料件；提交只冻结原有零件上的增量切削。 | 从已提交产品关系与真实构件求连续母材长度、槽站/朝向，校验原有端切及其他节点加工，生成并冻结可重放的归并配方和槽具；预览候选仍用产品版本、绑定修订和过期校验保护。 |
| 拆单与几何 `TubeDesignerSDO.cpp` / `ProductAssemblyBindingGeometry.inc` | 按制造输出 itemKey 一项造一件，要求其数量等于 `Run.PartCount`；`ApplyFrozenAssemblyBindings` 只在同一件上减刀。 | 制造输出/绑定配方须实际生成一根连续坯并替代节点两件；区分生成器基准件数与绑定后的物理件数，重开时从持久化配方重建同一 BRep，移除时可逆。不可仅修改内存中的首次拆单结果。 |
| 身份与消费者 `TubeDesignerComponents.h`、`ProductAssemblyConnections.inc`、快照/排样/导出 | 制造件只有一个 `SourceMemberID`，连接查询按 display itemKey 与制造 itemKey 一对一认领。 | 建立两个逻辑成员到同一 `partId` 的持久映射（保留旧单来源字段的兼容性），让连接查询、构件选中、数量、排样、文件名与 STEP 都认同三件物理结果。 |
| 前端 `webpage/productAssemblyBindings.mjs`、`assemblyLibrary.mjs`、`assemblyProductScene.mjs` | 非 `incremental-cut` 模板不可应用；成品与下料分开展示。 | 仅在原生能力和真实节点几何通过时开放 V 槽/边弧槽；显示“4 逻辑构件 / 3 下料件”、共享映射、槽位与待核对项，并用现有局部更新及交互状态保护机制刷新。 |

原生回归至少覆盖：4→3→4 的应用/移除、旧候选失效、已有另一节点工艺冲突、项目重开与资源恢复、排样和 STEP 件数、旧增量切削绑定不回归。浏览器回归覆盖节点选择、条件参数变化、异步响应期间继续滚动、两侧焦点/选区以及画布和事件监听器未替换。只有 Python 示例和前端文字通过，不能算真实归并验收。
