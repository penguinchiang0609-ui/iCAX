# CommandHandler

`CommandHandler` 是 framework 层的后端命令处理抽象。它位于 Mailbox 之上，用于把前端命令解释为后端应用用例调用。

本项目不直接依赖 Mailbox，也不保存业务状态。Mailbox 适配器可以把邮件转换为 `CCommandRequest`，再交给 `CCommandDispatcher`。

## 目录结构

- `CommandMessage.*`：命令请求、响应和状态码。
- `ICommandContext.h` / `CommandContext.*`：命令执行上下文和依赖注入容器。
- `ICommandHandler.h` / `FunctionCommandHandler.*`：命令处理器接口和函数适配器。
- `CommandRegistry.*`：命令 type code 到 handler 的注册表。
- `CommandDispatcher.*`：命令分发和异常到状态码的转换。
- `CommandHandlerExport.h`：DLL 导出宏。
