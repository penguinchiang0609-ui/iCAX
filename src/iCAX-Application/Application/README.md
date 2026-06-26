# Application

`Application` 是进程级应用容器，位于 `iCAX-Engine` 和具体 UI 宿主之上。

本目录的职责：

- 拥有并启动 `ApplicationHost`。
- 拥有 `CFrontendBridge`，为 H5、Qt、WPF 等 UI 宿主提供统一的 mailbox 入口。
- 隔离 Engine 生命周期和 UI 技术选择。

本目录不负责：

- 解析具体产品业务命令。
- 渲染 UI。
- 绑定 CEF/Qt/WPF 的窗口生命周期。

## 目录结构

```text
Application/
  Application.h / Application.cpp
  FrontendBridge.h / FrontendBridge.cpp
  ApplicationExport.h
  Application.vcxproj
```
