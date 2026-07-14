# laser-3d-cam

`laser-3d-cam` 是三维线条切割 CAM 产品目录。

它只描述具体产品，不向 `iCAX-Engine` 的 framework 写入产品逻辑。产品 backend 扩展位于 `src/iCAX-Plugins/cam/Laser3DCAM`，项目前端位于本目录的 `webpage`。

目录结构：

- `product.manifest.json`：产品定义、项目文件 magic、backend 插件 DLL、H5 入口。
- `webpage/`：产品自己的 H5 页面模块。

产品大区：

- `机床定义`：准备机床定义资源。当前主路径是导入 SDF/XML，生成 `MachineDescriptionResource`，并在主加工场景实例化一台机床。
- `工件编辑`：准备工件 BRep 资源。当前主路径是导入 STEP/IGS，后续模型检查和 CAD 修复会打开独立编辑 Scene，提交后生成新的 BRep 资源。
- `加工`：正式 CAM 主场景。这里摆放机床实例和工件实例，拾取 edge/loop 生成刀路，组织块与指令，进行排序、轨迹规划、碰撞检测、仿真和导出。

运行边界：

- backend 产品能力来自 `src/iCAX-Plugins/cam/Laser3DCAM`。
- 项目级参数保存在 ProjectSetting 中。
- 正式 CAM 数据保存在主 Scene 的 Database 中，包括机床实例、工件实例、刀路、块、作业和仿真状态。
- 机床定义、BRep、曲线、显示网格、拾取代理等大对象或运行期对象保存在主 Scene 的 Resources 中。
- 机床定义编辑、工件 CAD 修复等复杂资源编辑能力应使用独立 EditScene；提交时只向主 Scene 写入 `ResourcePut + 引用更新`。
- webpage 通过前端 SDK 的 ProductProxy/ProjectProxy/SceneProxy 建立上下文，并通过 SceneProxy 发送项目数据命令，不直接持有业务数据。
- manifest 中 backend 模块路径使用 `${Platform}` 和 `${Configuration}` 占位符，由 Application 加载产品定义时解析。
