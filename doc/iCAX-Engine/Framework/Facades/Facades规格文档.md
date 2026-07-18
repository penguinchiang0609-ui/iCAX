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

`CFacadeCall` 包含 `nCallID`、`nOriginID`、`Method` 和二进制 `Payload`。

`CFacadeResult` 包含相同调用身份、状态、错误和结果 `Payload`。状态包括：

- `Ok`
- `FacadeNotFound`
- `MethodNotFound`
- `InvalidCall`

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

- 方法编码无效：`InvalidCall`
- Facade 不存在：`FacadeNotFound`
- 方法不存在：`MethodNotFound`
- 方法成功：返回方法生成的结果
- 方法抛异常：异常原样传播

Invoker 统一回填调用 ID、来源 ID 和方法身份。

## 7. 自动注册

产品模块使用 `ICAX_REGISTER_FACADE` 写入 `CFacadeRegistrationCatalog`。`ProductRuntime` 按实际加载的模块路径调用 `ReplayByModulePaths`，把对应 Facade 注册到产品自己的 registry。

## 8. 验收约束

- `Machine.Jog` 合法；
- `Cam.Machine.Import` 非法；
- `MachineID#AxisNo#Jog` 不属于 Facade 协议；
- `Machine.Jog` 的 payload 可以包含 `machineId`、`axis` 和 `delta`；
- C++ 与 JS 必须生成一致的 64 位方法码；
- Mail 适配前后必须保持方法码和调用关联 ID。
