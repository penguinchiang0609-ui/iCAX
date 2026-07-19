# ApplicationContext

`ApplicationContext` 是 framework 层的应用作用域环境项目。它管理应用程序级描述、路径、用户配置、配置存储和应用服务环境；不承载项目数据、EC 数据或 Scene 资源。

`ApplicationRuntime` 创建并销毁 `ApplicationContext`，负责线程、调度和生命周期。Behaviour、Service、Product、Project、Scene 和普通 Facade 只获得 `const IApplicationContext` 视图；修改配置或应用服务环境必须回到 ApplicationRuntime 工作线程中的应用级 Facade。

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
- `ApplicationContext.*` / `IApplicationContext.h`：上下文只读接口和默认实现，内部持有应用级 `PropertyBag`、配置存储和 `CServiceProvider`。
- `IApplicationConfigStore.h` / `FileApplicationConfigStore.*`：配置读写抽象与文件实现。
- `ApplicationConfigService.*`：ApplicationRuntime 应用级 Facade 使用的配置写入器；实际配置状态和存储仍由 Context 管理。
- `ApplicationContextExport.h`：DLL 导出宏。
