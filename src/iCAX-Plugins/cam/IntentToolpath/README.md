# IntentToolpath

通用意图刀路领域插件，供平面、管材和三维线切割产品复用。

## 公共设计与实现状态

跨产品的目标契约见[通用可编辑刀路组件设计](../../../../doc/iCAX-Plugins/cam/IntentToolpath/README.md)，包括[规格](../../../../doc/iCAX-Plugins/cam/IntentToolpath/IntentToolpath规格文档.md)和[技术方案](../../../../doc/iCAX-Plugins/cam/IntentToolpath/IntentToolpath方案文档.md)。绘制、打断、合并和工艺生成的结果统一为可继续编辑的刀路；组织树、操作依赖、替代和加工约束分别管理。

当前公共入口是 [`EditableToolpath.h`](EditableToolpath.h)。`CEditableToolpathDocument` 用工程数据库保存文档、方案、源定义、操作输入/参数、端口与刀路身份、精确结果、映射、排除、组织和命令回执。`Resolve` 同时返回有效输出、加工约束及 `Complete / Incomplete / Invalid`，不会把失败分支过滤后当成完整结果。

基础模型、异步身份、原生事务、保存撤销和公共接入须按同一完整契约一次交付并验收，不以缺功能的中间模型或同步降级路线接管现有产品。

