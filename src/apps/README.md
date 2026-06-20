# apps

`apps` 放置具体业务应用。每个应用都是一组完整业务能力，通常同时包含 icax 侧 C++ 扩展、前端 UI 或宿主代码，以及应用级协议。

## 目录结构

- `sample/`：示例应用，用于验证应用结构和插件接入方式。
- `cax-host/`：当前 CAXHost 宿主应用。
- `mfc-sample/`：当前 MFC 示例应用。

## 应用目录约定

每个应用建议包含：

- `backend/`：该应用的 icax 侧 C++ 扩展。
- `frontend/`：该应用的前端 UI、面板和组件；具体技术可以是 Qt、WPF、H5 或其他宿主形态。
- `protocol/`：该应用自己的 Mailbox/PDO 协议。

当前前端策略采用 H5。通用工作台壳放在 `src/icax-workbench/`，具体应用的 `frontend/` 提供产品模块入口、产品工作区贡献和项目工作区贡献。

应用 H5 前端建议包含：

- `entry`：产品前端入口，对应 backend `ProductDefinition.FrontendEntry`。
- `pages/`：产品级页面。
- `panels/`：可挂载到工作台的产品或项目面板。
- `components/`：应用内部复用组件。
- `commands/`：前端命令定义和快捷入口。
- `project/`：项目主编辑视图和项目级贡献。
