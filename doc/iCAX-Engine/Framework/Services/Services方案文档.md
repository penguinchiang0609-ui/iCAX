# Services 方案文档

## 1. 工程位置

```text
src/icax/framework/Services/
```

`Services` 放在 framework 下，是因为服务体系本身是后台框架的一部分。它为 ApplicationHost、Behaviour、插件和产品代码提供统一的服务注册和解析入口。

## 2. 职责划分

`Services` 工程负责：

- 定义 `IService`。
- 提供 `CServiceProvider`。
- 提供服务自动注册辅助。
- 提供框架级 `MailPostOfficeService`。

`Services` 工程不负责：

- 具体业务服务实现。
- 具体命令处理。
- 项目数据管理。
- 资源对象持久化。

## 3. MailPostOfficeService

`Mailbox` 只提供 Mail、MailQueue、MailChannel 和 MailPostOffice 等基础通信对象。

`MailPostOfficeService` 在服务体系中管理这些对象：

```text
instance id -> CMailChannel
  -> backend post office
  -> frontend post office
```

这样 ApplicationHost、产品 backend 或前端桥接层可以通过同一个服务拿到对应端的邮局。

## 4. 迁移说明

旧位置：

```text
src/icax/services/Services/
```

新位置：

```text
src/icax/framework/Services/
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
- 将 CommandHandler 接入 ApplicationHost 后，业务命令通过服务解析具体能力。
