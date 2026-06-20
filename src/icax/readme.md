# icax

`icax` 是 C++ 后台平台。它负责对象模型、行为调度、服务、Mail 通信、PDO 高频数据和平台级扩展。

当前后台以 `ApplicationHost -> ProductRuntime -> Project` 三层组织。ApplicationHost 拥有应用级工作线程并轮询应用级、产品级 mailbox；每个 Project 拥有自己的 Repository、ResourceLibrary、Universe、MailChannel 和项目工作线程。

## 目录结构

- `foundation/`：不依赖业务框架的基础类型、数学几何、任务等基础能力。
- `framework/`：实体、组件、行为、应用宿主、服务体系、Mail 通信和 PDO 等后台核心框架。
- `plugins/`：平台级 C++ 扩展。
- `packages/`：当前 Visual Studio/NuGet 依赖包缓存。

## 运行入口

Visual Studio 打开 `iCAX.sln` 后，当前推荐先运行 H5 工作台壳：

1. 选择 `Debug | x64`。
2. 将 `icax-workbench/WorkbenchDevServer` 设为启动项目。
3. 按 F5 或 Ctrl+F5。

`WorkbenchDevServer` 会启动 `src/icax-workbench/run-workbench.ps1`，在 `http://127.0.0.1:4173/` 提供前端页面，并自动打开浏览器。当前没有宿主注入 `window.icaxBridge` 时，前端会使用 mock bridge，方便先验证 mailbox Promise、产品启动、项目打开等前端流程。
