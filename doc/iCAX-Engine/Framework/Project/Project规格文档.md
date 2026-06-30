# Project 规格文档

## 1. 定位

`Project` 是 framework 层的项目工程。

它只表达项目实例，不表达应用类型、业务类型或模块目录。运行态只允许一个主项目。为了支持预览、导入和转换，可以同时打开若干临时项目。

每个 `Project` 独占自己的 `Repository`、`ResourceLibrary`、`Universe`、项目 mail channel、可选 PDOHub 和后台工作线程。底层 `CMailChannel` 由应用级 `CMailChannelRegistry` 按 `ProjectChannelID` 托管。

## 2. Project

`CProject` 是项目运行单元：

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
info.pResourceLoaderRegistry = projectResourceLoaderRegistry;
info.pMailChannelRegistry = mailChannelRegistry;

iCAX::Project::CProject project(info);
```

`pApplicationContext`、`pProductContext`、`pServiceProvider`、`pMetaRegistry`、`pBehaviourRegistry`、`pResourceLoaderRegistry` 和 `pMailChannelRegistry` 是必填依赖。Project 只使用显式注入的注册表。这样可以保证项目数据、资源加载和行为调度都来自明确的产品/项目上下文。

每个项目拥有：

- 一个 `Repository`，承载 EC 数据。
- 一个 `ResourceLibrary`，承载项目资源。
- 一个 `Universe`，承载 Behaviour 调度器并执行 Behaviour；它不拥有 Repository，也不代表 Project。
- 一个可选 `PDOHub`，承载项目级高频共享内存 PDO 通道。
- 每帧调度和 Repository 事件转发时显式传入 `ApplicationContext`、`ProductContext` 和 `ProjectContext`。
- 一个 `ProjectChannelID`，用于从 `CMailChannelRegistry` 获取该项目自己的前后台邮件通道。
- 一个后台工作线程，按项目自己的帧循环执行 `PreSwapPDO`、邮件处理、`Tick` 和 `PostSwapPDO`。
- 一个 ProjectID，作为项目身份。
- 一个 ProjectChannelID，作为项目通信身份。
- 一个项目角色：`Main` 或 `Transient`。
- 一个可选启动组件名，用于启动时绑定首个 Behaviour。

项目线程由 `Start()` 启动，由 `Stop()` 或 `Close()` 停止：

```cpp
project.SetFrameHandler([](
    iCAX::Project::CProject& project,
    const iCAX::Mail::CMailPostOffice& backendOffice) {
    auto mails = backendOffice.Receive();
    // 在项目线程内处理该项目的命令。
});

project.Start();
project.Stop();
```

项目侧前端邮局可随时低成本获取：

```cpp
auto frontendOffice = project.GetFrontendPostOffice();
```

`Close()` 会停止项目线程、清理 `Universe` / `Repository` / `ResourceLibrary`，并从 `CMailChannelRegistry` 删除项目 channel，使已经发出去的旧邮局失效。

如果创建参数中启用 `bEnablePDOHub`，Project 会根据 `PDOHubCreateInfo` 创建自己的动态 PDOHub：

```cpp
info.bEnablePDOHub = true;
info.PDOHubCreateInfo.nArenaSize = 64ull * 1024ull * 1024ull;
info.PDOHubCreateInfo.nSlotCapacity = 4096;

iCAX::Project::CProject project(info);

auto& pdo = project.PDOHub();
auto arenaName = pdo.GetSharedArenaName();
```

产品或服务可以在运行期调用 `project.PDOHub().AllocateSlot(...)` / `FreeSlot(...)` 分配和释放具体 PDO slot。项目帧循环中，`PreSwapPDO()` 会交换入向 PDO，`PostSwapPDO()` 会交换出向 PDO。`Close()` 会释放项目 PDOHub；前端宿主应在项目关闭消息后停止访问对应 Arena。

## 3. 快速保存

Project 负责快速保存日志的生命周期，但不负责具体项目文件格式。项目文件的完整读写由产品或文件模块完成；Project 只在 Repository 层追加、回放和截断操作日志。

默认日志路径为：

```cpp
project.GetQuickSaveLogPath(); // ProjectPath + ".log"
```

也可以通过 `CProjectCreateInfo::QuickSaveLogPath` 或 `SetQuickSaveLogPath(path)` 指定独立日志路径。

打开项目的标准顺序：

```cpp
const std::string quickSaveMagic = productFile.QuickSaveLogMagic;
const uint32_t quickSaveVersion = productFile.QuickSaveLogVersion;

// 1. 产品/文件模块读取 project.icax，并通过 Database::BeginLoadBaseline/EndLoadBaseline 写入 Repository 基线。
// 2. Project 回放崩溃前追加日志。
project.ReplayQuickSaveLog(quickSaveMagic, quickSaveVersion);

