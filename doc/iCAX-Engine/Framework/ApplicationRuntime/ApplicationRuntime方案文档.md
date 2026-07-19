# ApplicationRuntime 方案文档

## 1. 工程位置

```text
src/icax-engine/framework/ApplicationRuntime/
```

`ApplicationRuntime` 是应用级运行时，不是 ProjectCatalog 容器。

## 2. 架构

```text
ApplicationRuntime
  ApplicationContext
  Application FacadeRegistry / FacadeInvoker
  Application Facade channel id
  CFacadeChannelRegistry
  ProductDefinition*
  ProductRuntime*
    Product Facade channel id
    ProjectCatalog*
      Project*
```

ApplicationRuntime 只处理应用级生命周期和应用级入口。产品模块加载、最近项目、ProjectCatalog 和 Project 的接入交给 `ProductRuntime`。

ApplicationRuntime 保存的是产品定义列表。产品定义只包含静态能力，例如前端入口、项目文件 magic 和模块路径；最近项目、产品用户设置等运行数据由对应 `ProductRuntime` 通过 ProductDataStore 加载。

ApplicationRuntime 还负责项目文件的轻量识别：根据 `ProductDefinition.ProjectFile.Magic` 读取少量文件头，判断文件应交给哪个产品。这里不解析项目数据，也不创建 Repository。magic 是产品文件类型的唯一识别依据，不能为空且不能重复；扩展名只用于文件选择器和展示。

## 3. 装配流程

```text
CApplicationRuntime::Start
  -> start work thread
  -> wait until Running or Faulted

WorkThread
  -> Notify Starting
  -> Phase Loading
  -> Create FacadeChannelRegistry
  -> Create application Facade channel
  -> Create ApplicationContext
       -> own and load application config store
       -> own application ServiceProvider
  -> RegisterBuiltInApplicationFacades
  -> Start startup product?
  -> Phase Running
  -> Notify Started
  -> loop MainLoop
  -> Phase Stopping
  -> Notify Stopping
  -> Phase Unloading
  -> Stop all ProductRuntime
  -> Remove application Facade channel
  -> Destroy ApplicationContext
       -> unload application service instances
  -> Notify Stopped
```

## 4. 主循环

```text
MainLoop
  -> Dispatch application Facade
```

`ApplicationRuntime` 只处理应用级 Facade。`ProductRuntime` 拥有自己的产品级工作线程并处理产品级 Facade；每个 `Scene` 拥有自己的 Scene 线程并处理 Scene Facade。因此应用、产品和 Scene 运行现场互不共享主循环。

## 5. 命令上下文

应用级命令上下文包含：

```text
ApplicationContext
FacadeRegistry
ProductDefinition list snapshot
ProductRuntime list snapshot
CFacadeChannelRegistry
```

其中应用配置、应用路径、应用数据与应用级 `CServiceProvider` 属于 `ApplicationContext`；线程、循环、Facade channel 与 ProductRuntime 生命周期属于 `ApplicationRuntime`。

产品级 Facade 上下文由 `ProductRuntime` 组装。Scene 级 Request 进入具体 Scene channel 时，ProductRuntime 会额外放入当前 ProjectContext 和 SceneContext；业务代码通过 ProjectContext 访问项目身份、路径和 ProjectSetting，通过 SceneContext 访问 Repository、Universe、ResourceLibrary、PDOHub、Scene Facade endpoint 和服务容器。

## 6. 通信通道

应用级通道：

```text
application Facade channel id -> App.GetState / App.ListProducts / App.StartProduct / App.StopProduct / App.ResolveProjectFile / App.OpenProjectFile
```

双击文件打开流程：

```text
Frontend shell receives project file path
  -> App.OpenProjectFile(projectPath)
  -> ApplicationRuntime.ResolveProjectFileProduct
       read ProjectFile.ProbeBytes from project file
       match ProjectFile.Magic at ProjectFile.MagicOffset
  -> StartProduct(productId)
  -> ProductRuntime.OpenProjectCatalog(projectPath)
  -> return product Facade channel id, catalog and main project id
```

如果 magic 未命中，ApplicationRuntime 返回 `NotFound`。如果同一个文件命中多个产品，说明产品定义中的 magic 存在歧义，应作为配置错误处理，而不是交给前端选择。

产品级通道：

```text
product Facade channel id -> Product.GetState / Product.OpenProjectCatalog / Product.CloseProjectCatalog
```

Scene 级通道：

```text
scene Facade channel id -> scene/project commands
```

`CFacadeChannelRegistry` 统一持有所有 `CFacadeChannel`。ApplicationRuntime 直接持有 registry，并显式注入 ProductRuntime、Project 和 Scene；ApplicationRuntime、ProductRuntime 和 Scene 只持有自己的 Facade channel id，并通过 context 暴露 frontend/backend Facade endpoint。上级运行体负责向前端 bridge 发放下级 Facade channel id 对应的 frontend Facade endpoint。运行体停止或关闭时删除对应 channel，旧 Endpoint 随之失效。

ApplicationRuntime 对同一个 `productId` 维护启动中和停止中标记。`StopProduct` 会先标记产品停止中，等 `ProductRuntime::Stop()` 完成后才从运行时表移除；`StartProduct` 遇到同一产品正在停止时等待。这样可以避免关闭再打开时同一产品短时间内出现两个后台运行时。

## 7. 模块加载

产品模块由 `CProductDefinition::Modules` 声明。`ApplicationRuntime` 不加载产品模块，启动产品时由 `ProductRuntime::Start()` 调用 `LoadLibraryA` 加载。

扩展模块继续使用 DLL 自动注册宏。宏注册项由注册目录记录，产品启动后由 `ProductRuntime` 按产品模块路径回放：Service 回放到产品级 ServiceProvider，ComponentMeta 回放到产品级 MetaRegistry，Behaviour 回放到产品级 BehaviourRegistry，ResourceLoader 回放到产品级 ResourceLoaderRegistry。ApplicationContext 的应用级 ServiceProvider 不接受产品线程写入。这样扩展模块按产品定义加载，不增加业务代码编写成本。

## 8. 设计原则

- ApplicationRuntime 只负责应用级入口和产品运行时生命周期。
- ProductRuntime 负责产品级入口和 ProjectCatalog 生命周期。
- Project 负责项目实例状态、ProjectSetting 和 Scene 集合；Scene 负责线程、Repository、ResourceLibrary、Universe、PDOHub 和 Scene Facade。
- ProductRuntime 可以同时维护多个 ProjectCatalog。
- 一个 ProjectCatalog 内只存在一个主 Project。
- 预览、导入、局部编辑和仿真等临时现场靠子 Scene 隔离。
