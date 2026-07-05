# Project 规格文档

## 1. 定位

`Project` 是 framework 层的项目管理容器。它承载项目身份、项目路径、项目角色、快速保存日志生命周期和跟图纸走的 `ProjectSetting`。

`Project` 不直接拥有 Repository、Universe、ResourceLibrary、PDOHub、MailChannel 或工作线程。这些运行期对象全部归属 `Scene`。Project 启动后会创建一个主 Scene；导入预览、局部编辑、仿真或临时转换可以由已有 Scene 打开子 Scene。

## 2. 上下文边界

- `IProjectContext`：只提供 ProjectID、ProjectName、ProjectPath 和 ProjectSetting。
- `ISceneContext`：提供 SceneID、Scene mail、Repository、ResourceLibrary、PDOHub、ServiceProvider 和 Scene 运行现场。

Behaviour、CommandHandler 和 RenderService 需要同时拿到 ProjectContext 与 SceneContext。ProjectContext 不能替代 SceneContext，SceneContext 也不能遮盖 ProjectContext。

## 3. Project

`CProject` 创建时需要注入应用、产品、服务、元数据、行为、资源加载器和 mailbox 注册表等依赖。Project 使用这些依赖创建主 Scene：

```cpp
iCAX::Project::CProjectCreateInfo info;
info.ProjectName = "Robot Cell";
info.ProjectPath = "D:/projects/robot.icax";
info.StartupComponent = "StartupComponent";
info.pApplicationContext = applicationContext;
info.pProductContext = productContext;
info.pServiceProvider = serviceProvider;
info.pMetaRegistry = productMetaRegistry;
info.pBehaviourRegistry = productBehaviourRegistry;
info.pResourceLoaderRegistry = sceneResourceLoaderRegistry;
info.pMailChannelRegistry = mailChannelRegistry;

iCAX::Project::CProject project(info);
```

Project 对外提供：

- 项目身份：`GetProjectID()`。
- 项目名称和路径：`GetProjectName()`、`GetProjectPath()`。
- 项目级设置：`Settings()`。
- 主 Scene：`GetMainScene()`。
- 子 Scene 管理：`OpenChildScene()`、`CloseScene()`、`GetScene()`、`GetScenes()`。
- 快速保存日志：`ReplayQuickSaveLog()`、`OpenQuickSaveLog()`、`MarkProjectFileSaved()`、`CloseQuickSaveLog()`。

Project 中保留的 `Database()`、`Resources()`、`PDOHub()`、`GetFrontendPostOffice()` 等便利方法只转发到主 Scene，不能理解为 Project 自己拥有这些运行时资源。

## 4. Scene

`CProjectScene` 是独立运行或编辑现场。每个 Scene 拥有：

- 一个 Repository，承载 EC 数据、事务、撤销重做和快速保存日志写入。
- 一个 ResourceLibrary，承载当前 Scene 的资源对象。
- 一个 Universe，承载 Behaviour 实例和生命周期调度。
- 一个 Scene mail channel，用于前后端低频通信。
- 一个可选 PDOHub，用于高频共享内存同步。
- 一个后台工作线程，驱动 `PreSwapPDO`、邮件处理、`Tick` 和 `PostSwapPDO`。
- 一个 SceneSetting，用于 Scene 运行期设置，默认不跟随项目文件持久化。

启动与停止：

```cpp
auto& scene = project.GetMainScene();

scene.SetFrameHandler([](
    iCAX::Project::CProjectScene& scene,
    const iCAX::Mail::CMailPostOffice& backendOffice) {
    auto mails = backendOffice.Receive();
    // 在 Scene 线程内处理当前 Scene 的命令。
});

scene.Start();
scene.Stop();
```

Scene 前端邮局可低成本获取：

```cpp
auto frontendOffice = scene.GetFrontendPostOffice();
```

`Close()` 会停止 Scene 线程、关闭 Scene mail channel，并释放 Universe、Repository、ResourceLibrary 和 PDOHub。

## 5. 快速保存

Project 负责快速保存日志路径、打开、回放、截断和关闭时机；具体操作日志写入和回放落在主 Scene 的 Repository 上。

打开项目的标准顺序：

```cpp
const std::string quickSaveMagic = productFile.QuickSaveLogMagic;
const uint32_t quickSaveVersion = productFile.QuickSaveLogVersion;

// 1. 产品/文件模块读取 project.icax，并通过 Repository::BeginLoadBaseline/EndLoadBaseline 写入主 Scene Repository 基线。
// 2. Project 回放崩溃前追加日志。
project.ReplayQuickSaveLog(quickSaveMagic, quickSaveVersion);

// 3. 主 Scene 启动后打开追加日志。
project.OpenQuickSaveLog(false, quickSaveMagic, quickSaveVersion);
```

保存项目文件成功后的标准顺序：

```cpp
// 1. 产品/文件模块把当前主 Scene Repository 和需要内嵌的资源写入 project.icax。
// 2. 成功后通知 Project 截断旧快速保存日志，并重新开始追加。
project.MarkProjectFileSaved("", quickSaveMagic, quickSaveVersion);
```

快速保存日志的 `magic` 和 `version` 由产品/文件层提供。Project 不定义产品身份，也不维护日志格式升级策略。

## 6. ProjectRuntime

`IProjectRuntime` 是 ProductRuntime 看到的项目运行句柄。它提供项目身份、状态、主 Scene 前端邮局、启动、停止和关闭能力，但不要求上层知道项目内部是不是 `CProject`。

当前实现是 `CLocalProjectRuntime`，它包装进程内 `CProject`，实际工作线程由主 Scene 持有。若未来需要每个 Project 独立内存空间，应增加新的进程级实现，而不是让 ProductRuntime 直接依赖 `CProject` 内部对象。

## 7. ProjectCatalog

`CProjectCatalog` 管理一个主 Project 和若干临时 Project：

```cpp
iCAX::Project::CProjectCatalogCreateInfo catalogInfo;
catalogInfo.pMetaRegistry = productMetaRegistry;
catalogInfo.pBehaviourRegistry = productBehaviourRegistry;
catalogInfo.pMailChannelRegistry = mailChannelRegistry;
catalogInfo.ResourceLoaderRegistryFactory = []() {
    return CreateSceneResourceLoaderRegistry();
};

iCAX::Project::CProjectCatalog projectCatalog(catalogInfo);

auto mainProject = projectCatalog.OpenMainProject("Robot Cell", "D:/projects/robot.icax");
auto previewProject = projectCatalog.OpenTransientProject("Import Preview");
```

Catalog 只负责打开、关闭和查询 Project。Project 的主 Scene 或子 Scene 才是真正的数据和运行隔离单元。

## 8. 隔离规则

- Project 之间的 ProjectSetting、主 Scene 和子 Scene 集合互相隔离。
- Scene 之间的 Repository、ResourceLibrary、Universe、mail channel、PDOHub 和后台线程互相隔离。
- Project 必须显式注入 MetaRegistry、BehaviourRegistry、ResourceLoaderRegistry 和 MailChannelRegistry，不读取全局注册表。
- 资源路径相同也只在各自 Scene 的 `ResourceLibrary` 内复用，不跨 Scene 共享对象。
- Behaviour 回调不做异常拦截或过滤，运行错误按第一现场暴露；独立 OS 地址空间必须由进程级 `IProjectRuntime` 实现。
