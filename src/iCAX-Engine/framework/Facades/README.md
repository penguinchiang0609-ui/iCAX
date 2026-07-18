# Facades

`Facades` 定义产品或宿主对外提供的面向对象交互面。它不依赖 Mailbox，不保存业务状态，也不暴露后端 C++ 对象、Entity 或 Component。

一个可调用方法的公开身份固定为 `FacadeName.MethodName`。两段名称都必须匹配 `[A-Z][A-Za-z0-9_]*`，完整名称只允许一个点。例如 `Machine.Jog`。`Cam.Machine.Import` 不是合法名称。

Facade 是软件接口，不是硬件地址空间。机床实例、轴和运动量必须放进参数：

```text
Machine.Jog({ machineId, axis, delta })
```

不得为每个机床实例或轴创建 Facade，也不得把运行时 ID 拼进名称。`Machine.Jog` 是稳定能力契约；`machineId` 和 `axis` 是本次调用的数据。

两段名称分别生成 32 位稳定码并组合成 64 位方法码，供 Mail 紧凑承载和进程内查找。名称是协议身份，编码不是另一套公开概念。

Facade 方法会收到 `ApplicationContext`、可选的 `ProductContext`、可选的 `ProjectContext` 和可选的 `SceneContext`。应用级调用没有产品、项目和场景上下文；产品级调用没有项目和场景上下文；Scene 范围调用同时获得 Product、Project 和 Scene 上下文。

## 目录结构

- `FacadeMethod.*`：Facade 名称、方法名称及 64 位紧凑编码。
- `FacadeCall.*`：调用、结果和状态。
- `Facade.*`：`IFacade` 接口、`CFacade` 基类和方法公开入口。
- `FacadeRegistry.*`：当前运行范围内的 Facade 注册表。
- `FacadeRegistrationCatalog.*`：产品 Facade 模块的自动注册回放表。
- `FacadeInvoker.*`：查找 Facade 并调用方法。
- `FacadesExport.h`：DLL 导出宏。

注册只允许新增，不提供覆盖和运行时注销。`CFacadeRegistry::GetMethods()` 可导出当前运行范围的协议清单，用于诊断和文档生成。模块按进程常驻处理，不支持卸载后继续回放注册动作。
