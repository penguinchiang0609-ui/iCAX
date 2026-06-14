# Services

`Services` 是 icax backend 的服务工程，负责服务接口、服务提供器、设置/资源/日志/Mailbox 等服务装配。

Mailbox 的基础消息结构和队列位于 `src/icax/foundation/Mailbox`。本工程只保留 `IMailBoxService` 与 `CMailBoxService`，用于按引擎 ID 管理 inbox/outbox 并接入服务注册体系。

## 目录结构

- `Services.vcxproj`：服务工程。
- `IMailBoxService.h` / `MailBoxService.*`：Mailbox 服务接口和服务实现，依赖 foundation Mailbox。
- `IService.h` / `ServiceProvider.*` / `ServicesHelper.h`：服务接口、服务提供器和自动注册辅助。
