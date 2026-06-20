# Services 规格文档

## 1. 定位

`Services` 是 framework 层的服务体系工程，提供服务接口、服务提供器、自动注册辅助和框架级服务实现。

它不承载具体业务逻辑。日志、资源、设置等内置服务实现可以放在 `plugins/core/CoreService`，业务服务可以放在对应的 service 工程中。

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

自动注册辅助，用于让服务模块在加载时把注册动作记录到 `CServiceRegistrationCatalog`。ApplicationHost 创建应用级 `CServiceProvider` 后，再把注册动作回放到该容器。

### 2.4 通信通道边界

`Services` 提供应用级 `IMailChannelService`，用于统一托管 Mail channel 生命周期。

`IMailChannelService` 负责：

- 按 mail id 显式创建 `CMailChannel`。
- 判断指定 mail id 的 channel 是否存在。
- 返回 frontend/backend `CMailPostOffice`。
- 按 mail id 显式删除 channel，并使旧 post office 失效。
- 在服务卸载时清空所有 channel。

约束：

- `GetFrontendPostOffice` / `GetBackendPostOffice` 不会隐式创建 channel。
- `CreateChannel` 对重复 id 返回失败，不覆盖旧 channel。
- nil uuid 不是合法 mail id。
- `ApplicationHost`、`ProductRuntime`、`Project` 只保存自己的 mail id，不直接持有 `CMailChannel`。

## 3. 依赖边界

`Services` 可以依赖：

- `foundation/Data`
- `framework/Mailbox`
- 其他 foundation 基础能力。

其他 framework 项目、插件和业务代码可以依赖 `Services`。

## 4. 当前约束

- 正式运行路径使用 ApplicationHost 创建的应用级服务提供器。
- 全局服务提供器仅作为底层测试和裸用场景的兼容入口。
- 当前不负责具体业务命令分发。
