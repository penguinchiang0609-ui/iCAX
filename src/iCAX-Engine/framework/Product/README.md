# Product

`Product` 是产品级后台运行时工程。它位于 `ApplicationRuntime` 和 `Project` 之间：ApplicationRuntime 负责列出并启动产品，ProductRuntime 负责产品级 Facade、产品模块加载、产品运行数据、ProjectCatalog 生命周期和 `IProjectRuntime` 运行时句柄；Project 负责项目身份和 ProjectSetting，Repository、ResourceLibrary、Universe、PDOHub、Facade 和工作线程归属 Scene。

ProductRuntime 保存自己的 `productChannelId`，并通过 ApplicationRuntime 显式注入的 `CFacadeChannelRegistry` 创建和删除产品级 `CFacadeChannel`。前端通过 ApplicationRuntime 启动产品后，拿到该产品的 frontend Facade endpoint，再调用产品级 Facade 方法。

ProductRuntime 工作循环同时消费 `GetProductTaskScheduler()` 的任务队列，并 Tick 产品的 `CCoroutineRuntime`。产品代码先通过该 scheduler 回到产品工作线程，再调用 `StartCoroutine()`；协程始终在产品线程恢复。产品停止或异常退出时会取消 Runtime 中所有未完成协程；再次启动会创建新的 Runtime。

ProductRuntime 作为 `IProductContext` 的当前实现，管理独立的产品级 ServiceProvider、MetaRegistry、BehaviourRegistry、ResourceLoaderRegistry、FacadeRegistry 和 ProductData；加载产品模块后，将模块中的自动注册项按模块路径回放到当前产品环境，不再修改 ApplicationContext 的服务环境。每个 SceneContext 管理自己的 Repository、ResourceLibrary、Universe 和可选 PDOHub，Scene Runtime 管理线程、调度与 Facade 分发。

产品定义、产品数据和 `IProductContext` 公共契约位于 `ProductContext` 项目。`Product` 负责产品运行时实现，并提供 `product.manifest.json` 到 `CProductDefinition` 的加载能力；`ProductRuntime` 实现 `IProductContext`。

## 目录结构

- `Product.vcxproj`：产品运行时工程。
- `ProductRuntime.h` / `ProductRuntime.cpp`：产品运行时入口，处理产品级 Facade 调用、ProjectCatalog 管理、项目运行时登记和 Scene Facade 调用分发。
- `ProductFacades.h` / `ProductFacades.cpp`：产品级内置 Facade 方法常量和 Variant payload 编解码。
- `ProductManifest.h` / `ProductManifest.cpp`：产品 manifest 读取、校验和 `CProductDefinition` 构造。
- `ProductExport.h`：DLL 导出宏。

