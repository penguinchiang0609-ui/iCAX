# UIContainer 规格文档

## 1. 定位

`UIContainer` 是前端容器抽象，不是具体窗口或浏览器实现。

它位于 `src/iCAX-UI/UIContainer`，负责定义 Application 启动层和 UI 技术实现之间的稳定契约。CEF、WPF、QT、Headless 都应实现 `IUIContainer`。

## 2. 核心类型

- `IFrontendBridge`：Application 提供的后端桥接口。
- `CFrontendMailEnvelope`：UI 与 Engine 之间传递的邮件信封。
- `CUIContainerConfig`：UI 容器启动配置。
- `IUIContainer`：真实 UI 容器接口。
- `CUIContainerFactory`：根据配置创建容器。
- `CUIContainerRegistrar` / `ICAX_REGISTER_UI_CONTAINER`：静态注册入口。

## 3. 配置

配置文件默认位置：

```text
Setting/UIContainer.Setting
```

支持字段：

- `type`：容器类型。默认 `headless`。
- `modulePath`：容器 DLL 路径。`headless` 可为空，`cef` 默认可为 `CefUIContainer.dll`，`wpf` 默认可为 `WpfUIContainer.dll`，`qt` 默认可为 `QtUIContainer.dll`。
- `webPageRoot`：AppShell 根目录。
- `startURL`：可选启动 URL。
- `startupHandshakeTimeoutMS`：headless 启动握手超时。
- 其他字段会进入 `Properties`，由具体容器解释。

## 4. 静态注册

动态容器 DLL 加载后，应通过宏注册：

```cpp
ICAX_REGISTER_UI_CONTAINER("cef", iCAX::Frontend::Cef::CCefUIContainer)
```

WPF/QT 容器也使用同一注册方式：

```cpp
ICAX_REGISTER_UI_CONTAINER("wpf", iCAX::Frontend::CWpfUIContainer)
ICAX_REGISTER_UI_CONTAINER("qt", iCAX::Frontend::CQtUIContainer)
```

工厂流程：

```text
CUIContainerFactory.Create(config)
  -> 若 type 未注册且有 modulePath，则 LoadLibrary(modulePath)
  -> DLL 静态初始化注册 type
  -> 根据 type 创建 IUIContainer 实例
  -> SetConfig(config)
```

不提供注销。已加载的 UI 容器 DLL 在进程生命周期内保持有效，避免注册表中的构造/销毁函数悬空。

## 5. Headless 容器

`cef` 是当前默认真实前端容器。WPF/QT 是同一契约下的可选前端容器。

内置 `headless` 容器用于启动链路验收，不创建窗口。

启动时执行：

```text
GetApplicationChannelIDText()
PostMail(App.GetState)
PollMails()
validate response
```

这条路径等价于前端 `ApplicationProxy` 初始化阶段对后端 application channel 的第一次握手。

## 6. 边界

`UIContainer` 不拥有 Engine，不启动/停止 `ApplicationHost`，不解析业务 payload，不管理 PDO shared memory。真实 UI 容器只通过 `IFrontendBridge` 与后端交互。
