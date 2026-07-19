# Product 规格文档

## 1. 定位

`Product` 是产品级运行时。它位于 `ApplicationRuntime` 和 `Project` 之间。

`ApplicationRuntime` 负责列出和启动产品；`ProductRuntime` 负责产品级 Facade、产品模块加载、最近项目列表和 ProjectCatalog 生命周期。产品级 Facade 的底层 channel 由应用级 `CFacadeChannelRegistry` 按 `productChannelId` 托管。产品 UI 入口由 `FrontendEntry` 描述，但后端不绑定 H5、WPF 或 Qt。

## 2. 产品定义

```cpp
iCAX::Product::CProductDefinition product;
product.ProductID = "robot";
product.ProductName = "Robot";
product.ProductVersion = "1.0";
product.FrontendEntry = "ui://robot/main";
product.DefaultProjectStartupComponent = "RobotStartupComponent";
product.ProjectFile.Magic = "ICAX_ROBOT";
product.ProjectFile.FormatVersion = "1.0";
product.ProjectFile.FileExtensions.push_back(".robot");
product.ProjectFile.MagicOffset = 0;
product.ProjectFile.ProbeBytes = 256;
product.Modules.ComponentModules.push_back("bin/RobotComponent.dll");
product.Modules.BehaviourModules.push_back("bin/RobotBehaviour.dll");
product.Modules.ServiceModules.push_back("bin/RobotService.dll");
product.Modules.FacadeModules.push_back("bin/RobotCommand.dll");
```

`CProductDefinition` 只表达产品静态定义，包含产品 ID、名称、版本、前端入口、默认项目启动组件、项目文件格式和产品模块路径。最近项目、用户偏好等会变化的数据不放在产品定义中。

`ProjectFile` 描述产品可识别的项目文件格式：

```text
magic: string                    // 必填，全应用唯一
formatVersion: string
fileExtensions: array<string>    // 仅用于文件选择器和展示，不用于识别产品
magicOffset: unsigned_long_long
probeBytes: unsigned_long_long
```

`magic` 是识别项目文件归属产品的唯一依据，不能为空，且同一 ApplicationRuntime 下不能重复。`magicOffset` 表示 magic 在文件头中的偏移；`probeBytes` 表示 ApplicationRuntime 为识别产品最多读取的探测字节数。ApplicationRuntime 只做轻量文件探测，不解析完整项目文件。

## 3. 产品 Manifest

`Product` 模块提供产品 manifest loader，用于把 `src/apps/<product-id>/product.manifest.json` 转换为 `CProductDefinition`。

接口：

```cpp
auto manifest = iCAX::Product::LoadProductManifest("D:/iCAX/src/apps/robot/product.manifest.json");
auto products = iCAX::Product::LoadProductDefinitions("D:/iCAX/src/apps");
```

字段映射：

```text
productId                         -> CProductDefinition.ProductID
productName                       -> CProductDefinition.ProductName
productVersion                    -> CProductDefinition.ProductVersion
projectFile.magic                 -> CProductDefinition.ProjectFile.Magic
projectFile.formatVersion         -> CProductDefinition.ProjectFile.FormatVersion
projectFile.quickSaveLogMagic     -> CProductDefinition.ProjectFile.QuickSaveLogMagic
projectFile.quickSaveLogVersion   -> CProductDefinition.ProjectFile.QuickSaveLogVersion
projectFile.fileExtensions        -> CProductDefinition.ProjectFile.FileExtensions
projectFile.magicOffset           -> CProductDefinition.ProjectFile.MagicOffset
projectFile.probeBytes            -> CProductDefinition.ProjectFile.ProbeBytes
backend.startupComponent          -> CProductDefinition.DefaultProjectStartupComponent
backend.modules.components        -> CProductDefinition.Modules.ComponentModules
backend.modules.behaviours        -> CProductDefinition.Modules.BehaviourModules
backend.modules.services          -> CProductDefinition.Modules.ServiceModules
backend.modules.commands          -> CProductDefinition.Modules.FacadeModules
webpage.entry                     -> CProductDefinition.FrontendEntry
```

模块路径按 manifest 所在产品目录解析为规范化路径。`UIContainer` 不解析 manifest；启动层负责调用上述接口并把结果写入 `ApplicationRuntimeConfig.Products`。

## 4. 产品数据

`ProductData` 是产品运行数据，归属产品运行时，不属于应用级配置，也不属于产品静态定义。

```cpp
iCAX::Product::CProductData data;
data.RecentProjects.push_back({
    "D:/projects/robot.robot",
    "Robot Cell",
    "2026-06-20T00:00:00Z"});
data.Settings.Set("theme", std::string("dark"));
```

`CProductData` 当前包含：

```text
recentProjects: array<object>
userSettings: object
```

最近项目对象包含：

```text
path: string
displayName: string
lastOpenedTime: string
```

默认文件存储由 `CFileProductDataStore` 提供，路径为：

```text
{UserConfigDirectory}/Products/{productId}/Product.Data
```

