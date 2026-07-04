# ApplicationContext 规格文档

## 1. 定位

`ApplicationContext` 是应用程序级上下文。

它描述当前应用能识别什么、运行在哪些目录、当前应用级配置是什么。它不保存项目数据，不拥有 `Database`，不拥有 `Universe`，不负责主循环。

`ApplicationHost` 在启动时读取配置并构造 `ApplicationContext`，再把它传给 Behaviour、Service、CommandHandler、文件读写模块等需要应用上下文的地方。

## 2. 核心概念

### 2.1 ApplicationDescriptor

应用描述：

```cpp
struct CApplicationDescriptor
{
    std::string AppID;
    std::string AppName;
    std::string AppVersion;
    std::vector<std::string> SupportedProjectMagics;
    std::vector<uint32_t> SupportedProjectVersions;
    std::string DefaultProjectExtension;
};
```

`SupportedProjectMagics` 用于文件模块判断某个项目文件是否属于当前应用可识别格式。它是应用能力描述，不是项目数据。

### 2.2 ApplicationPaths

应用路径：

```cpp
struct CApplicationPaths
{
    std::string InstallDirectory;
    std::string UserConfigDirectory;
    std::string CacheDirectory;
    std::string TempDirectory;
    std::string LogDirectory;
};
```

这些路径属于应用运行环境，不进入 `Database`。

### 2.3 应用级 Settings

应用级配置快照，例如 UI 容器类型、语言、主题、全局插件目录、缓存目录等。

配置数据使用 `Data::PropertyBag` 承载：

```cpp
iCAX::Data::PropertyBag settings;
settings.Set("ui.theme", Variant(std::string("dark")));
auto theme = settings.Get("ui.theme").To<std::string>();
```

### 2.4 ApplicationContext

上下文只读接口：

```cpp
class IApplicationContext
{
public:
    virtual const CApplicationDescriptor& GetDescriptor() const = 0;
    virtual const CApplicationPaths& GetPaths() const = 0;
    virtual iCAX::Data::PropertyBag GetSettings() const = 0;
};
```

### 2.5 ApplicationConfigService

应用配置修改、保存和重载通过 `CApplicationConfigService` 完成：

```cpp
settingsService.SetValue("ui.theme", Variant(std::string("dark")));
settingsService.Save();
settingsService.Reload();
```

`ApplicationContext` 本身不直接读写文件。

## 3. 使用边界

- `ApplicationContext` 可以被 `Behaviour`、`Service`、`CommandHandler`、`ApplicationHost` 依赖。
- `ApplicationContext` 不依赖 `Behaviour`、`Service`、`Database`、`ApplicationHost`。
- 项目作者、创建日期、资源清单、项目业务设置等项目语义数据进入 `Database`。
- 文件 offset、size、chunk index 等物理布局是文件读写过程中的临时数据，不进入 `ApplicationContext`。
