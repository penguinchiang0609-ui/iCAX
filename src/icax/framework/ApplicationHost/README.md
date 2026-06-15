# ApplicationHost

`ApplicationHost` 是 icax 后台应用宿主工程，负责构造 ApplicationContext、插件加载、服务装配、Universe/Repository 初始化和主循环入口。

## 目录结构

- `ApplicationHost.vcxproj`：应用宿主工程。
- `ApplicationHost.h` / `ApplicationHost.cpp`：后台应用宿主入口，负责装配和驱动主循环。
- `ApplicationHostConfig.h`：应用宿主配置路径、应用描述和应用路径。
- `PluginMeta.h`：插件描述信息。
- `Startup.h`：启动组件配置。
