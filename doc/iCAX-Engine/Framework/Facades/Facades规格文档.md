# Facades 规格文档

## 1. 名称与编码

### 1.1 名称约束

- Facade 名称：`[A-Z][A-Za-z0-9_]*`
- 方法名称：`[A-Z][A-Za-z0-9_]*`
- 完整名称：`FacadeName.MethodName`
- 完整名称必须且只能包含一个点

### 1.2 编码

`MakeFacadeMethodCode(facadeCode, methodCode)` 把两个 32 位码组合成 64 位值。`GetFacadeCode` 和 `GetMethodCode` 分别读取高、低 32 位。

字符串名称使用 `InteractionNameHash32` 生成 FNV-1a 32 位稳定码。编码冲突必须显式报错，不允许静默覆盖。

## 2. CFacadeMethod

字段：

- `strFacadeName`
- `strMethodName`
- `nFacadeCode`
- `nMethodCode`

接口：

- `GetCode()`
- `IsValid()`
- `MakeFacadeMethod(facadeName, methodName)`
- `MakeFacadeMethod(code)`
- `FormatFacadeMethod(method)`

由 64 位编码构造时只能恢复编码，不能反向恢复字符串名称。

## 3. 调用与结果

`CInvocation` 包含 `nCallID`、`Method` 和二进制 `Payload`。

`CInvocationResult` 包含相同调用身份、状态、错误和结果 `Payload`。状态包括：

- `Ok`
- `FacadeNotFound`
- `MethodNotFound`
- `InvalidInvocation`

## 4. IFacade 与 CFacade

`IFacade` 提供：

- `GetName()`
- `GetCode()`
- `HasMethod(methodCode)`
- `GetMethods()`
- `Invoke(call, application, product, project, scene)`

`CFacade` 在构造时接收单段 Facade 名称。派生类通过 protected `ExposeMethod(methodName, func)` 声明方法。空函数、非法名称、重复方法和编码碰撞必须被拒绝。

Facade 名称不包含对象 ID。对象 ID、轴号和其他实例信息必须位于 `Payload`。

## 5. CFacadeRegistry

- `Register(facade)`
- `Has(facadeCode)`
- `Find(facadeCode)`
- `GetCodes()`
- `GetMethods()`

注册表只允许新增。相同名称重复注册返回 false；相同编码对应不同名称时抛出异常。

## 6. CFacadeInvoker

`Invoke` 的结果规则：

- 方法编码无效：`InvalidInvocation`
- Facade 不存在：`FacadeNotFound`
- 方法不存在：`MethodNotFound`
- 方法成功：返回方法生成的结果
- 方法抛异常：异常原样传播

Invoker 统一回填调用 ID 和方法身份。

`CallRemote` 向对端发起调用并立即返回 `Task<CInvocationResult>`，不阻塞当前线程。`DispatchAvailableFrames` 处理当前运行范围已经到达的 Facade 帧：请求帧调用本端 Facade，汇报帧通知调用方，响应帧完成对应 Task。

Facades 只负责完成 Task，不创建工作线程。远端汇报回调在调用 `DispatchAvailableFrames()` 的线程执行。`CallRemote()` 接受 scheduler，并把它绑定到初始 Task；无 scheduler 参数的 `ContinueWith()` 自动继承该 scheduler。需要访问 Scene/EC 状态的后端调用应将所属 `Universe::GetEngineTaskScheduler()` 传给 `CallRemote()`，由下一次 Universe Tick 在 Scene 工作线程执行 continuation。前端原生 Task 在创建时使用 `FrontendBridge::GetFrontTaskScheduler()`；JavaScript Promise 由前端单线程事件循环继续执行。带 scheduler 参数的 `ContinueWith()` 仅用于显式切换后续执行位置。

内部传输帧 `CFacadeFrame` 包含：

- `nCallID`
- `nMethodCode`
- `nKind`：`Request`、`Report`、`Response` 或 `Event`
- `nStatus`
- 自身拥有的二进制 `Payload`

同一次 Request、Report 和 Response 始终使用同一个 `nCallID`，不再生成消息 ID，也不再使用来源 ID 关联消息。`CFacadeEndpoint` 表示通道的一端；`CFacadeChannel` 提供成对 Endpoint；`CFacadeChannelRegistry` 按运行范围管理通道。这些对象属于 Facades 的内部实现，不构成使用者必须理解的另一套协议。

## 7. 自动注册

产品模块使用 `ICAX_REGISTER_FACADE` 写入 `CFacadeRegistrationCatalog`。`ProductRuntime` 按实际加载的模块路径调用 `ReplayByModulePaths`，把对应 Facade 注册到产品自己的 registry。

## 8. 验收约束

- `Machine.Jog` 合法；
- `Cam.Machine.Import` 非法；
- `MachineID#AxisNo#Jog` 不属于 Facade 协议；
- `Machine.Jog` 的 payload 可以包含 `machineId`、`axis` 和 `delta`；
- C++ 与 JS 必须生成一致的 64 位方法码；
- Facades 内部传输前后必须保持方法码和同一个调用 ID；
- 跨端调用必须返回 Task 或 Promise，不允许同步阻塞等待；
- C++ Task 的 continuation 与汇报回调不得在 Facade 收帧/分发线程中执行。