如果 `UserConfigDirectory` 为空，则使用 `Setting/Products/{productId}/Product.Data`。`ProductRuntime::Start()` 会加载 ProductData，打开真实文件路径的项目后会更新最近项目；`memory://` 等非文件路径不会写入最近项目。

## 5. 产品运行时

```cpp
auto productRuntime = host.StartProduct("robot");
auto productOffice = host.GetProductFrontendFacadeEndpoint("robot");
```

直接调用方式：

```cpp
auto catalog = productRuntime->OpenProjectCatalog(
    "Robot Catalog",
    "D:/projects/robot.icax",
    "Robot Cell",
    "D:/projects/robot.icax");

auto project = catalog->GetMainProject();
auto mainScene = project->GetMainScene();
auto sceneOffice = productRuntime->GetSceneFrontendFacadeEndpoint(project->GetProjectID(), mainScene->GetSceneID());
```

`ProductRuntime` 打开 ProjectCatalog 时使用产品定义中的 `DefaultProjectStartupComponent`，上层打开项目时不再传入 startup component。

`ProductRuntime` 内部通过 `IProjectRuntime` 管理项目运行实例。当前实现使用 `CProjectRuntime` 包装进程内 `CProject`；上层只应依赖项目 ID、状态和主 Scene Facade，不应假设项目一定和 ProductRuntime 位于同一个地址空间。

产品运行时拥有自己的产品级注册表：

```text
ProductRuntime
  Product MetaRegistry
  Product BehaviourRegistry
  Product ResourceLoaderRegistry
  Product FacadeRegistry
```

产品模块加载后，`ComponentMeta`、`Behaviour`、`ResourceLoader` 和 `Facades` 的宏注册动作只回放到当前产品运行时。Service 注册动作回放到 Application 级 `CServiceProvider`，因为服务是应用级共享能力。

每个 Scene 创建时会得到自己的 `ResourceLoaderRegistry`，该 registry 从当前产品模块重新回放 ResourceLoader 注册动作。这样 loader 实例、资源对象和资源池都按 Scene 隔离。

产品 DLL 进程内只加载一次。由于自动注册宏只提供注册、不提供注销，模块加载后按进程常驻处理；停止产品只关闭 Facade、ProjectCatalog、ProjectRuntime 和产品数据，不卸载 DLL。

## 6. 产品级命令

- `kProductGetStateMethodCode` / `Product.GetState`：查询产品状态和 ProjectCatalog 列表。
- `kProductListProjectCatalogsMethodCode` / `Product.ListProjectCatalogs`：查询 ProjectCatalog 列表，响应与 `Product.GetState` 一致。
- `kProductOpenProjectCatalogMethodCode` / `Product.OpenProjectCatalog`：打开 ProjectCatalog 并创建主项目。
- `kProductCloseProjectCatalogMethodCode` / `Product.CloseProjectCatalog`：关闭指定 ProjectCatalog。

`Product.OpenProjectCatalog` 请求 payload：

```cpp
iCAX::Data::ObjectMap payload;
payload["catalogName"] = std::string("Robot Catalog");
payload["catalogPath"] = std::string("D:/projects/robot.icax");
payload["projectName"] = std::string("Robot Cell");
payload["projectPath"] = std::string("D:/projects/robot.icax");

auto bytes = iCAX::Product::EncodeProductPayload(iCAX::Data::Variant(payload));
```

响应：

```text
productId: string
productChannelId: uuid
catalog: object
state: object
```

`state` 中包含：

```text
productId: string
productName: string
productVersion: string
frontendEntry: string
productChannelId: uuid
isStarted: bool
recentProjects: array<object>
projectFile: object
catalogCount: unsigned_long_long
catalogs: array<object>
```

其中 `recentProjects` 是对象数组：

```text
path: string
displayName: string
lastOpenedTime: string
```

`projectFile` 包含：

```text
magic: string
formatVersion: string
fileExtensions: array<string>
magicOffset: unsigned_long_long
probeBytes: unsigned_long_long
```

项目对象包含：

```text
projectId: uuid
mainSceneId: uuid
mainSceneChannelId: uuid
projectName: string
projectPath: string
startupComponent: string
state: string
isOpen: bool
isRunning: bool
faultMessage: string
mainScene: object
sceneCount: unsigned_long_long
scenes: array<object>
```

Scene 对象包含：

```text
sceneId: uuid
sceneChannelId: uuid
parentSceneId: uuid
sceneName: string
role: string
state: string
isMainScene: bool
isTransientScene: bool
isOpen: bool
isRunning: bool
startupComponent: string
faultMessage: string
pdo: object
undoRedo: object
```

## 6. 线程模型

`ProductRuntime` 创建自己的产品级工作线程，并由该线程轮询产品级 Facade。

每个 `Project` 会创建主 Scene；主 Scene 自己创建工作线程，并在 Scene 线程内处理 Scene Facade、Repository 事件和 Behaviour Tick。Scene channel 由 `CFacadeChannelRegistry` 托管，Scene 关闭时删除。

Behaviour 回调不做异常拦截或过滤，运行错误按第一现场暴露。若要隔离访问冲突、堆破坏或整个项目崩溃，需要将 `IProjectRuntime` 实现替换为每 Project 一个 Worker 进程。

