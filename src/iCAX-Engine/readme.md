# icax-engine

`icax-engine` 是 C++ 后台平台。它负责对象模型、行为调度、服务、资源、Mail 通信和 PDO 高频数据。

当前后台以 `ApplicationHost -> ProductRuntime -> Project -> Scene` 四层组织。ApplicationHost 拥有应用级工作线程并轮询应用级 mailbox；ProductRuntime 拥有产品级工作线程并轮询产品级 mailbox；每个 Scene 拥有自己的 Repository、ResourceLibrary、Universe、MailChannel、PDOHub 和工作线程。

## 目录结构

- `foundation/`：不依赖业务框架的基础类型、数学几何、任务等基础能力。
- `framework/`：实体、组件、行为、应用宿主、服务体系、资源、Mail 通信和 PDO 等后台核心框架。
- `packages/`：当前 Visual Studio/NuGet 依赖包缓存。

## 运行入口

当前推荐先运行新的 H5 ApplicationProxy 调试入口：

```powershell
node ..\iCAX-UI\ApplicationProxy\dev-server.mjs
```

或在仓库根目录运行：

```powershell
node src\iCAX-UI\ApplicationProxy\dev-server.mjs
```

开发服务器会在 `http://127.0.0.1:4173/` 提供唯一 H5 页面。当前没有 ECF 宿主注入 `window.icax` 时，前端会自动使用 mock bridge，方便先验证 mailbox Promise、产品启动、项目打开和产品 webpage 模块挂载流程。

旧工作台实验入口已移动到 `X Other/legacy/icax-workbench`，不再作为正式前端结构继续扩展。
旧 `plugins` 目录已移动到 `X Other/legacy/icax-engine-plugins`，不再作为后台主线结构继续扩展。
