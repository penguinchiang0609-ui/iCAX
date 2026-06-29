# Services

`Services` 是 framework 层的服务体系工程，负责服务接口、服务提供器和自动注册辅助。

Mail 通信位于 `Mailbox`，PDO 位于 `PDO`。二者都是运行时通信/共享数据能力，不进入 ServiceProvider。`ApplicationHost` 直接持有 `CMailChannelRegistry`，Project 直接持有自己的 PDOHub。

框架级服务实现放在本工程内。产品级服务实现放在对应 `src/apps/<product-id>/backend/service` 工程，并通过本工程提供的服务注册体系接入。

## 目录结构

- `Services.vcxproj`：服务工程。
- `IService.h` / `ServiceProvider.*` / `ServicesHelper.h`：服务接口、服务提供器和自动注册辅助。

