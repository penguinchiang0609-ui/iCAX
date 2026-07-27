# SDO 方案文档

## 1. 定位

SDO 是产品和宿主的对外交互层。它以面向对象方式组织稳定业务能力，但不远程暴露后端 C++ 对象、Entity 或 Component。

SDO 不是 Command 投递目标，也不是硬件地址空间。公开模型只有调用：

```text
SDOName.MethodName(parameters) -> result
```

## 2. 名称模型

公开方法名固定为两个单段标识符：

```text
Machine.Jog
WorkpieceModel.Import
Project.Undo
```

SDO 名称与方法名称均匹配 `[A-Z][A-Za-z0-9_]*`，完整名称只允许一个点。`Cam.Machine.Import` 不合法。

两段名称分别生成 FNV-1a 32 位稳定码，组合为 64 位方法码：

```text
high 32 bits = SDO code
low  32 bits = Method code
```

64 位值只用于 SDO 内部传输和进程内查找；公开协议以名称为身份。

## 3. 软件接口而非硬件寻址

运行时实例不得进入 SDO 地址空间。

```text
不采用：MachineID#AxisNo#Jog
采用：  Machine.Jog({ machineId, axis, delta })
```

`Machine.Jog` 是稳定产品能力。机床数量、实例 ID 和轴结构变化不会产生新的 SDO 或方法。`machineId`、`axis` 和 `delta` 是调用参数。

同理，`Workpiece.SetActive({ workpieceEntityId })` 不会为每个工件创建 SDO；`IntentToolpath.Split({ intentToolpathId, ... })` 也不会把意图刀路 ID 拼入方法名。

## 4. 核心对象

- `CSDOMethod`：SDO 名称、方法名称和紧凑编码。
- `CInvocation`：一次方法调用的身份、参数 payload 和关联 ID。
- `CInvocationResult`：调用状态、结果 payload 和错误信息。
- `ISDO`：一个稳定业务接口。
- `CSDO`：声明方法并执行方法函数的基类。
- `CSDORegistry`：当前运行范围的 SDO 集合。
- `CSDOInvoker`：调用本端 SDO，或异步调用对端 SDO并处理汇报与最终结果。
- `CSDORegistrationCatalog`：模块静态注册动作的回放目录。
- `CSDOFrame`：SDO 内部传输的一次请求、汇报、响应或事件。
- `CSDOEndpoint`：双向 SDO 通道的一端。
- `CSDOChannel`：连接两个运行端的内部双向通道。
- `CSDOChannelRegistry`：按 Application、Product 或 Scene 运行范围管理通道。

## 5. 调用流程

```text
Caller
  -> SDOName.MethodName(parameters)
  -> CSDOInvoker（内部接收并转换请求）
  -> CSDORegistry.Find(sdoCode)
  -> ISDO.Invoke(call, contexts...)
  -> exposed method
  -> CInvocationResult（内部返回结果）
```

`Frame/Endpoint/Channel` 是 SDO 项目内部的传输实现，不形成独立的 Handler 或通信框架。使用者只面对 SDO 名称、参数、汇报和结果。进程内代码和测试仍可直接调用 `CSDOInvoker::Invoke`。

跨端调用是对称的：前端可以调用后端提供的 SDO，后端也可以调用前端提供的 SDO。调用方向只决定哪个 Endpoint 发送 Request，不改变协议模型。

```text
前端 SDOClient.invoke() -> Promise
后端 CSDOInvoker::CallRemote() -> Task<CInvocationResult>
```

Request、Report 和 Response 共享同一个 `CallID`。接收端处理 Request 后可发送零到多个 Report，最后发送唯一 Response。前端 Promise 和后端 Task 均由 Response 完成。SDO 不创建 continuation 线程；远端 Report 回调在收帧/分发线程执行。后端需要操作 Scene/EC 状态时，在 `CallRemote()` 创建初始 Task 时传入 `Universe::GetEngineTaskScheduler()`；后续 `ContinueWith()` 默认继承它。调度器只入队，并在下一次 Tick 开始时由 Scene 工作线程消费。前端原生 Task 在创建时绑定 `FrontendBridge::GetFrontTaskScheduler()`，由 UI event loop 消费。需要主动切换线程时，可给 `ContinueWith()` 显式传入另一个 scheduler。

## 6. 上下文与范围

SDO 方法可以获得：

- `ApplicationContext`：始终存在；
- `ProductContext`：产品和 Scene 范围存在；
- `ProjectContext`：Scene 范围存在；
- `SceneContext`：Scene 范围存在。

方法必须校验自己要求的范围。例如 `Product.OpenProjectCatalog` 只接受产品 SDO 调用，`Project.Undo` 只接受具体 Scene SDO 调用。范围不匹配是一次失败调用，不应让项目线程故障退出。

## 7. 产品所有权

共享插件提供 Component、Resource、Behaviour、Service 和算法；具体产品拥有 SDO 协议。不同产品可以基于同一个 Machine 插件定义不同的 `Machine` SDO。

产品 manifest 的 `sdo` 模块由 `ProductRuntime` 加载。模块中的 `ICAX_REGISTER_SDO` 只记录注册动作；运行时按已加载模块路径回放到自己的 `CSDORegistry`，保证产品隔离。

## 8. 稳定性

- SDO 和方法只允许新增，不允许运行时覆盖；
- 同名重复注册返回 false，编码碰撞抛出异常；
- 方法函数异常由 `CSDOInvoker` 原样传播；
- SDO 的请求边界捕获异常并返回失败的 `CInvocationResult`，避免单次调用击穿运行线程；
- 模块当前按进程常驻，不支持卸载 DLL 后继续使用旧注册动作。
