# Product

`Product` 是产品级后台运行时工程。它位于 `ApplicationRuntime` 和 `Project` 之间：ApplicationRuntime 负责列出并启动产品，ProductRuntime 负责产品级 SDO、产品模块加载、产品运行数据、ProjectCatalog 生命周期和 `IProjectRuntime` 运行时句柄；Project 负责项目身份和 ProjectSetting，Repository、ResourceLibrary、Universe、PDOHub、SDO 和工作线程归属 Scene。

ProductRuntime 保存自己的 `productChannelId`，并通过 ApplicationRuntime 显式注入的 `CSDOChannelRegistry` 创建和删除产品级 `CSDOChannel`。前端通过 ApplicationRuntime 启动产品后，拿到该产品的 frontend SDO endpoint，再调用产品级 SDO 方法。

ProductRuntime 工作循环同时消费 `GetProductTaskScheduler()` 的任务队列，并 Tick 产品的 `CCoroutineRuntime`。产品代码先通过该 scheduler 回到产品工作线程，再调用 `StartCoroutine()`；协程始终在产品线程恢复。产品停止或异常退出时会取消 Runtime 中所有未完成协程；再次启动会创建新的 Runtime。

ProductRuntime 作为 `IProductContext` 的当前实现，管理独立的产品级 ServiceProvider、MetaRegistry、BehaviourRegistry、ResourceLoaderRegistry、SDORegistry 和 ProductData；加载产品模块后，将模块中的自动注册项按模块路径回放到当前产品环境，不再修改 ApplicationContext 的服务环境。每个 SceneContext 管理自己的 Repository、ResourceLibrary、Universe 和可选 PDOHub，Scene Runtime 管理线程、调度与 SDO 分发。

项目文件由 ProductRuntime 接入 `ProjectFile`。`OpenProjectFile` 先读取、校验并升级完整文档，再按文件中的 ProjectID 创建 MainScene，恢复 MainScene Database + ResourceLibrary，回放 quick-save log，最后启动 Scene。`SaveProjectFile` 为同步 C++ 入口；运行中的 Scene 会被短暂停止以取得一致快照。前端应调用 MainScene 邮箱上的 `Project.Save` SDO，该方法天然在 Scene 帧线程执行，不需要停 Scene。只有项目文件原子写入成功后才会轮换 quick-save log。

产品 manifest 的 `projectFile.formatRevision` 是单调递增的数值升级依据，`formatVersion` 只用于展示。业务插件使用 `ICAX_REGISTER_RESOURCE_PERSISTENCE_CODEC` 登记稳定 ResourceTypeID 与 Codec，ProductRuntime 会按本产品实际加载的模块路径，将 Codec 自动回放到每个 Project/Scene ResourceLibrary。

产品定义、产品数据和 `IProductContext` 公共契约位于 `ProductContext` 项目。`Product` 负责产品运行时实现，并提供 `product.manifest.json` 到 `CProductDefinition` 的加载能力；`ProductRuntime` 实现 `IProductContext`。

## 目录结构

- `Product.vcxproj`：产品运行时工程。
- `ProductRuntime.h` / `ProductRuntime.cpp`：产品运行时入口，处理产品级 SDO 调用、ProjectCatalog 管理、项目运行时登记和 Scene SDO 调用分发。
- `ProductSDO.h` / `ProductSDO.cpp`：产品级内置 SDO 方法常量和 Variant payload 编解码。
- `ProductManifest.h` / `ProductManifest.cpp`：产品 manifest 读取、校验和 `CProductDefinition` 构造。
- `ProductExport.h`：DLL 导出宏。

