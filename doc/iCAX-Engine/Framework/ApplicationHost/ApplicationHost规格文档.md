# ApplicationHost 规格文档

## 1. 定位

`ApplicationHost` 是 C++ 后台工作区宿主。

它负责构造 `ApplicationContext`，加载产品目录，创建 `ProjectManager`，接入应用级 `Mailbox` 和 `CommandHandler`，并管理多个已打开项目。

`ApplicationHost` 不再代表某一个产品，也不直接拥有单个 `Repository` 或单个 `Universe`。这些对象由 `ProjectSession` 装配，其中 `Universe` 只承载行为结构，不承载项目数据。

## 2. 配置

```cpp
iCAX::ApplicationHost::ApplicationHostConfig config;
config.strApplicationSettingsPath = "Setting/Application.Setting";
config.strProductCatalogPath = "Products";
config.Descriptor.AppID = "icax";
config.Descriptor.AppName = "iCAX";

config.StartupProjects.push_back({
    "icax.product.five-axis",
    "Robot Cell",
    "D:/projects/robot.icax"
});

iCAX::ApplicationHost::CApplicationHost host;
host.SetConfig(config);
```

`ApplicationHostConfig` 包含：

- `strApplicationSettingsPath`：应用级配置文件。
- `Descriptor`：应用描述。
- `Paths`：应用路径。
- `strProductCatalogPath`：产品目录路径，目录下每个产品目录可包含 `manifest.json`。
- `ProductManifestPaths`：显式产品 manifest 路径列表。
- `StartupProjects`：宿主启动时自动打开的项目列表。
- `nFrameIntervalMilliseconds`：应用宿主线程帧间隔，只用于应用级消息处理，不驱动项目帧。

## 3. 生命周期

### 3.1 Start

`Start` 会启动后台工作线程，并等待宿主进入 `Running` 或 `Faulted`。

加载阶段执行：

```text
LoadApplicationSettings
Create ApplicationContext
Resolve IMailPostOfficeService
Create ProjectManager
Load product manifests
Load product modules
Register products into ProductCatalog
Prepare application command context
Open startup projects
```

### 3.2 Stop

`Stop` 请求后台线程停止，并等待线程退出。

卸载阶段会关闭所有 `ProjectSession`，清空应用级邮件通道，卸载已创建的服务实例，并释放应用上下文。

## 4. 后台循环

后台线程按固定帧间隔循环：

```text
Dispatch application mailbox
```

应用宿主线程只处理应用级消息，不再统一 Tick 所有项目。

每个 `ProjectSession` 有自己的后台线程和自己的项目级 `MailChannel`。项目线程的循环为：

```text
ProjectSession.PreSwapPDO
Dispatch project mailbox
ProjectSession.Tick
ProjectSession.PostSwapPDO
```

因此一个项目阻塞时不会阻塞其他项目的行为运行和项目邮件处理。

## 5. 项目 API

```cpp
auto& projectManager = host.GetProjectManager();
auto project = host.OpenProject("icax.product.flat-cut", "Sheet A");

auto& database = project->Database();
auto& resources = project->Resources();
auto& universe = project->Universe();

host.CloseProject(project->GetProjectID());
```

`ApplicationHost` 不提供全局 `Repository` 或全局 `Universe`。多项目场景下，数据和行为运行单元必须从明确的 `ProjectSession` 获取：

```cpp
auto project = host.OpenProject("icax.product.flat-cut", "Sheet A");
auto& repository = project->Database();
auto& universe = project->Universe();
auto& resources = project->Resources();
```

## 6. Mailbox 命令接入

`ApplicationHost` 有两类通信通道：

- 应用级通道：通过 `GetApplicationMailID()` 标识，用于打开项目、列项目、应用设置等不属于某个项目的命令。
- 项目级通道：归属具体 `ProjectSession`，用于操作指定项目。

```cpp
auto appOffice = host.GetApplicationFrontendPostOffice();
auto projectOffice = host.GetProjectFrontendPostOffice(project->GetProjectID());
```

`GetProjectFrontendPostOffice(projectId)` 会先定位 `ProjectSession`，再从该项目自己的 `MailChannel` 获取前端邮局。项目关闭后，旧项目邮局失效。

宿主收到 Mail 后转换为 `CCommandRequest`：

- `Mail.Header.nMailId` -> `CCommandRequest::nCommandID`
- `Mail.Header.nOriginId` -> `CCommandRequest::nOriginID`
- `Mail.Header.nTypeCode` -> `CCommandRequest::nTypeCode`
- `Mail.Payload` -> `CCommandRequest::Payload`

项目级命令的 `CommandContext` 会包含：

- `IApplicationContext`
- `CProjectManager`
- 当前 `CProjectSession`
- 当前项目的 `IRepository`
- 当前项目的 `IUniverse`，用于访问行为运行结构
- 当前项目的 `CResourceLibrary`
- `IMailPostOfficeService`

应用级命令不包含当前项目依赖。

## 7. 事件订阅

`ApplicationHost` 支持订阅生命周期事件：

- `Starting`
- `Started`
- `Stopping`
- `Stopped`
- `Faulted`

事件携带当前状态、运行阶段、停止原因、消息和异常。

## 8. 依赖边界

`ApplicationHost` 可以依赖：

- `ApplicationContext`
- `CommandHandler`
- `Project`
- `Services`
- `Mailbox`

`ApplicationHost` 不应被 `Project`、`Database`、`Behaviour`、`Resources` 或产品代码反向依赖。