// 3. 项目启动后打开追加日志。
project.OpenQuickSaveLog(false, quickSaveMagic, quickSaveVersion);
```

保存项目文件成功后的标准顺序：

```cpp
// 1. 产品/文件模块把当前 Repository 和资源写入 project.icax。
// 2. 成功后通知 Project 截断旧快速保存日志，并重新开始追加。
project.MarkProjectFileSaved("", quickSaveMagic, quickSaveVersion);
```

快速保存日志的 `magic` 和 `version` 由产品/文件层提供。Project 不定义产品身份，也不维护日志格式升级策略，只把这两个值透传给 Repository。日志打开或回放时会校验头部；`magic` 或 `version` 不匹配时拒绝继续追加或回放。

`MarkProjectFileSaved(newPath, quickSaveMagic, quickSaveVersion)` 可用于 Save As。它会更新 `ProjectPath`；如果没有显式设置 `QuickSaveLogPath`，日志路径会随新的 `ProjectPath` 重新推导为 `newPath + ".log"`。如果已经显式设置日志路径，则继续使用该显式路径。截断后的新日志会重新写入当前的 `magic` 和 `version` 头部。

`Close()` 会先关闭快速保存日志，再清理 Repository，避免项目关闭过程中的实体删除被追加到快速保存日志。

## 4. ProjectRuntime

`IProjectRuntime` 是项目运行时抽象。上层可以通过它获取项目身份、状态和前端邮局，并控制启动、停止和关闭：

```cpp
std::shared_ptr<iCAX::Project::CProject> project = catalog.OpenMainProject("Robot Cell");
auto runtime = iCAX::Project::CreateLocalProjectRuntime(project);

runtime->SetFrameHandler([](
    iCAX::Project::IProjectRuntime& runtime,
    const iCAX::Mail::CMailPostOffice& backendOffice) {
    auto mails = backendOffice.Receive();
    // 在项目运行时内处理该项目的命令。
});

runtime->Start();
auto frontendOffice = runtime->GetFrontendPostOffice();
runtime->Close();
```

当前提供的实现是 `CLocalProjectRuntime`，它包装进程内 `CProject`。如果未来需要每个 Project 独立内存空间，应增加新的进程级实现，而不是让 ProductRuntime 直接依赖 `CProject`。

本地实现可以通过 `GetLocalProject()` 取得被包装的 `CProject`，用于当前进程内命令上下文补齐。进程级实现应返回空指针，项目内部数据访问由 Project Worker 自己处理。

## 5. ProjectCatalog

`CProjectCatalog` 管理一个主项目和若干临时项目：

```cpp
iCAX::Project::CProjectCatalogCreateInfo catalogInfo;
catalogInfo.pMetaRegistry = productMetaRegistry;
catalogInfo.pBehaviourRegistry = productBehaviourRegistry;
catalogInfo.pMailChannelRegistry = mailChannelRegistry;
catalogInfo.ResourceLoaderRegistryFactory = []() {
    return CreateProjectResourceLoaderRegistry();
};

iCAX::Project::CProjectCatalog projectCatalog(catalogInfo);

auto mainProject = projectCatalog.OpenMainProject("Robot Cell", "D:/projects/robot.icax");
auto previewProject = projectCatalog.OpenTransientProject("Import Preview");

auto current = projectCatalog.GetMainProject();
projectCatalog.CloseProject(previewProject->GetProjectID());
projectCatalog.CloseMainProject();
```

`CProjectCatalogCreateInfo` 必须提供 `pApplicationContext`、`pProductContext`、`pServiceProvider`、`pMetaRegistry`、`pBehaviourRegistry`、`pMailChannelRegistry` 和 `ResourceLoaderRegistryFactory`。Catalog 构造阶段即完成依赖校验，不允许创建半有效目录。

`ProjectCatalog` 只负责打开、关闭和查询项目。它不统一驱动所有项目帧循环；项目运行由各自 `Project` 的后台线程完成。

`OpenMainProject` 在已经存在主项目时会失败。是否保存、关闭并替换主项目由更上层命令或业务逻辑决定，framework 不隐式丢弃当前主项目。

## 6. 隔离规则

- 主项目是当前业务编辑对象，只能存在一个。
- 临时项目用于预览、导入、转换和对照，不应承载主 UI 的长期编辑状态。
- 项目是实例级运行对象，不与其他项目共享 Repository、ResourceLibrary、Universe、项目 mail channel、PDOHub 或后台线程。
- Project 必须显式注入 MetaRegistry、BehaviourRegistry、ResourceLoaderRegistry 和 MailChannelRegistry，不读取全局注册表。
- 资源路径相同也只在各自 Project 的 `ResourceLibrary` 内复用，不跨项目共享对象。
- Behaviour 回调不做异常拦截或过滤，运行错误按第一现场暴露；独立 OS 地址空间必须由进程级 `IProjectRuntime` 实现。

