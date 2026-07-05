# CommandHandler 方案文档

## 1. 工程位置

```text
src/icax-engine/framework/CommandHandler/
```

`CommandHandler` 放在 framework，是因为它是后端应用协议层的通用抽象。它位于 Mailbox 之上，向业务模块提供统一命令分发模型。

## 2. 分层关系

Mailbox 只负责收发邮件和传输 payload，不理解命令语义。

当前框架中，Mailbox 的 `MailHeader::nTypeCode` 承载命令路由码：

```text
MailHeader.nTypeCode
  -> CCommandRoute(high32 main / low32 sub)
  -> CCommandRequest.Route
  -> CCommandDispatcher
  -> ICommandTarget
  -> sub command function
```

因此二者分层：

```text
Mailbox = transport
CommandHandler = command route and dispatch
ApplicationHost = application mail adapter and product runtime lifecycle
ProductRuntime = product/project mail adapter and context assembly
ApplicationContext / ProductContext / ProjectContext / SceneContext = execution context
```

## 3. 为什么是主/子命令

扁平的 `typeCode -> handler` 会导致两个问题：

- 命令身份不清晰，业务上看不出哪些命令属于同一类。
- 容易演化成一个命令一个类，上层代码碎片化。

所以当前设计改为：

```text
main command = 一类命令，如 App / Product / Project / Robot
sub command  = 具体动作，如 GetState / OpenProjectCatalog / Solve
```

底层仍然使用一个 `uint64_t` route code：

```text
high 32 bits = hash(main command name)
low  32 bits = hash(sub command name)
```

上层以字符串定义命令，底层以整数码分发。注册时保存命令名称，发生 hash 碰撞时抛出异常。

命令协议使用 `Main.Sub` 作为上层表达，但 hash 时必须拆成 main/sub 两段分别计算。单段名称必须匹配 `[A-Z][A-Za-z0-9_]*`，不允许中文、空格、连字符、点号或小写开头。这个限制让 C++ 与 H5 使用同一套字节序列计算 FNV-1a，避免跨语言传输中出现隐式编码差异。

## 4. CommandTarget

一个 `ICommandTarget` 对应一个主命令。`CCommandTarget` 是具体主命令类的基类，内部维护：

```text
subCode -> sub command function
```

ApplicationHost 内建命令现在注册为一个 `App` command target：

```text
App
  GetState
  ListProducts
  StartProduct
  StopProduct
  ResolveProjectFile
  OpenProjectFile
```

ProductRuntime 内建命令注册为一个 `Product` command target：

```text
Product
  GetState
  ListProjectCatalogs
  OpenProjectCatalog
  CloseProjectCatalog
```

产品模块通过继承 `CCommandTarget` 提供自己的 command target，例如：

```text
Robot
  Solve
  ImportModel
  GenerateToolpath
```

这样业务代码按领域聚合，而不是一个命令一个类。

## 5. 上下文传递

命令执行只传四类显式上下文：

- `ApplicationContext`：应用级描述、路径和系统设置。
- `ProductContext`：产品定义、产品数据、产品级注册表和产品服务入口；应用级命令为空。
- `ProjectContext`：项目身份、路径和项目级 Settings；非项目命令为空。
- `SceneContext`：Repository、ResourceLibrary、PDOHub、MailChannel 和 Scene 服务入口；非 Scene 命令为空。

应用级、产品级和项目级命令的上下文不同：

- 应用级命令从应用邮局进入，通常有 `ApplicationContext`、产品定义列表、已启动产品列表和应用服务。
- 产品级命令从产品邮局进入，通常有 `ApplicationContext`、`ProductRuntime`、`ProjectCatalog` 列表和服务。
- Scene 级命令从 Scene 邮局进入，同时携带 `ProjectContext` 和 `SceneContext`；项目级 Settings 走 ProjectContext，数据现场走 SceneContext。

命令不再通过泛型依赖袋获取对象。需要什么能力，就从对应层级的 context 获取；如果 context 没有这个能力，应补充该 context 的契约，而不是新增隐式依赖入口。

## 6. 注册隔离

`CCommandRegistry` 不是单例：

- `ApplicationHost` 拥有应用级 registry，只放应用入口命令。
- 每个 `ProductRuntime` 拥有产品级 registry，内建产品命令和产品模块命令都注册到这里。
- Scene 级命令仍使用所属产品的 registry，通过 Scene 邮箱进入时额外携带 `ProjectContext` 和 `SceneContext`。

宏注册保留动态 DLL 的低成本写法：

```cpp
ICAX_REGISTER_COMMAND_TARGET(CRobotCommandTarget)
```

宏只写入 `CCommandRegistrationCatalog`。产品启动加载模块后，`ProductRuntime` 使用已加载模块路径调用 `ReplayByModulePaths`，把当前产品模块贡献的命令目标回放到当前产品 registry。这样不同产品可以在同一进程内加载不同命令目标，又不需要业务模块手写大量注册代码。

`CCommandRegistrationCatalog` 是进程级静态目录，记录中的回放函数可能位于产品 DLL 内。当前产品模块加载后按进程常驻处理，不支持命令模块热卸载、热替换或从 catalog 中删除旧记录。产品级隔离依靠 `ReplayByModulePaths` 回放到各自的 `CCommandRegistry`，不是依靠卸载全局记录。

注册回放不做错误汇总和转换。命令目标构造失败、重复注册、hash 碰撞等问题会在回放调用现场直接抛出，便于定位具体模块和具体命令。

命令表不支持运行时注销。产品启动时完成注册，之后命令表作为当前 runtime 的稳定协议使用；产品停止或项目关闭通过释放对应 runtime 完成。

命令表也不支持覆盖。`CCommandTarget::Bind()` 是 protected，只有派生类能在构造阶段绑定子命令；`CCommandRegistry` 只通过 `Register()` 新增 command target。重复注册返回 false 或抛出 hash 碰撞异常，启动代码应把 false 转换为异常，避免协议被静默替换。

`CCommandRegistry::GetCommandRoutes()` 用于导出当前 runtime 的命令清单。它不参与分发，只用于日志、调试、前端协议查看和产品自检。

## 7. 异常策略

`CommandDispatcher` 只处理路由层可以确定的状态：

- route 无效：返回 `InvalidRequest`。
- 主命令或子命令不存在：返回 `NoHandler`。

子命令函数内部抛出的异常不会被 `CommandDispatcher` 捕获，也不会被 ApplicationHost/ProductRuntime 的邮件适配层转换成错误邮件。异常按调用栈原样传播，遵循“错误停在第一现场”的原则。

## 8. 不做的事

当前不做：

- 具体命令 payload 编码。
- 业务服务定位器。
- 前端 promise 实现。
- Mailbox 传输层语义解释。

Mailbox 适配器由 `ApplicationHost` 和 `ProductRuntime` 接入；具体命令协议编码和业务 command target 由业务模块或后续协议模块提供。

## 9. 测试

测试工程：

```text
src/tests/icax-engine/framework/CommandHandler/CommandHandlerTest/CommandHandlerTest.vcxproj
```

当前测试重点验证命令表稳定性、注册表隔离、命令清单导出、路由层错误状态和异常透传。上层接入通过 `ApplicationHostTest` 和 `ProductTest` 验证应用邮箱、产品邮箱、项目邮箱三条命令链路。
H5 侧 `iCAX-UI` 测试会精确断言 `App.GetState`、`Product.GetState` 和产品命令的 typeCode，确保 JS 生成的 64 位 route code 与 C++ 的 `MakeCommandCode(main, sub)` 一致。
