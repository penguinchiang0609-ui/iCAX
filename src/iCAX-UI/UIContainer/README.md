# UIContainer

`UIContainer` 是 Application 启动层看到的 UI 抽象层。它不代表某一种具体 UI 技术，而是定义：

- `IFrontendBridge`：UI 容器连接后端 `ApplicationHost` 的桥。
- `CFrontendMailEnvelope`：前后端跨 UI 边界传递的邮件信封。
- `IUIContainer`：CEF、WPF、QT、Headless 等 UI 容器共同实现的接口。
- `CUIContainerFactory`：根据配置创建 UI 容器实例。
- `ICAX_REGISTER_UI_CONTAINER`：供 UI 容器 DLL 静态注册到工厂。

## 运行方式

`Application.exe` 先启动后端 `ApplicationHost`，再读取 `Setting/UIContainer.Setting`，通过 `CUIContainerFactory` 创建真实 UI 容器。

默认没有配置文件时，`Application.exe` 使用 CEF/H5 容器。需要无窗口验收时，可显式配置 `type=headless`；它不创建窗口，只模拟前端初始化流程：获取 application channel，发送 `App.GetState`，等待后端响应。WPF/QT 等其他前端容器只要实现同一契约，也可以通过配置切换。

## 配置示例

```ini
type=cef
modulePath=CefUIContainer.dll
webPageRoot=D:/penguinchinage0609/iCAX/src/iCAX-UI/SDK/AppShell
startupHandshakeTimeoutMS=5000
```

切换 WPF：

```ini
type=wpf
modulePath=WpfUIContainer.dll
startupHandshakeTimeoutMS=5000
```

切换 QT：

```ini
type=qt
modulePath=QtUIContainer.dll
startupHandshakeTimeoutMS=5000
```

无窗口验收：

```ini
type=headless
startupHandshakeTimeoutMS=5000
```

`modulePath` 指向的 DLL 被加载后，会通过静态注册宏把容器类型注册到 `CUIContainerFactory`。

## 边界

`UIContainer` 不拥有 Engine，不解析产品 manifest，不实现产品 Facade，不解释 PDO payload。它只定义前端容器和后端桥之间的稳定契约。
