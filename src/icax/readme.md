# icax

`icax` 是 C++ 后台平台。它运行在 backend 单线程中，负责对象模型、行为调度、服务、Mail 通信、PDO 高频数据和平台级扩展。

## 目录结构

- `foundation/`：不依赖业务框架的基础类型、数学几何、任务等基础能力。
- `framework/`：实体、组件、行为、应用宿主、服务体系、Mail 通信和 PDO 等后台核心框架。
- `plugins/`：平台级 C++ 扩展。
- `packages/`：当前 Visual Studio/NuGet 依赖包缓存。
