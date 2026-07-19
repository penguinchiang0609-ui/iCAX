# Services 规格文档

## 1. 定位

`Services` 是 framework 层的服务体系工程，提供服务接口、服务提供器和自动注册辅助。

它不承载具体业务逻辑。框架级服务实现放在 framework 内，产品级服务实现放在对应 `src/apps/<product-id>/backend/service` 工程中。

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

自动注册辅助，用于让服务模块在加载时把注册动作记录到 `CServiceRegistrationCatalog`。产品模块由 ProductRuntime 回放到对应 ProductContext 的 ServiceProvider；ApplicationContext 管理独立的应用级 ServiceProvider，其注册和首次初始化只能发生在 ApplicationRuntime 工作线程。只读 Provider 的 `Resolve<T>()` 只返回已初始化的 `shared_ptr<const T>`，不会调用服务工厂。

### 2.4 通信通道边界

`Services` 不承载 Facades，也不承载 PDO。通信和共享内存能力属于运行时上下文：

- Facades 位于 `framework/Facades`，由 `ApplicationRuntime` 直接持有 `CFacadeChannelRegistry` 并显式注入 ProductRuntime / Project / Scene。
- PDO 位于 `framework/PDO`，由 `Scene` 直接持有 `IPDOHub` 并通过 `ISceneContext::PDOHub()` 暴露。

约束：

- ServiceProvider 只管理实现 `IService` 的服务对象。
- Facades / PDO 不允许注册成 Service。
- 业务代码需要通信能力时从 Application/Product/Project/Scene context 获取；项目级设置走 ProjectContext，运行现场能力走 SceneContext。

## 3. 依赖边界

`Services` 可以依赖：

- 其他 foundation 基础能力。

其他 framework 项目、产品模块和业务代码可以依赖 `Services`。

## 4. 当前约束

- 正式运行路径使用 ApplicationRuntime 创建的应用级服务提供器。
- 全局服务提供器仅作为底层测试和裸用场景入口。
- 当前不负责具体业务命令分发。

