# Services

`Services` 是 framework 层的服务体系工程，负责服务接口、服务提供器、自动注册辅助和框架级服务实现。

Mail 通信的基础消息结构、单向队列、双向通道和邮局位于 `src/icax/framework/Mailbox`。本工程保留 `IMailPostOfficeService` 与 `CMailPostOfficeService`，用于按后台实例 ID 管理 backend/frontend 邮局并接入服务注册体系。

具体的日志、资源、设置等内置服务实现位于 `src/icax/plugins/core/CoreService`，通过本工程提供的服务注册体系接入。

## 目录结构

- `Services.vcxproj`：服务工程。
- `IMailPostOfficeService.h` / `MailPostOfficeService.*`：邮局服务接口和服务实现，按引擎 ID 管理 `CMailChannel`。
- `IService.h` / `ServiceProvider.*` / `ServicesHelper.h`：服务接口、服务提供器和自动注册辅助。
