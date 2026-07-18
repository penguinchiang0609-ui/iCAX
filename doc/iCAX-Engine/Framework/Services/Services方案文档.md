# Services 方案文档

## 1. 工程位置

```text
src/icax-engine/framework/Services/
```

`Services` 放在 framework 下，是因为服务体系本身是后台框架的一部分。它为 ApplicationHost、Behaviour、插件和业务代码提供统一的服务注册和解析入口。

## 2. 职责划分

`Services` 工程负责：

- 定义 `IService`。
- 提供 `CServiceProvider`。
- 提供服务自动注册辅助。

`Services` 工程不负责：

- 具体业务服务实现。
- 项目数据管理。
- 资源对象持久化。
- 具体业务命令分发。

## 3. 通信通道边界

`Mailbox` 提供 Mail、MailQueue、MailChannel、MailPostOffice 和 MailChannelRegistry 等基础通信对象。

`CMailChannelRegistry` 不是 Service。`ApplicationHost` 直接持有一个应用级 registry，并显式注入 ProductRuntime / Project / Scene：

```text
ApplicationHost
  CServiceProvider
  CMailChannelRegistry
    applicationChannelId -> CMailChannel
    productChannelId     -> CMailChannel
    sceneChannelId       -> CMailChannel
```

运行体只保存自己的 mail id：

- `ApplicationHost` 保存 `applicationChannelId`。
- `ProductRuntime` 保存 `productChannelId`。
- `Scene` 保存 `sceneChannelId`。

上级运行体创建或启动下级运行体后，向前端 bridge 发放下级 mail id 对应的 frontend post office。Application/Product/SceneContext 暴露本层 post office。`CMailPostOffice` 是弱引用视图；`RemoveChannel` 或 `ClearChannels` 删除底层 channel 后，旧邮局会失效，继续收发会抛出 `std::logic_error`，不会悬空访问已释放队列。

`GetFrontendPostOffice(id)` / `GetBackendPostOffice(id)` 只查询既有 channel，不隐式创建。channel 创建必须走 `CreateChannel(id)`，销毁必须走 `RemoveChannel(id)`。

## 4. 迁移说明

旧位置：

```text
src/icax-engine/services/Services/
```

新位置：

```text
src/icax-engine/framework/Services/
```

迁移后，include 路径仍保持：

```cpp
#include "Services/ServiceProvider.h"
#include "Services/ServicesHelper.h"
```

因为 framework 已经作为工程 include 根目录。

## 5. 后续演进

- 区分全局服务、应用宿主服务和项目服务的生命周期。
- 为服务卸载顺序补充更明确的依赖约束。
- 将 Facades 接入 ApplicationHost 后，业务命令通过服务解析具体能力。

