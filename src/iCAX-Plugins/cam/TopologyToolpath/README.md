# TopologyToolpath

`TopologyToolpath` 是三维线条切割 CAM 的首个产品插件。它不保存插件私有项目状态，而是向 Database 注册 CAM 组件，并向 CommandHandler 注册 `Cam.*` 项目命令。

当前插件只落地产品数据边界：

- 项目状态、当前选择和刀路目标列表进入 Database。
- 导入模型、拓扑索引和显示数据的对象进入 Project.Resources。
- 前端只能通过 `Cam.*` 命令读取和修改项目状态。
- 插件不在 framework 中写入任何产品专属逻辑。

当前没有内置 STEP/OCC 解析器，也不再硬编码示例孔、边或面。`Cam.ImportModel` 只登记模型资源和空拓扑资源；后续模型导入/特征识别插件负责填充 `CCAMTopologyResource`。

目录结构：

- `TopologyToolpathComponents.h`：CAM 模型引用、当前选择、刀路列表组件。
- `TopologyToolpathResources.h`：CAM 模型资源和拓扑资源的数据契约。
- `CamCommands.cpp`：项目级 `Cam` 命令目标，负责模型导入登记、拓扑拾取、识别结果入刀路列表、维护刀路目标。
- `TopologyToolpath.h/.cpp`：插件契约版本入口。
- `TopologyToolpath.vcxproj`：插件 DLL 工程。

命令：

- `Cam.GetScene`：返回项目当前模型、拓扑状态、选择和刀路目标列表。
- `Cam.ImportModel`：登记模型来源，创建模型资源、拓扑资源和显示资源引用。
- `Cam.PickTopology`：选择一个已存在的拓扑对象。
- `Cam.RecognizeLoops`：从拓扑资源中筛选 `role=hole/cut` 的 loop，加入刀路目标列表。
- `Cam.AddSelectionToToolpathList`：把当前选择加入刀路目标列表。
- `Cam.ClearToolpathList`：清空刀路目标列表。
