# Product

`Product` 是产品级后台运行时工程。它位于 `ApplicationHost` 和 `Project` 之间：ApplicationHost 负责列出并启动产品，ProductRuntime 负责产品级 mailbox、产品模块加载、产品运行数据、ProjectCatalog 生命周期和 `IProjectRuntime` 运行时句柄；每个 Project 仍然独立拥有线程、Repository、ResourceLibrary、Universe 和项目 mailbox。

ProductRuntime 保存自己的 `productChannelId`，并通过 ApplicationHost 显式注入的 `CMailChannelRegistry` 创建和删除产品级 `CMailChannel`。前端通过 ApplicationHost 启动产品后，拿到该产品的 frontend post office，再与产品级命令入口对话。

ProductRuntime 不创建独立 ServiceProvider，Service 仍由 ApplicationHost 的应用级 ServiceProvider 共享。Mailbox 不通过 ServiceProvider 解析，而是由 ApplicationHost 显式注入。ProductRuntime 会创建自己的产品级 MetaRegistry、BehaviourRegistry、ResourceLoaderRegistry 和 CommandRegistry；加载产品模块后，将模块中的自动注册项按模块路径回放到当前产品运行时。每个 Project 仍然拥有自己的 Universe 实例、Repository、ResourceLibrary、项目 ResourceLoaderRegistry、项目 mailbox 和可选 PDOHub。

产品定义、产品数据和 `IProductContext` 公共契约位于 `ProductContext` 项目。`Product` 负责产品运行时实现，并提供 `product.manifest.json` 到 `CProductDefinition` 的加载能力；`ProductRuntime` 实现 `IProductContext`。

## 目录结构

- `Product.vcxproj`：产品运行时工程。
- `ProductRuntime.h` / `ProductRuntime.cpp`：产品运行时入口，处理产品级命令、ProjectCatalog 管理、项目运行时登记和项目命令转发。
- `ProductCommands.h` / `ProductCommands.cpp`：产品级内置命令常量和 Variant payload 编解码。
- `ProductManifest.h` / `ProductManifest.cpp`：产品 manifest 读取、校验和 `CProductDefinition` 构造。
- `ProductExport.h`：DLL 导出宏。

