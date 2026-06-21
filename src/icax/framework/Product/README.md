# Product

`Product` 是产品级后台运行时工程。它位于 `ApplicationHost` 和 `Project` 之间：ApplicationHost 负责列出并启动产品，ProductRuntime 负责产品级 mailbox、产品模块加载、产品运行数据、ProjectCatalog 生命周期和 `IProjectRuntime` 运行时句柄；每个 Project 仍然独立拥有线程、Repository、ResourceLibrary、Universe 和项目 mailbox。

ProductRuntime 保存自己的 `productMailId`，并通过应用级 `IMailChannelService` 显式创建和删除产品级 `CMailChannel`。前端通过 ApplicationHost 启动产品后，拿到该产品的 frontend post office，再与产品级命令入口对话。

ProductRuntime 不创建独立 ServiceProvider，Service 仍由 ApplicationHost 的应用级 ServiceProvider 共享。ProductRuntime 会创建自己的产品级 MetaRegistry、BehaviourRegistry、ResourceLoaderRegistry 和 CommandRegistry；加载产品模块后，将模块中的自动注册项按模块路径回放到当前产品运行时。每个 Project 仍然拥有自己的 Universe 实例、Repository、ResourceLibrary、项目 ResourceLoaderRegistry 和项目 mailbox。

产品定义和产品数据分离：`ProductDefinition` 保存产品静态能力，例如前端入口、项目文件 magic、magic 偏移、探测字节数和模块路径；`ProductData` 保存最近项目和产品用户设置，默认落盘到 `{UserConfigDirectory}/Products/{productId}/Product.Data`。项目文件 magic 是产品类型的唯一识别依据，不能为空且不能重复；扩展名只用于文件选择器和展示。

## 目录结构

- `Product.vcxproj`：产品运行时工程。
- `ProductDefinition.h`：产品定义、前端入口、项目文件格式、产品模块路径和默认项目启动组件。
- `ProductData.h` / `ProductData.cpp`：产品运行数据、最近项目、产品用户设置和默认文件存储。
- `ProductRuntime.h` / `ProductRuntime.cpp`：产品运行时入口，处理产品级命令、ProjectCatalog 管理、项目运行时登记和项目命令转发。
- `ProductCommands.h` / `ProductCommands.cpp`：产品级内置命令常量和 Variant payload 编解码。
- `ProductExport.h`：DLL 导出宏。
