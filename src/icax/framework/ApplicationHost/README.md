# ApplicationHost

`ApplicationHost` 是 icax 后台工作区宿主工程，负责构造 ApplicationContext、加载产品目录、装配服务、管理多项目会话、驱动应用级后台线程和发布生命周期事件。

项目运行不由 ApplicationHost 统一 Tick。每个 `ProjectSession` 自己持有项目线程、项目邮箱、Repository、ResourceLibrary 和 Universe。

## 目录结构

- `ApplicationHost.vcxproj`：应用宿主工程。
- `ApplicationHost.h` / `ApplicationHost.cpp`：后台工作区宿主入口，负责产品目录加载、项目会话管理、应用级命令分发、事件订阅和故障诊断。
- `ApplicationHostConfig.h`：应用宿主配置、产品目录路径和启动项目列表。
