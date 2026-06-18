# Project 规格文档

## 1. 定位

`Project` 是 framework 层的产品与项目会话工程。

它区分两个概念：

- `ProductDefinition`：产品类型，如三维五轴、平切、焊接。
- `ProjectSession`：一个已打开的项目实例。

同一个进程可以同时加载多个产品类型，也可以同时打开多个项目实例。每个 `ProjectSession` 独占自己的 `Repository`、`ResourceLibrary`、`UniverseContext`、`Universe`、`MailChannel` 和后台工作线程。

## 2. ProductDefinition

产品定义描述产品类型，不保存项目数据：

```cpp
iCAX::Project::CProductDefinition product;
product.ProductID = "icax.product.five-axis";
product.DisplayName = "三维五轴";
product.StartupComponent = "FiveAxisStartupComponent";
product.FrontendEntry = "frontend/index.html";
product.ProtocolPath = "protocol";
```

产品 ID 必须显式填写，不从显示名或目录名推断。

## 3. ProductCatalog

`CProductCatalog` 管理产品定义：

```cpp
iCAX::Project::CProductCatalog catalog;
catalog.Set(product);

auto fiveAxis = catalog.Require("icax.product.five-axis");
auto allProducts = catalog.GetDefinitions();
```

产品 manifest 是普通 UTF-8 JSON 文件，使用 `productId` 字段：

```json
{
  "productId": "icax.product.flat-cut",
  "displayName": "平切",
  "backend": {
    "components": [],
    "behaviours": [],
    "services": [],
    "commands": []
  },
  "frontend": {
    "entry": "frontend/index.html"
  },
  "protocol": {
    "path": "protocol"
  },
  "startup": {
    "firstComponent": "FlatCutStartupComponent"
  }
}
```

`backend` 下的模块路径、`frontend.entry` 和 `protocol.path` 都按 manifest 所在目录解析为规范路径。模块路径应指向可加载的动态库文件。

## 4. ProjectSession

`CProjectSession` 是项目运行单元：

```cpp
auto project = projectManager.OpenProject("icax.product.five-axis", "Robot Cell");

auto& database = project->Database();
auto& universe = project->Universe();
auto& resources = project->Resources();

auto model = project->Resources().Load<ModelResource>("D:/assets/robot.fbx");
```

每个项目会话拥有：

- 一个 `Repository`，承载 EC 数据。
- 一个 `ResourceLibrary`，承载项目资源。
- 一个 `UniverseContext`，把当前项目的 Repository、计时器和应用设置传给 Behaviour。
- 一个 `Universe`，承载 Behaviour 调度器并执行 Behaviour；它不拥有 Repository，也不代表 Project。
- 一个 `MailChannel`，承载该项目自己的前后台邮件。
- 一个后台工作线程，按项目自己的帧循环执行 `PreSwapPDO`、邮件处理、`Tick` 和 `PostSwapPDO`。
- 一个 ProjectID，作为项目身份。

项目线程由 `Start()` 启动，由 `Stop()` 或 `Close()` 停止：

```cpp
project->SetFrameHandler([](
    iCAX::Project::CProjectSession& project,
    const iCAX::Mail::CMailPostOffice& backendOffice) {
    auto mails = backendOffice.Receive();
    // 在项目线程内处理该项目的命令。
});

project->Start();
project->Stop();
```

项目侧前端邮局可随时低成本获取：

```cpp
auto frontendOffice = project->GetFrontendPostOffice();
```

`Close()` 会停止项目线程、清理 `Universe` / `UniverseContext` / `Repository` / `ResourceLibrary`，并重置项目 `MailChannel`，使已经发出去的旧邮局失效。

## 5. ProjectManager

`CProjectManager` 管理多个项目实例：

```cpp
iCAX::Project::CProjectManager projectManager;
projectManager.Products().Set(fiveAxisProduct);
projectManager.Products().Set(flatCutProduct);

auto fiveAxis = projectManager.OpenProject("icax.product.five-axis", "A");
auto flatCut = projectManager.OpenProject("icax.product.flat-cut", "B");

projectManager.SetActiveProject(flatCut->GetProjectID());
projectManager.CloseProject(fiveAxis->GetProjectID());
```

`ProjectManager` 只负责打开、关闭、查询和设置活动项目。它不统一驱动所有项目帧循环；项目运行由各自 `ProjectSession` 的后台线程完成。

## 6. 隔离规则

- 产品定义是类型级信息，可以被多个项目共享。
- 项目会话是实例级信息，不与其他项目共享 Repository、ResourceLibrary、UniverseContext、Universe、MailChannel 或后台线程。
- 同一产品可以多开多个项目。
- 不同产品可以同时打开项目。
- 资源路径相同也只在各自 Project 的 `ResourceLibrary` 内复用，不跨项目共享对象。
