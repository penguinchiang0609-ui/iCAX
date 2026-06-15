# Services 规格文档

## 1. 定位

`Services` 是 framework 层的服务体系工程，提供服务接口、服务提供器、自动注册辅助和框架级服务实现。

它不承载具体产品业务逻辑。日志、资源、设置等内置服务实现可以放在 `plugins/core/CoreService`，产品服务可以放在产品自己的 service 工程中。

## 2. 核心能力

### 2.1 IService

所有可注册服务的基础接口。

服务实现通过继承 `IService` 接入服务体系。

### 2.2 CServiceProvider

服务提供器负责：

- 注册服务实例。
- 按接口类型解析服务。
- 卸载所有服务。

### 2.3 ServicesHelper

自动注册辅助，用于让服务模块在加载时把服务实例注册到全局服务提供器。

### 2.4 IMailPostOfficeService

框架级邮局服务，按后台实例 ID 管理 `CMailChannel`，并提供 backend/frontend 两侧邮局。

```cpp
auto service = iCAX::Services::GetGlobalServiceProvider()
    ->Resolve<iCAX::Services::IMailPostOfficeService>();

auto backend = service->GetBackendPostOffice(instanceId);
auto frontend = service->GetFrontendPostOffice(instanceId);
```

## 3. 依赖边界

`Services` 可以依赖：

- `foundation/Data`
- `framework/Mailbox`

其他 framework 项目、插件和产品代码可以依赖 `Services`。

## 4. 当前约束

- 当前使用全局服务提供器。
- 当前不区分应用级服务容器和项目级服务容器。
- 当前不负责具体业务命令分发。
