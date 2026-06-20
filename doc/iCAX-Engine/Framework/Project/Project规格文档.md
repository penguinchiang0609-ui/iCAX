# Project 规格文档

## 1. 定位

`Project` 是 framework 层的项目工程。

它只表达项目实例，不表达应用类型、业务类型或模块目录。运行态只允许一个主项目。为了支持预览、导入和转换，可以同时打开若干临时项目。

每个 `Project` 独占自己的 `Repository`、`ResourceLibrary`、`UniverseContext`、`Universe`、项目 mail channel 和后台工作线程。底层 `CMailChannel` 由应用级 `IMailChannelService` 按 `ProjectMailID` 托管。

## 2. Project

`CProject` 是项目运行单元：

```cpp
iCAX::Project::CProjectCreateInfo info;
info.ProjectName = "Robot Cell";
info.ProjectPath = "D:/projects/robot.icax";
info.StartupComponent = "StartupComponent";
info.pMailChannelService = mailChannelService;

iCAX::Project::CProject project(info);
```

每个项目拥有：

- 一个 `Repository`，承载 EC 数据。
- 一个 `ResourceLibrary`，承载项目资源。
- 一个 `UniverseContext`，把当前项目的 Repository、计时器和应用设置传给 Behaviour。
- 一个 `Universe`，承载 Behaviour 调度器并执行 Behaviour；它不拥有 Repository，也不代表 Project。
- 一个 `ProjectMailID`，用于从 `IMailChannelService` 获取该项目自己的前后台邮件通道。
- 一个后台工作线程，按项目自己的帧循环执行 `PreSwapPDO`、邮件处理、`Tick` 和 `PostSwapPDO`。
- 一个 ProjectID，作为项目身份。
- 一个 ProjectMailID，作为项目通信身份。
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

`Close()` 会停止项目线程、清理 `Universe` / `UniverseContext` / `Repository` / `ResourceLibrary`，并从 `IMailChannelService` 删除项目 channel，使已经发出去的旧邮局失效。

## 3. ProjectRuntime

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

## 4. ProjectCatalog

`CProjectCatalog` 管理一个主项目和若干临时项目：

```cpp
iCAX::Project::CProjectCatalogCreateInfo catalogInfo;
catalogInfo.pMailChannelService = mailChannelService;

iCAX::Project::CProjectCatalog projectCatalog(catalogInfo);

auto mainProject = projectCatalog.OpenMainProject("Robot Cell", "D:/projects/robot.icax");
auto previewProject = projectCatalog.OpenTransientProject("Import Preview");

auto current = projectCatalog.GetMainProject();
projectCatalog.CloseProject(previewProject->GetProjectID());
projectCatalog.CloseMainProject();
```

`ProjectCatalog` 只负责打开、关闭和查询项目。它不统一驱动所有项目帧循环；项目运行由各自 `Project` 的后台线程完成。

`OpenMainProject` 在已经存在主项目时会失败。是否保存、关闭并替换主项目由更上层命令或业务逻辑决定，framework 不隐式丢弃当前主项目。

## 5. 隔离规则

- 主项目是当前业务编辑对象，只能存在一个。
- 临时项目用于预览、导入、转换和对照，不应承载主 UI 的长期编辑状态。
- 项目是实例级运行对象，不与其他项目共享 Repository、ResourceLibrary、UniverseContext、Universe、项目 mail channel 或后台线程。
- 资源路径相同也只在各自 Project 的 `ResourceLibrary` 内复用，不跨项目共享对象。
- 当前进程内实现可以隔离标准 C++ 异常；独立 OS 地址空间必须由进程级 `IProjectRuntime` 实现。
