# laser-3d-cam

`laser-3d-cam` 是三维线条切割 CAM 产品目录。

它只描述具体产品，不向 `iCAX-Engine` 的 framework 写入产品逻辑。产品 backend 扩展位于 `src/iCAX-Plugins/cam/TopologyToolpath`，项目前端位于本目录的 `webpage`。

目录结构：

- `product.manifest.json`：产品定义、项目文件 magic、backend 插件 DLL、H5 入口。
- `webpage/`：产品自己的 H5 页面模块。

运行边界：

- backend 产品能力来自 `src/iCAX-Plugins/cam/TopologyToolpath`。
- 项目状态保存在 Project.Database 中。
- 模型、拓扑、显示等大对象或运行期对象保存在 Project.Resources 中。
- webpage 只通过前端 SDK 的 ProductProxy/ProjectProxy 发送命令，不直接持有业务数据。
- manifest 中 backend 模块路径使用 `${Platform}` 和 `${Configuration}` 占位符，由 Application 加载产品定义时解析。
