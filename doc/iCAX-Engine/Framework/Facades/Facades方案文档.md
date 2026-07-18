# Facades 方案文档

## 1. 定位

Facades 是产品和宿主的对外交互层。它以面向对象方式组织稳定业务能力，但不远程暴露后端 C++ 对象、Entity 或 Component。

Facade 不是 Command 投递目标，也不是硬件地址空间。底层仍可使用 Mail 传输，但公开模型只有调用：

```text
FacadeName.MethodName(parameters) -> result
```

## 2. 名称模型

公开方法名固定为两个单段标识符：

```text
Machine.Jog
WorkpieceModel.Import
Project.Undo
```

Facade 名称与方法名称均匹配 `[A-Z][A-Za-z0-9_]*`，完整名称只允许一个点。`Cam.Machine.Import` 不合法。

两段名称分别生成 FNV-1a 32 位稳定码，组合为 64 位方法码：

```text
high 32 bits = Facade code
low  32 bits = Method code
```

64 位值只用于 Mail 和进程内查找；公开协议以名称为身份。

## 3. 软件接口而非硬件寻址

运行时实例不得进入 Facade 地址空间。

```text
不采用：MachineID#AxisNo#Jog
采用：  Machine.Jog({ machineId, axis, delta })
```

`Machine.Jog` 是稳定产品能力。机床数量、实例 ID 和轴结构变化不会产生新的 Facade 或方法。`machineId`、`axis` 和 `delta` 是调用参数。

同理，`Workpiece.SetActive({ workpieceEntityId })` 不会为每个工件创建 Facade；`IntentToolpath.Split({ intentToolpathId, ... })` 也不会把意图刀路 ID 拼入方法名。

## 4. 核心对象

- `CFacadeMethod`：Facade 名称、方法名称和紧凑编码。
- `CFacadeCall`：一次方法调用的身份、参数 payload 和关联 ID。
- `CFacadeResult`：调用状态、结果 payload 和错误信息。
- `IFacade`：一个稳定业务接口。
- `CFacade`：声明方法并执行方法函数的基类。
- `CFacadeRegistry`：当前运行范围的 Facade 集合。
- `CFacadeInvoker`：根据方法码查找 Facade 并调用方法。
- `CFacadeRegistrationCatalog`：模块静态注册动作的回放目录。

## 5. 调用流程

```text
Caller
  -> CFacadeCall
  -> CFacadeInvoker
  -> CFacadeRegistry.Find(facadeCode)
  -> IFacade.Invoke(call, contexts...)
  -> exposed method
  -> CFacadeResult
```

Facade 核心不依赖 Mailbox。进程内代码和测试可以直接调用 `CFacadeInvoker`。`CMailFacadeHandler` 只是 Mail 与 `CFacadeCall/CFacadeResult` 之间的适配器。

## 6. 上下文与范围

Facade 方法可以获得：

- `ApplicationContext`：始终存在；
- `ProductContext`：产品和 Scene 范围存在；
- `ProjectContext`：Scene 范围存在；
- `SceneContext`：Scene 范围存在。

方法必须校验自己要求的范围。例如 `Product.OpenProjectCatalog` 只接受产品 mailbox 调用，`Project.Undo` 只接受具体 Scene mailbox 调用。范围不匹配是一次失败调用，不应让项目线程故障退出。

## 7. 产品所有权

共享插件提供 Component、Resource、Behaviour、Service 和算法；具体产品拥有 Facade 协议。不同产品可以基于同一个 Machine 插件定义不同的 `Machine` Facade。

产品 manifest 的 `facades` 模块由 `ProductRuntime` 加载。模块中的 `ICAX_REGISTER_FACADE` 只记录注册动作；运行时按已加载模块路径回放到自己的 `CFacadeRegistry`，保证产品隔离。

## 8. 稳定性

- Facade 和方法只允许新增，不允许运行时覆盖；
- 同名重复注册返回 false，编码碰撞抛出异常；
- 方法函数异常由 `CFacadeInvoker` 原样传播；
- Mail 边界捕获异常并返回失败的 `CFacadeResult`，避免通信错误击穿运行线程；
- 模块当前按进程常驻，不支持卸载 DLL 后继续使用旧注册动作。
