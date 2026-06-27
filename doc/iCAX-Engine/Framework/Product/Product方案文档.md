# Product 方案文档

## 1. 工程位置

```text
src/icax-engine/framework/Product/
```

## 2. 分层

```text
ApplicationHost
ProductRuntime
    ProductDefinition
    ProductData / ProductDataStore
    Product MetaRegistry
    Product BehaviourRegistry
    Product ResourceLoaderRegistry
    Product CommandRegistry / CommandDispatcher
    Product mail id
    IProjectRuntime*
    ProjectCatalog*
      Main Project?
      Transient Project*
```

`ProductRuntime` 是产品级边界。它不拥有应用上下文的生命周期，只持有 ApplicationHost 创建的 `ApplicationContext`；应用级配置通过 `ApplicationContext` 读取，不再复制额外的设置副本。

`ProductDefinition` 只表达产品静态能力，例如产品 ID、名称、版本、前端入口、项目文件 magic、文件扩展名和模块列表。`ProductManifest` 负责从产品包的 `product.manifest.json` 构造 `ProductDefinition`。`ProductData` 表达产品运行数据，例如最近项目和产品用户设置，通过 `IProductDataStore` 读写。

## 3. 启动

```text
ApplicationHost::StartProduct(productId)
  -> find CProductDefinition
  -> create CProductRuntime
  -> ProductRuntime::Start
       LoadProductData
       LoadProductModules
       Replay module registrations
       Register built-in product commands
       Prepare product command context
       Create product mail channel
  -> return productChannelId
```

产品模块加载使用 `LoadLibraryA`，同一 DLL 在进程内只加载一次。当前阶段保留已有自动注册宏，但宏本身只把注册动作记录到注册目录；`ProductRuntime::Start()` 会把当前产品模块对应的 ComponentMeta、Behaviour、ResourceLoader 和 CommandHandler 回放到产品级注册表，把 Service 回放到 ApplicationHost 持有的应用级 ServiceProvider。

默认 ProductData 文件位于 `{UserConfigDirectory}/Products/{productId}/Product.Data`，为空时回退到 `Setting/Products/{productId}/Product.Data`。

## 4. Manifest 加载

```text
LoadProductDefinitions(productRoot)
  -> scan productRoot/*/product.manifest.json
  -> LoadProductManifest(manifestPath)
  -> parse productId/productName/projectFile/backend/webpage
  -> resolve backend module paths relative to product directory
  -> return vector<CProductDefinition>
```

manifest loader 属于 `Product` 模块，不属于 `UIContainer`。启动层可以用它构造 `ApplicationHostConfig.Products`，然后再启动 `ApplicationHost` 或 `UIContainer`。

## 5. 打开项目目录

```text
Product.OpenProjectCatalog
  -> ProductRuntime::OpenProjectCatalog
  -> create ProjectCatalog
  -> create project-local ResourceLoaderRegistry from product modules
  -> ProjectCatalog.OpenMainProject
  -> CreateLocalProjectRuntime(project)
  -> register IProjectRuntime
  -> ProjectRuntime.SetFrameHandler(dispatch project mailbox)
  -> ProjectRuntime.Start
  -> RecordRecentProject(projectPath)
  -> return catalog and main project id
```

`ProductRuntime` 不再直接把 `CProject` 当成运行边界，而是维护 ProjectID 到 `IProjectRuntime` 的映射。当前 `IProjectRuntime` 的实现是 `CLocalProjectRuntime`，它包装进程内 `CProject`。后续进程级隔离可以新增 `CProcessProjectRuntime`，让 ProductRuntime 继续只面对运行时句柄。

`ProductRuntime` 负责把项目运行时中的 backend post office 转换为命令请求，并补齐项目级命令上下文。

最近项目记录属于产品数据。`ProductRuntime` 只记录真实文件路径；`memory://`、`ui://` 等虚拟路径不写入 ProductData。最近项目按路径去重，新打开的项目移到列表前端，当前上限为 20 条。

## 6. 命令上下文

产品级命令上下文：

```text
ApplicationContext
ProductRuntime
CommandRegistry
ProjectCatalog list snapshot
CServiceProvider
IMetaRegistry
IBehaviourRegistry
CResourceLoaderRegistry
```

项目级命令上下文：

```text
ApplicationContext
ProductRuntime
CommandRegistry
owning ProjectCatalog
ProjectCatalog list snapshot
IProjectRuntime
Project                  // 仅本地 ProjectRuntime 存在
IRepository
IUniverse
CResourceLibrary
```

产品命令必须发到产品 mailbox。若产品命令发到项目 mailbox，因为上下文中存在当前 `IProjectRuntime`，会返回 `InvalidRequest`。

## 7. 生命周期

`ProductRuntime::Stop()` 会关闭所有 ProjectCatalog，ProjectCatalog 会关闭其中所有 Project。ProductRuntime 随后通过 `IMailChannelService::RemoveChannel(productChannelId)` 删除产品级 channel，旧产品邮局随之失效。

ApplicationHost 卸载时会停止所有已启动 ProductRuntime。

## 8. Project 隔离演进

当前 `ProductRuntime` 使用 `IProjectRuntime` 管理项目。实际实现仍是进程内 `CLocalProjectRuntime -> CProject`，每个 Project 独立线程、Repository、ResourceLibrary、Universe 和项目 mail channel。Behaviour 回调不做异常拦截或过滤，运行错误按第一现场暴露。

若要求每个 Project 拥有独立 OS 内存空间，需要在现有抽象下新增进程级实现：

```text
IProjectRuntime
  CLocalProjectRuntime      // 当前进程内实现
  CProcessProjectRuntime    // 每个 Project 一个 Worker 进程
```

进程级实现中，ProductRuntime 只保留 `ProjectHandle`、状态和 mailbox 代理；Repository、ResourceLibrary、Universe 和业务模块都在 Project Worker 进程内。普通 Mailbox 通过 IPC 转发，高频 PDO 使用每 Project 独立共享内存段。Worker 进程崩溃时，由进程级 ProjectRuntime 负责记录该 Project 的故障状态，并保留其他 Project 与 ProductRuntime。
