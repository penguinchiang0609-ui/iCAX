# Application

`Application` 是进程级可执行程序，位于 `iCAX-Engine` 和具体 UI 宿主之上。

本目录的职责：

- 拥有并启动 `ApplicationHost`。
- 拥有 `CFrontendBridge`，为 WPF、Qt、H5 等 UI 宿主提供统一的 mailbox 入口。
- 隔离 Engine 生命周期和 UI 技术选择。
- 提供进程入口 `wWinMain`，负责创建应用对象、读取 `UIContainer.Setting`、通过 `CUIContainerFactory` 挂载 UI 容器并按顺序启动/停止。

本目录不负责：

- 解析具体产品业务命令。
- 渲染 UI。
- 绑定 CEF/Qt/WPF 的窗口生命周期。

## 目录结构

```text
Application/
  Main.cpp
  Application.h / Application.cpp
  FrontendBridge.h / FrontendBridge.cpp
  Application.vcxproj
```

默认没有 UI 配置文件时，`Application.exe` 使用 WPF UI 容器。需要无窗口验收时，可在 `Setting/UIContainer.Setting` 中显式配置 `type=headless`。
