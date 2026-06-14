# Services

`Services` 是 icax backend 的服务工程，负责服务接口、服务提供器、设置/资源/日志/Mail 通信等服务装配。

Mail 通信的基础消息结构、单向队列、双向通道和邮局位于 `src/icax/foundation/Mailbox`。本工程保留 `IMailPostOfficeService` 与 `CMailPostOfficeService`，用于按引擎 ID 管理 backend/frontend 邮局并接入服务注册体系。

## 目录结构

- `Services.vcxproj`：服务工程。
- `IMailPostOfficeService.h` / `MailPostOfficeService.*`：邮局服务接口和服务实现，按引擎 ID 管理 `CMailChannel`。
- `IService.h` / `ServiceProvider.*` / `ServicesHelper.h`：服务接口、服务提供器和自动注册辅助。
