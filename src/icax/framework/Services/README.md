# Services

`Services` 是 framework 层的服务体系工程，负责服务接口、服务提供器、自动注册辅助和框架级服务实现。

Mail 通信的基础消息结构、单向队列、双向通道和邮局位于 `src/icax/framework/Mailbox`。`CMailChannelService` 是应用级服务，按 mail id 显式创建、查询和删除 `CMailChannel`；`ApplicationHost`、`ProductRuntime`、`Project` 只保存自己的 mail id，并从服务中获取对应的 frontend/backend post office。

具体的日志、资源、设置等内置服务实现位于 `src/icax/plugins/core/CoreService`，通过本工程提供的服务注册体系接入。

## 目录结构

- `Services.vcxproj`：服务工程。
- `IService.h` / `ServiceProvider.*` / `ServicesHelper.h`：服务接口、服务提供器和自动注册辅助。
- `IMailChannelService.h` / `MailChannelService.*`：应用级 MailChannel 目录服务，统一托管 channel 生命周期。
