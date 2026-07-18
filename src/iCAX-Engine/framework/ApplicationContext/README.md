# ApplicationContext

`ApplicationContext` 是 framework 层的应用运行上下文项目。它描述应用程序级信息、路径和用户配置，不承载项目数据、EC 数据、资源对象或业务行为。

`ApplicationContext` 可以被 Behaviour、Service、Facades、ApplicationHost 使用；它本身不依赖这些项目。

应用级设置是与具体产品和具体项目都无关的 iCAX 应用程序参数。它跟随当前 iCAX 安装或当前用户环境，不跟随产品，也不跟随项目文件。

典型应用级设置：

- UI 容器类型，例如 ECF、WebView、CEF、Qt 或 WPF。
- 应用安装路径、配置路径、缓存路径、临时路径和日志路径。
- 全局语言、主题和日志级别。
- 全局插件搜索目录。
- 应用级网络、更新、诊断和崩溃转储配置。

不应放入应用级设置：

- 某个产品自己的业务默认参数。
- 最近打开项目这类产品相关数据。
- 跟项目文件一起保存和打开的项目参数。
- 工件、刀路、图层、运动规划等项目业务对象。

## 目录结构

- `ApplicationDescriptor.*`：应用描述、支持的项目 magic 和版本。
- `ApplicationPaths.h`：应用安装、配置、缓存、临时和日志目录。
- `ApplicationContext.*` / `IApplicationContext.h`：上下文只读接口和默认实现，内部直接持有应用级 `PropertyBag`。
- `IApplicationConfigStore.h` / `FileApplicationConfigStore.*`：配置读写抽象与文件实现。
- `ApplicationConfigService.*`：修改、保存、重载应用配置的服务类。
- `ApplicationContextExport.h`：DLL 导出宏。
