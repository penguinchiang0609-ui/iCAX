# ApplicationHost 规格文档

## 1. 定位

`ApplicationHost` 是应用级后台宿主，是前端启动后最先连接的稳定入口。

它负责：

- 构造 `ApplicationContext`。
- 创建应用级服务容器。
- 直接持有 `CMailChannelRegistry`，并托管应用、产品和项目 mail channel。
- 维护支持的产品定义列表。
- 启动、停止和查询 `ProductRuntime`。
- 提供应用级 mailbox 和应用级内置命令。
- 发布宿主生命周期事件。

`ApplicationHost` 不直接维护 `ProjectCatalog`，也不直接打开项目。项目目录和项目实例归属 `ProductRuntime`。

## 2. 配置

```cpp
iCAX::ApplicationHost::ApplicationHostConfig config;
config.strApplicationSettingsPath = "Setting/Application.Setting";
config.Descriptor.AppID = "icax";
config.Descriptor.AppName = "iCAX";

iCAX::Product::CProductDefinition robot;
robot.ProductID = "robot";
robot.ProductName = "Robot";
robot.FrontendEntry = "ui://robot/main";
robot.ProjectFile.Magic = "ICAX_ROBOT";
robot.ProjectFile.FormatVersion = "1.0";
robot.ProjectFile.FileExtensions.push_back(".robot");
robot.ProjectFile.MagicOffset = 0;
robot.ProjectFile.ProbeBytes = 256;
robot.Modules.ComponentModules.push_back("bin/RobotComponent.dll");
robot.Modules.BehaviourModules.push_back("bin/RobotBehaviour.dll");
robot.Modules.ServiceModules.push_back("bin/RobotService.dll");
robot.Modules.CommandModules.push_back("bin/RobotCommand.dll");
robot.DefaultProjectStartupComponent = "RobotStartupComponent";

config.Products.push_back(robot);
config.StartupProductID = "robot"; // 可选，只启动产品，不自动打开项目

iCAX::ApplicationHost::CApplicationHost host;
host.SetConfig(config);
```

`ApplicationHostConfig` 包含：

- `strApplicationSettingsPath`：应用级配置文件。
- `Descriptor`：应用描述。
- `Paths`：应用路径。
- `Products`：宿主支持的产品定义列表。
- `StartupProductID`：宿主启动时自动启动的产品，可为空。
- `nFrameIntervalMilliseconds`：应用宿主线程帧间隔，只用于应用级与产品级消息轮询。

## 3. 生命周期

`Start` 启动后台工作线程，并等待宿主进入 `Running` 或 `Faulted`。

加载阶段：

```text
LoadApplicationSettings
Create ApplicationContext
Create ServiceProvider
Create MailChannelRegistry
Create application mail channel
Bind ApplicationContext runtime capabilities
Register built-in application commands
Prepare application command context
Start configured startup product?
```

`Stop` 请求后台线程停止，并等待线程退出。

卸载阶段会停止所有已启动产品、删除应用级 mail channel、卸载已创建的服务实例，并释放应用上下文。

## 4. 应用级命令

应用级命令通过 `GetApplicationFrontendPostOffice()` 发送，payload 使用 `VariantSerializer` 编码的 `ObjectMap`。

- `kAppGetStateCommand` / `App.GetState`：查询宿主状态、产品清单和已启动产品。
- `kAppListProductsCommand` / `App.ListProducts`：查询产品清单，响应结构与 `App.GetState` 一致。
- `kAppStartProductCommand` / `App.StartProduct`：启动产品。payload 可包含 `productId`。当只有一个产品时可省略。
- `kAppStopProductCommand` / `App.StopProduct`：停止产品。payload 必须包含 `productId`。
- `kAppResolveProjectFileCommand` / `App.ResolveProjectFile`：根据项目文件快速识别产品。
- `kAppOpenProjectFileCommand` / `App.OpenProjectFile`：根据项目文件识别产品、启动产品并打开 ProjectCatalog。

同一 `productId` 的 `StartProduct` 和 `StopProduct` 严格串行。产品停止期间，新的 `StartProduct` 会等待旧 `ProductRuntime` 完成停止并从宿主表中移除后，再创建新的运行时实例。

`App.StartProduct` 请求示例：

```cpp
iCAX::Data::ObjectMap payload;
payload["productId"] = std::string("robot");

auto bytes = iCAX::ApplicationHost::EncodeApplicationHostPayload(
    iCAX::Data::Variant(payload));
```

应用级状态响应：

```text
applicationChannelId: uuid
state: string
phase: string
productCount: unsigned_long_long
products: array<object>
runningProductCount: unsigned_long_long
faultMessage: string
```

产品对象包含：

```text
productId: string
productName: string
productVersion: string
frontendEntry: string
defaultProjectStartupComponent: string
projectFile: object
isStarted: bool
productChannelId: uuid
recentProjects: array<object>
runtime: object? // 已启动时存在，内容为 ProductRuntime 状态
```

`projectFile` 包含：

```text
magic: string
formatVersion: string
fileExtensions: array<string>
magicOffset: unsigned_long_long
probeBytes: unsigned_long_long
```

`recentProjects` 来自产品运行数据，产品未启动时为空数组；产品启动后由 `ProductRuntime` 从 ProductData 中加载。最近项目对象包含：

```text
path: string
displayName: string
lastOpenedTime: string
```

`App.StartProduct` 响应：

```text
applicationChannelId: uuid
product: object
state: object
```

`App.ResolveProjectFile` 请求示例：

```cpp
iCAX::Data::ObjectMap payload;
payload["projectPath"] = std::string("D:/projects/RobotCell.robot");
```

响应：

```text
applicationChannelId: uuid
resolve: object
```

`resolve` 包含：

```text
projectPath: string
status: string              // Resolved / NotFound
productId: string
candidateProductIds: array<string>
matchedByMagic: bool
```

`App.OpenProjectFile` 请求示例：

```cpp
iCAX::Data::ObjectMap payload;
payload["projectPath"] = std::string("D:/projects/RobotCell.robot");
```

响应：

```text
applicationChannelId: uuid
resolve: object
product: object
catalog: object
state: object
```

`App.OpenProjectFile` 只接收文件路径和可选显示名称，不接收产品 ID。ApplicationHost 只用文件头 magic 识别产品。magic 不能为空，且必须在 ApplicationHost 的产品定义列表中全局唯一。扩展名只用于文件选择器和展示，不参与产品识别。若 magic 未命中则返回 `NotFound`；若出现多个产品命中同一个文件，则视为产品定义错误。

## 5. 前端启动流

```text
Frontend
  -> ApplicationHost.Start()
  -> ApplicationHost.GetApplicationFrontendPostOffice()
  -> App.GetState / App.ListProducts
  -> App.OpenProjectFile(filePath)?       // 双击文件启动时
  -> App.StartProduct(productId)
  <- productChannelId, frontendEntry, recentProjects
  -> product frontend initializes by frontendEntry
  -> Product mailbox handles Product.OpenProjectCatalog
  <- catalogId, main projectId
  -> Project mailbox handles project commands
```

## 6. 依赖边界

`ApplicationHost` 可以依赖：

- `ApplicationContext`
- `CommandHandler`
- `Mailbox`
- `Product`
- `Services`
- `Database`
- `Behaviour`
- `Resources`

`ApplicationHost` 不应被 `Product`、`Project`、`Database`、`Behaviour`、`Resources` 或业务模块反向依赖。

