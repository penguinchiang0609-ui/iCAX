# ApplicationHost 方案文档

## 1. 工程位置

```text
src/icax-engine/framework/ApplicationHost/
```

`ApplicationHost` 是应用级宿主，不是 ProjectCatalog 容器。

## 2. 架构

```text
ApplicationHost
  ApplicationContext
  Application CommandRegistry / CommandDispatcher
  Application mail id
  IMailChannelService
  ProductDefinition*
  ProductRuntime*
    Product mail id
    ProjectCatalog*
      Project*
```

ApplicationHost 只处理应用级生命周期和应用级入口。产品模块加载、最近项目、ProjectCatalog 和 Project 的接入交给 `ProductRuntime`。

ApplicationHost 保存的是产品定义列表。产品定义只包含静态能力，例如前端入口、项目文件 magic 和模块路径；最近项目、产品用户设置等运行数据由对应 `ProductRuntime` 通过 ProductDataStore 加载。

ApplicationHost 还负责项目文件的轻量识别：根据 `ProductDefinition.ProjectFile.Magic` 读取少量文件头，判断文件应交给哪个产品。这里不解析项目数据，也不创建 Repository。magic 是产品文件类型的唯一识别依据，不能为空且不能重复；扩展名只用于文件选择器和展示。

## 3. 装配流程

```text
CApplicationHost::Start
  -> start work thread
  -> wait until Running or Faulted

WorkThread
  -> Notify Starting
  -> Phase Loading
  -> LoadApplicationSettings
  -> Create ApplicationContext
  -> Create ServiceProvider
  -> Register built-in MailChannelService
  -> Resolve IMailChannelService
  -> Create application mail channel
  -> RegisterBuiltInApplicationCommands
  -> Start startup product?
  -> Phase Running
  -> Notify Started
  -> loop MainLoop
  -> Phase Stopping
  -> Notify Stopping
  -> Phase Unloading
  -> Stop all ProductRuntime
  -> Remove application mail channel
  -> Unload service instances
  -> Notify Stopped
```

## 4. 主循环

```text
MainLoop
  -> Dispatch application mailbox
  -> Dispatch every started ProductRuntime product mailbox
```

`ApplicationHost` 只处理应用级 mailbox。`ProductRuntime` 拥有自己的产品级工作线程并处理产品级 mailbox；每个 `Project` 也拥有自己的项目线程并处理项目级 mailbox。因此应用、产品和项目三个层级互不共享主循环。

## 5. 命令上下文

应用级命令上下文包含：

```text
ApplicationContext
CommandRegistry
ProductDefinition list snapshot
ProductRuntime list snapshot
CServiceProvider
IMailChannelService
```

产品级命令上下文由 `ProductRuntime` 组装。项目级命令进入具体 Project 邮箱时，ProductRuntime 会额外放入当前 ProjectContext；业务代码通过 ProjectContext 访问 Repository、Universe、ResourceLibrary、PDOHub 和服务容器。

## 6. 通信通道

应用级通道：

```text
application mail id -> App.GetState / App.ListProducts / App.StartProduct / App.StopProduct / App.ResolveProjectFile / App.OpenProjectFile
```

双击文件打开流程：

```text
Frontend shell receives project file path
  -> App.OpenProjectFile(projectPath)
  -> ApplicationHost.ResolveProjectFileProduct
       read ProjectFile.ProbeBytes from project file
       match ProjectFile.Magic at ProjectFile.MagicOffset
  -> StartProduct(productId)
  -> ProductRuntime.OpenProjectCatalog(projectPath)
  -> return product mail id, catalog and main project id
```

如果 magic 未命中，ApplicationHost 返回 `NotFound`。如果同一个文件命中多个产品，说明产品定义中的 magic 存在歧义，应作为配置错误处理，而不是交给前端选择。

产品级通道：

```text
product mail id -> Product.GetState / Product.OpenProjectCatalog / Product.CloseProjectCatalog
```

项目级通道：

```text
project mail id -> project commands
```

`IMailChannelService` 统一持有所有 `CMailChannel`。ApplicationHost、ProductRuntime 和 Project 只持有自己的 mail id，并通过服务获取 frontend/backend post office。上级运行体负责向前端 bridge 发放下级 mail id 对应的 frontend post office。运行体停止或关闭时删除对应 channel，旧邮局随之失效。

ApplicationHost 对同一个 `productId` 维护启动中和停止中标记。`StopProduct` 会先标记产品停止中，等 `ProductRuntime::Stop()` 完成后才从运行时表移除；`StartProduct` 遇到同一产品正在停止时等待。这样可以避免关闭再打开时同一产品短时间内出现两个后台运行时。

## 7. 模块加载

产品模块由 `CProductDefinition::Modules` 声明。`ApplicationHost` 不加载产品模块，启动产品时由 `ProductRuntime::Start()` 调用 `LoadLibraryA` 加载。

扩展模块继续使用 DLL 自动注册宏。宏注册项由注册目录记录，产品启动后由 `ProductRuntime` 按产品模块路径回放：Service 回放到应用级 ServiceProvider，ComponentMeta 回放到产品级 MetaRegistry，Behaviour 回放到产品级 BehaviourRegistry，ResourceLoader 回放到产品级 ResourceLoaderRegistry。这样扩展模块按产品定义加载，不增加业务代码编写成本。

## 8. 设计原则

- ApplicationHost 只负责应用级入口和产品运行时生命周期。
- ProductRuntime 负责产品级入口和 ProjectCatalog 生命周期。
- Project 负责项目实例状态、项目线程、Repository、ResourceLibrary、Universe 和项目 mailbox。
- ProductRuntime 可以同时维护多个 ProjectCatalog。
- 一个 ProjectCatalog 内主项目最多存在一个。
- 临时项目靠 ProjectID 隔离，用于预览、导入和转换。
