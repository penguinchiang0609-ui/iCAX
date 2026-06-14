# icax

`icax` 是 C++ 后台平台。它运行在 backend 单线程中，负责对象模型、行为调度、服务、Mailbox 通信、PDO 高频数据和平台级扩展。

## 目录结构

- `foundation/`：不依赖业务框架的基础类型、数学几何、任务、ProcedureCall、Mailbox 和 PDO 等基础能力。
- `framework/`：实体、组件、行为、事务追踪等后台核心框架。
- `runtime/`：后台运行时和宿主装配逻辑。
- `services/`：后台服务接口与服务实现。
- `plugins/`：平台级 C++ 扩展。
- `packages/`：当前 Visual Studio/NuGet 依赖包缓存。
