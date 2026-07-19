# Project 方案文档

## 1. 工程位置

```text
src/iCAX-Engine/framework/Project/
```

`Project` 是 framework 层的项目管理能力。它不再把 Database、Resources、Behaviour、Facades 和 PDO 全部压在 Project 自身，而是用 `Project -> Scene` 的结构把项目级信息和运行现场分开。

## 2. 分层

```text
ApplicationRuntime
  ProductRuntime
    ProjectCatalog
      Main Project
        ProjectID / ProjectPath / ProjectSetting
        Main Scene
          Repository
          ResourceLibrary
          Universe
          PDOHub?
          Scene Facade channel id
          WorkThread
        Child Scene*
          Repository
          ResourceLibrary
          Universe
          PDOHub?
          Scene Facade channel id
          WorkThread
```

`ApplicationRuntime` 负责应用级生命周期和产品运行时；`ProductRuntime` 负责产品模块、产品 Facade、产品数据和 ProjectCatalog；`ProjectCatalog` 管理主 Project；`Project` 管理主 Scene 与子 Scene；`Scene` 才是运行现场。

## 3. Project 与 Scene 的职责

Project 负责：

- ProjectID、ProjectName、ProjectPath。
- ProjectSetting，也就是跟随图纸保存和打开的项目级参数。
- 主 Scene 与子 Scene 集合。
- 快速保存日志路径、打开、回放、截断和关闭时机。
- 对外暴露主 Scene 的运行句柄，供上层完成默认项目交互。

Scene 负责：

- Repository 和 EC 数据。
- ResourceLibrary 和资源对象。
- Universe 和 Behaviour 实例。
- Scene Facade channel。
- 可选 PDOHub。
- Scene 工作线程和帧调度。

这条边界是硬规则：ProjectContext 只管理项目级环境；Repository、Resource、PDO 和场景服务由 SceneContext 管理。Scene Runtime 负责 SceneContext 的线程、调度、Facade 分发和生命周期。

## 4. 运行流程

打开主项目：

```text
ProductRuntime::OpenProjectCatalog(name, path)
  -> ProjectCatalog.OpenMainProject(name, path, startupComponent)
       -> create CProject
       -> CProject creates MainScene
            -> create scene ResourceLoaderRegistry
            -> create Repository(project id, product MetaRegistry)
            -> create Universe(product BehaviourRegistry)
            -> create ResourceLibrary(scene ResourceLoaderRegistry)
            -> create PDOHub if enabled
            -> create scene Facade channel through CFacadeChannelRegistry
            -> subscribe repository events
  -> replay quick-save log on MainScene Repository
  -> CreateProjectRuntime(project)
  -> ProjectRuntime.SetMainSceneFrameHandler(dispatch main scene Facade)
  -> ProjectRuntime.Start()
  -> open quick-save log for append
```

Scene 每帧：

```text
Scene PreSwapPDO
  -> Scene PDOHub SwapInSlot()
  -> Universe PreSwapPDO()
FrameHandler(scene backend Facade)
  -> Facades(application, product, project, scene)
Scene Tick
  -> Universe Tick(application, product, project, scene)
Scene PostSwapPDO
  -> Universe PostSwapPDO()
  -> Scene PDOHub SwapOutSlot()
```

Repository 事件由 Scene 转发给 Universe，转发时显式携带四层上下文：

```text
ApplicationContext
ProductContext
ProjectContext
SceneContext
```

## 5. 快速保存

快速保存分为两层：

- Database 层：Repository 负责把普通修改、批量提交、事务提交、Undo/Redo 写成有序 `OperationBatch`，并能回放日志。
- Project 层：Project 负责日志路径、打开、回放、截断和关闭时机。

Project 不直接序列化主项目文件。正确流程是：

```text
打开项目文件
  -> 产品/文件模块读取主项目文件
  -> MainScene.Repository.BeginLoadBaseline()
  -> 写入项目基线数据
  -> MainScene.Repository.EndLoadBaseline()
  -> Project.ReplayQuickSaveLog(quickSaveMagic, quickSaveVersion)
  -> MainScene.Start()
  -> Project.OpenQuickSaveLog(false, quickSaveMagic, quickSaveVersion)
```

保存项目文件：

```text
保存项目文件
  -> 产品/文件模块把 MainScene Repository/ResourceLibrary 写入主项目文件
  -> 写文件成功
  -> Project.MarkProjectFileSaved(path?, quickSaveMagic, quickSaveVersion)
       -> close old operation log
       -> truncate quick-save log
       -> write quick-save log header
       -> reopen quick-save log for append
```

关闭 Project 时先关闭快速保存日志，再关闭 Scene，避免 Scene 清理实体和组件时把删除操作追加到快速保存日志。

## 6. 通信

Application、Product、Scene 各自拥有自己的 Facade channel。Project 本身不拥有 channel；外部默认项目交互使用主 Scene channel。

```text
applicationChannelId -> ApplicationRuntime
productChannelId     -> ProductRuntime
sceneChannelId       -> Project MainScene / ChildScene
```

`ProductRuntime::GetSceneFrontendFacadeEndpoint(projectId, sceneId)` 会找到对应 ProjectRuntime，再返回指定 Scene 的 frontend Facade endpoint。ApplicationRuntime 的同名接口只是跨已启动产品做一次查找代理。

应用级命令发到应用 Facade；产品级命令发到产品 Facade；数据编辑、撤销重做、渲染同步等 Scene 级命令发到对应 Scene Facade，并由 Facades 同时接收 ProjectContext 和 SceneContext。

## 7. 与产品级注册表的关系

ComponentMeta、Behaviour 定义、ResourceLoader 和 Facades 都在 ProductRuntime 级装配。产品模块加载后，模块中的宏注册项会被 ProductRuntime 回放到当前产品的注册表。Service 仍在 Application 级 ServiceProvider 中共享。

Scene 创建 Repository 和 Universe 时引用产品级定义能力；创建 ResourceLibrary 时使用 Scene 自己的 ResourceLoaderRegistry。实体数据、组件实例、资源对象、Scene 消息和 Scene 线程完全归属当前 Scene。

## 8. 与 PDO 的关系

PDO Arena 由产品或文件/启动模块决定，通过 Project 创建参数转给主 Scene。Scene 不理解 payload 字段含义，只负责创建、交换和释放 Scene PDOHub。具体 slot 可以由 RenderService、InputService 或其他业务服务在运行期动态分配和释放。

Behaviour、Facades 和 RenderService 通过 `ISceneContext::HasPDOHub()` 和 `ISceneContext::PDOHub()` 访问 Scene PDO。未配置 PDO 的 Scene 会在访问 `PDOHub()` 时抛出异常。

前端宿主通过 Facades 获取 Scene PDO Arena name，再打开对应 shared memory。Scene 关闭时释放 PDOHub；前端宿主收到 Scene 关闭后必须停止访问该 Arena。

## 9. 内存隔离

当前实现是进程内隔离：不同 Scene 拥有独立对象图和线程，但仍处在同一个 OS 进程地址空间内。Behaviour 回调不做异常拦截或过滤，运行错误按第一现场暴露。

严格意义上的独立内存空间必须使用进程级隔离，即每个 Project 或 Scene 由独立 Worker 进程承载。线程和对象边界不能阻止野指针、堆破坏、访问冲突等 native 崩溃影响整个进程。
