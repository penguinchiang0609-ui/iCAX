# CommandHandler 方案文档

## 1. 工程位置

```text
src/icax/framework/CommandHandler/
```

`CommandHandler` 放在 framework，是因为它是后端应用协议层的通用抽象。它承接 Framework 的 Mailbox，又向产品 backend 提供统一命令分发模型。

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
Service / Database / Behaviour = execution
```

## 3. 为什么不直接依赖 Database 和 Service

不同产品命令需要的依赖不同。如果 CommandHandler 直接依赖所有 framework/service 项目，它会快速变成新的中心依赖。

所以当前用 `ICommandContext` 提供类型化依赖容器：

```cpp
context.SetDependency<IApplicationContext>(appContext);
context.SetDependency<IRepository>(repository);
context.SetDependency<IResourceService>(resourceService);
```

具体 handler 自己知道需要取什么依赖。

## 4. 典型命令归属

撤销还原：

```text
UndoCommandHandler
  -> IRepository::Undo(domainId)
```

导入 FBX：

```text
ImportFbxCommandHandler
  -> ImportFbxService
  -> ResourceService
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

- Mailbox 适配器。
- 具体命令协议编码。
- 产品业务 handler。
- 服务定位器。

这些应由 ApplicationHost、产品 backend 或后续协议模块接入。