2026-10-26 补充的图层及场景右侧图层栏仍为目标契约：初始 8 个显示层、选中刀路点色块换层、自定义增删/配色、方案内归属与继承/覆盖尚未由当前公共 API 完整承载。`CamRuntime/LayerComponents.h` 的历史层组件和产品工艺中的 `layerKey` 不等于这套功能已接入；须按[规格 P18–P22](../../../../doc/iCAX-Plugins/cam/IntentToolpath/IntentToolpath规格文档.md#13-跨产品验收)补齐原生与真实界面验收。

管材中性模型编译已移到独立的 [`TubeIntentAdapter`](../TubeIntentAdapter/README.md)，通过 `TubeIntentAdapter/TubeMachiningIntent.h` 使用。核心和独立编辑测试不加载管材、机床、产品、UI 或 OpenCascade。

## 当前能力

插件负责：

- 平面、空间和 UV 权威曲线，使用 `GeometryData` 精确值，不将显示折线作为计算权威。
- 统一操作注册、输入槽、Replace/ReadOnly、完整覆盖校验、稳定输出角色和来源区间映射。
- 反向、改起点、打断、分段、合并、显式桥接、平移、精确几何替换、通道编辑、复制，以及微联和直引线操作。
- 持久删除/恢复、独立加工方案、共享来源、分组、加工块与先后约束。
- 冻结输入的后台计算、独立分支并行、实际可见数据的指纹、旧任务拒绝、原子派生发布和原生撤销。
- 精确保存与导入预检、缺插件/后端诊断、命令幂等、小摘要和带一致性指纹的分页。

## 调用约束

Repository 的访问、命令和 `Publish` 在文档所属线程执行；工作线程只读冻结输入。`Execute` 保存定义，`GenerateAsync` 计算，所属线程在 `IsReady` 后调用 `Publish`。丢弃取消任务不会等待算法退出；关闭文档使旧会话不能发布。

新增操作通过 `CToolpathOperationRegistry::Register`，提供参数 schema、规范化/校验、读写通道、求值及结果校验。算法可读取完整精确几何，所以其几何、拓扑、方向、来源和坐标一起进入读取指纹；未声明的扩展通道不会传给求值器。未写通道按精确来源映射传播，冲突须显式解决。

`SOperationType.EvaluationRevision` 标识当前求值实现，独立于可编辑定义的 `SchemaVersion/AlgorithmVersion`。缓存复用、当前结果解析及异步发布均校验该版本；修正实现后递增求值版本，保留原定义的编辑能力。Current 记录必须携带非空求值版本，缺失时不通过导入预检；不提供旧缓存迁移或默认版本回退。

常用命令包括 `CreateSource`、`SetPlanRoots`、`AddOperation`、`UpdateOperation`、`UpdateSource`、`Rebind`、`DeletePaths`、`RestoreOutputs`、`SetParticipation`、`RemoveOperation`、`SetOperationEnabled`、`SetLabels`、`Group`、`Ungroup`、`SetExecutionConstraints`、`CopyPaths` 和 `Detach`。修改操作须携带已观察的定义指纹，选择当前输出须携带其输入指纹；`SetPlanRoots` 需携带方案指纹。

默认几何服务支持其明确实现的解析几何和可用的中性后端。UV 引线必须注册曲面算法；直引线需要明确的废料半空间并校验实际端点。导入允许无损保留缺少求值器的精确值，解析加工输出时仍标为不完整。宿主可向文档及导入接口注入 `IToolpathGeometryServices`。

`SetPathLocked {pathIds, locked}` 保存路径自身锁，与全部祖先组锁共同生效；组的停加工状态不禁止编辑。上游变更检查所有受影响方案的下游锁。`InspectPlan` 保留 Current、Stale、Pending 与显式 Deleted/Disabled，分别返回自身与有效状态；`DisplayConstraints` 使用加工排序的共同端口展开器并包含停加工路径。`ReadPath(planId, port)` 可读取已消费的中间端口，仍要求上游结果为 Current；`CopyPaths/Detach` 拒绝旧缓存和待生成值。

`ReadPaths(planId, ports)` 在一次方案快照和完整有效性校验后读取多个端口，按输入顺序返回并保留重复项。每个端口的标签、组织、参与与锁定状态与 `ReadPath` 一致；任一端口过期、缺失或不属于方案时整批拒绝。`ReadPath` 委托该公共批量入口，不直接读取未校验结果。

`icax.edit.split` 的 `retainFirstIdentity=true` 在创建时保存原起点，仅让包含该物理端点的片段继承身份与编号。`AddOperation.insertBefore {operationId,inputSlot,expectedOperationFingerprint}` 在同一命令中插入固定单 main 节点并重绑消费者。

`ClonePlan {planId,sourcePlanId,expectedSourcePlanFingerprint,labelScope?}` 向空方案复制完整可编辑图，证书取 `ReadPlan.graphFingerprint`，返回 `operationIds/pathIds/portRemap`。`HostEdit.PlanClones[commandId]` 提供类型化来源/上下文变换及 `ExternalIdentityRemap`，输入证据和实际输出进入幂等校验。复制保存来源定义与身份种子，重新生成后才有 Current 成品；`ReframeOnly` 通过精确几何不变校验后才可更新匹配参数证书。`HostEdit.RecordUndo=false` 通过框架 `EndWithoutHistory` 完成初始化基线，事务失败仍整体回滚，成功不增加或清空用户撤销历史。

标准反向操作对材料侧采用明确的 `material` 通道：`schema="icax.material-side/1"`、`attachment="Path"`、`retainedSide="Left"|"Right"`，以及工件坐标中的单位向量 `referenceNormal`。Left 由 `referenceNormal × 遍历切向` 定义；反向保持法向并交换左右，保留同一物理材料侧。未知材料侧 schema 保留原数据并阻止该反向操作，不猜测插件专用值的含义。

## 验证与接入状态

独立原生宿主位于 [`IntentToolpathTest`](../../../tests/icax-plugins/cam/IntentToolpathTest/)，不加载产品。覆盖二维/空间微联与引线、派生段再次拆合、多轮撤销重做、来源更新、稳定编号、删除/重开、并行复用、发布失败重试、角色匹配、共享方案以及损坏导入。数据库测试另验证派生通知不能重入修改定义、持久化和撤销历史。

FiveAxisCAM 的刀路创建、工艺编辑、分段、复制和执行投影已使用统一操作图，不再保存旧产品刀路组件镜像。历史 `CIntent*` 和 `CollectEffectiveLeafIntentIDs` 接口不属于该操作图的存储或输出解析契约，也不作为旧工程兼容入口。独立宿主当前覆盖 109 项原生测试；产品真实工作流、延迟和界面交互须另外验证，不能由基础测试通过推断整体验收完成。
