# CommandTargets

`CommandTargets` 是 framework 层的后端命令目标抽象，用于把命令请求分发到对应的主命令目标。

本项目不直接依赖 Mailbox，也不保存业务状态。`framework/MailHandler` 负责把邮件转换为 `CCommandRequest`，再交给 `CCommandDispatcher`。

CommandTargets 使用主/子命令模型。`CCommandRoute` 的高 32 位是主命令码，低 32 位是子命令码；底层使用合成后的 64 位 route code 分发。主命令可以是命名空间化名称，例如 `Cam.Machine`、`Cam.Workpiece`；子命令必须是单段动作名称，例如 `Import`、`SetActive`。完整命令字符串按最后一个点切分，因此 `Cam.Machine.Import` 会被解释为主命令 `Cam.Machine`、子命令 `Import`。每个名称分段必须匹配 `[A-Z][A-Za-z0-9_]*`。`CCommandRegistry` 注册的是 `ICommandTarget`，一个 command target 负责一个主命令，并在内部把多个子命令分发到不同函数。`CCommandTarget` 是具体主命令类的基类，业务模块通过继承它实现自己的主命令。

CommandTarget 的入口会直接收到 `ApplicationContext`、可选的 `ProductContext`、可选的 `ProjectContext` 和可选的 `SceneContext`。应用级命令没有产品/项目/场景上下文，产品级命令没有项目/场景上下文；进入具体 Scene 的命令会同时拿到 ProjectContext 和 SceneContext。项目级 Settings 从 ProjectContext 读取，Repository、资源库、PDO、mail 和 Scene 服务从 SceneContext 读取，二者不能互相遮盖。

## 目录结构

- `CommandRoute.*`：主/子命令名称、主/子命令码和 64 位路由码。
- `CommandMessage.*`：命令请求、响应和状态码。
- `CommandTarget.*`：命令目标接口和主命令基类。
- `CommandRegistry.*`：主命令码到 command target 的注册表。
- `CommandRegistrationCatalog.*`：产品命令模块的自动注册回放表。
- `CommandDispatcher.*`：命令分发；只返回路由层状态，handler 异常原样向外传播。
- `CommandTargetsExport.h`：DLL 导出宏。

命令注册只允许新增，不提供覆盖和注销。`CCommandRegistry::GetCommandRoutes()` 可以导出当前 runtime 的命令路由清单，用于日志、诊断和前端协议查看。命令模块按进程常驻处理，不支持热卸载后继续回放注册动作。
