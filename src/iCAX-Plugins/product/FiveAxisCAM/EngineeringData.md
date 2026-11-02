# CAM 工程数据存储

刀路定义、刀轴姿态与关键点、工艺与工艺库快照、识别证据和来源、规划配置、定位报告、切割精确曲线、空移、碰撞与光路报告、机床轴运动计划均属于工程数据，保存在 Scene 数据库的组件属性中。

| 组件 | 权威字段 |
| --- | --- |
| EditableDocument / EditablePlan | 刀路文档、计划、分组、参与与锁定规则 |
| EditableOperation | 原始精确路径及刀轴、补偿、裁剪、引线、分段的操作定义 |
| EditableDerived | 与定义、输入、上下文和求值实现版本一致的可重建精确结果 |
| Setup | PlanningProfileData、GeneratedToolpaths、GeneratedRapidPaths、SimulationCheckData、FixtureBeamCheck、MachineMotionPlanData |
| Placement | RegistrationData |

业务值由 `revision` 和 `value` 组成；revision 是异步响应的身份标记，不是资源句柄。原始曲线和补偿后的精确曲线使用 GeometryData 持久格式保存为组件数据。派生报告和生成结果使用持久、Observable 属性，发布结果不应清空已有撤销重做历史。

每条切割按实际定义、姿态、工艺、曲线与摆放的内容摘要判定有效性；空移按实际两端位置、方向及相关环境判定。更改一条数据不能通过全局资源身份使其他切割和空移全部过期。即使内容发生变化而 revision 未变，摘要也必须检测到变化。

工程直接保存并重开当前统一模型，不提供旧刀路组件、旧业务资源字段或旧空移缓存的迁移与回退。CAD 源文件、BRep、网格、材质和业务值中的真实资产引用通过资源系统保存。

界面状态仅携带数据版本和摘要。显示采样、完整碰撞报告和机床运动帧经 RuntimeOnly 传输资源读取；这些是可重建的传输副本，工程数据库仍是权威来源。禁止把精确曲线、完整大报告重新塞进有容量限制的状态消息。

`FiveAxisEngineeringData.inc` 管理值、内容指纹与曲线编码；普通编辑和生成均通过统一刀路模型读取工程数据。
