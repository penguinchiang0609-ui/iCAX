# ApplicationHost 规格文档

## 1. 定位

`ApplicationHost` 是 C++ 后台应用宿主。它负责构造 `ApplicationContext`，并把 `Database`、`Behaviour`、`Services`、`Mailbox`、`PDO` 和插件装配成一个可运行的后台实例。

`ApplicationHost` 不表达业务领域模型，也不替代 `Database`、`Behaviour` 或 `CommandHandler`。

## 2. 生命周期

### 2.1 SetConfig

设置宿主需要读取的配置文件路径。

```cpp
iCAX::ApplicationHost::ApplicationHostConfig config;
config.strApplicationSettingsPath = "Setting/Application.Setting";
config.strPluginManifestPath = "Setting/Plugins.Setting";
config.strStartupPath = "Setting/Startup.Setting";
config.Descriptor.AppID = "icax";
config.Descriptor.AppName = "iCAX";
config.Paths.UserConfigDirectory = "Setting";

iCAX::ApplicationHost::CApplicationHost host;
host.SetConfig(config);
```

### 2.2 Load

加载应用设置、创建 `ApplicationContext`、创建 Repository、创建 Universe、解析插件配置、加载插件 DLL，并根据 Startup 配置绑定入口 Behaviour。

```cpp
iCAX::ApplicationHost::CApplicationHost host;
host.Load();
```

### 2.3 MainLoop

执行一帧后台循环：

```text
PreSwapPDO
Receive backend mails
Universe Tick
PostSwapPDO
```

```cpp
host.MainLoop();
```

### 2.4 Unload

卸载服务、清理 Universe 和 Repository。

```cpp
host.Unload();
```

## 3. 对外访问

`ApplicationHost` 当前提供 Repository 和 Universe 访问入口：

```cpp
auto& repository = host.GetRepository();
auto& universe = host.GetUniverse();
auto& applicationContext = host.GetApplicationContext();
```

## 4. 配置文件格式

`ApplicationHost` 使用 `Data::VariantSerializer` 读取结构化配置。

应用设置文件是一个对象：

```text
{"__variant_type":"Object","value":{}}
```

插件清单包含 `plugins` 数组，每个插件项包含 `name`、`componentPath`、`behaviourPath` 和 `servicePath`。

启动项包含 `firstComponent` 字段。

## 5. 依赖边界

`ApplicationHost` 可以依赖：

- `ApplicationContext`
- `Database`
- `Behaviour`
- `Services`
- `Mailbox`
- `PDO`

这些项目不应反向依赖 `ApplicationHost`。

## 6. 当前限制

- 邮件命令分发尚未接入 `CommandHandler`。
- 应用设置通过 `ApplicationContext` 的 `IApplicationConfigStore` 读取。
- 插件清单和启动项使用 `Data::VariantSerializer` 格式。
