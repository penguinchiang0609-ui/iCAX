# SDO（Service Data Object）

`SDO` 定义产品、宿主或前端对外提供的非周期服务交互面。使用者只需要注册和调用 SDO，不需要理解内部传输对象。`Frame/Endpoint/Channel` 都位于本项目内，只负责承载调用，不形成另一套公开框架概念。SDO 不保存业务状态，也不暴露后端 C++ 对象、Entity 或 Component。

它借用 EtherCAT 中 SDO/PDO 的分层思路：SDO 负责低频控制、查询和事务性操作，PDO 负责高频可覆盖状态；iCAX SDO 本身不是 EtherCAT 或 CoE 的线协议实现。

SDO 的结构化二进制 payload 使用普通 Google FlatBuffer。消息帧本身已经提供边界，因此不增加 size prefix 或 iCAX 自定义 envelope；业务模块负责 schema、file identifier、生成代码和 `schema_version`，接收端在访问 root 前必须使用生成类型与 file identifier 执行 `Verifier` 校验。

一个可调用方法的公开身份固定为 `SDOName.MethodName`。两段名称都必须匹配 `[A-Z][A-Za-z0-9_]*`，完整名称只允许一个点。例如 `Machine.Jog`。`Cam.Machine.Import` 不是合法名称。

SDO 是软件接口，不是硬件地址空间。机床实例、轴和运动量必须放进参数：

```text
Machine.Jog({ machineId, axis, delta })
```

不得为每个机床实例或轴创建 SDO，也不得把运行时 ID 拼进名称。`Machine.Jog` 是稳定能力契约；`machineId` 和 `axis` 是本次调用的数据。

两段名称分别生成 32 位稳定码并组合成 64 位方法码，供内部传输帧紧凑承载和进程内查找。名称是协议身份，编码不是另一套公开概念。

SDO 方法会收到只读的 `const IApplicationContext&`、可选的 `ProductContext`、可选的 `ProjectContext` 和可选的 `SceneContext`。应用级调用没有产品、项目和场景上下文；产品级调用没有项目和场景上下文；Scene 范围调用同时获得 Product、Project 和 Scene 上下文。应用设置只能由 ApplicationRuntime 工作线程中的应用级 SDO 修改，其他 SDO 不能通过调用参数获得写权限。

## 目录结构

- `SDOMethod.*`：SDO 名称、方法名称及 64 位紧凑编码。
- `Invocation.*`：一次调用、调用结果和状态。
- `SDO.*`：`ISDO` 接口、`CSDO` 基类和方法公开入口。
- `SDORegistry.*`：当前运行范围内的 SDO 注册表。
- `SDORegistrationCatalog.*`：产品 SDO 模块的自动注册回放表。
- `SDOFrame.*`：内部请求、汇报、响应和事件帧；同一次调用始终使用同一个 `CallID`。
- `SDOPayload.*`：文本与二进制 payload 转换。
- `SDOPayload.h` / `SDOFlatBuffer.h`：SDO 字节所有权以及标准 Google FlatBuffer 构造、校验和读取辅助。
- `SDOQueue.*`：有界、拥有 payload 的内部帧队列。
- `SDOEndpoint.*`：双向通道的一端。
- `SDOChannel.*`：连接两个运行端的内部双向通道。
- `SDOChannelRegistry.*`：按运行范围管理通道。
- `SDOInvoker.*`：调用本端 SDO，或返回 `Task<CInvocationResult>` 异步调用对端 SDO；SDO 只完成 Task，不创建工作线程。调用方在 `CallRemote()` 创建初始 Task 时传入所需 scheduler，后续 `ContinueWith()` 默认继承它；后端使用 `Universe::GetEngineTaskScheduler()`，前端桥使用 `GetFrontTaskScheduler()`，分别回到自己的单线程 event loop。带 scheduler 参数的 `ContinueWith()` 可显式切换后续执行位置。
- `SDOExport.h`：DLL 导出宏。

注册只允许新增，不提供覆盖和运行时注销。`CSDORegistry::GetMethods()` 可导出当前运行范围的协议清单，用于诊断和文档生成。模块按进程常驻处理，不支持卸载后继续回放注册动作。
