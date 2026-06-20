# CommandHandler 方案文档

## 1. 工程位置

```text
src/icax/framework/CommandHandler/
```

`CommandHandler` 放在 framework，是因为它是后端应用协议层的通用抽象。它位于 Mailbox 之上，向业务模块提供统一命令分发模型。

## 2. 为什么不放 Mailbox

Mailbox 只负责收发邮件和传输 payload。它不应该知道 Undo、SaveProject、ImportFbx 等命令语义。

CommandHandler 处理的是命令语义：

```text
Mail.nTypeCode
  -> CCommandRequest.nTypeCode
  -> ICommandHandler
```

因此二者分层：

```text
Mailbox = transport
CommandHandler = command dispatch
ApplicationHost = application mail adapter and product runtime lifecycle
ProductRuntime = product/project mail adapter and context assembly
Project / Repository / ResourceLibrary / Service = execution dependencies
```

## 3. 为什么不直接依赖 Database 和 Service

不同业务命令需要的依赖不同。如果 CommandHandler 直接依赖所有 framework/service 项目，它会快速变成新的中心依赖。

所以当前用 `ICommandContext` 提供类型化依赖容器：

```cpp
context.SetDependency<IApplicationContext>(appContext);
context.SetDependency<CProject>(project);
context.SetDependency<IRepository>(repository);
context.SetDependency<CResourceLibrary>(resources);
```

具体 handler 自己知道需要取什么依赖。

应用级、产品级和项目级命令的上下文不同：

- 应用级命令从应用邮局进入，通常有 `ApplicationContext`、产品定义列表、已启动产品列表和应用服务。
- 产品级命令从产品邮局进入，通常有 `ApplicationContext`、`ProductRuntime`、`ProjectCatalog` 列表和服务。
- 项目级命令从项目邮局进入，额外有 `Project`、所属 `ProjectCatalog`、`IRepository`、`IUniverse`、`ResourceLibrary`。
- `CommandHandler` 本身不保存这些对象，也不决定命令属于应用级还是项目级。

## 4. 典型命令归属

撤销还原：

```text
UndoCommandHandler
  -> IRepository::Undo()
```

导入 FBX：

```text
ImportFbxCommandHandler
  -> ImportFbxService
  -> CProject::Resources()
  -> IRepository::BeginUndoCommand
  -> 创建 EC 数据
```

保存系统参数：

```text
UpdateSettingsCommandHandler
  -> ApplicationSettingsService::SetValue
  -> ApplicationSettingsService::Save
```

## 5. 不做的事

当前不做：

- 具体命令协议编码。
- 业务 handler。
- 服务定位器。

Mailbox 适配器由 `ApplicationHost` 和 `ProductRuntime` 接入；具体命令协议编码和业务 handler 由业务模块或后续协议模块提供。
