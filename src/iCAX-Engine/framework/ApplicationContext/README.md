# ApplicationContext

`ApplicationContext` 是 framework 层的应用运行上下文项目。它描述应用程序级信息、路径和用户配置，不承载项目数据、EC 数据、资源对象或业务行为。

`ApplicationContext` 可以被 Behaviour、Service、CommandHandler、ApplicationHost 使用；它本身不依赖这些项目。

## 目录结构

- `ApplicationDescriptor.*`：应用描述、支持的项目 magic 和版本。
- `ApplicationPaths.h`：应用安装、配置、缓存、临时和日志目录。
- `ApplicationSettings.*`：应用级配置数据快照。
- `ApplicationContext.*` / `IApplicationContext.h`：上下文只读接口和默认实现。
- `IApplicationConfigStore.h` / `FileApplicationConfigStore.*`：配置读写抽象与文件实现。
- `ApplicationSettingsService.*`：修改、保存、重载应用配置的服务类。
- `ApplicationContextExport.h`：DLL 导出宏。
