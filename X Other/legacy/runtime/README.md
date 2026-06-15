# runtime

`runtime` 放置 icax 后台运行时。它负责把 framework、services、Mail 通信、PDO 和插件装配成可运行的后台实例。

## 目录结构

- `Engine/`：当前 C++ 后台引擎工程，负责加载配置、服务、插件和主循环入口。
