# Application

`Application` 是进程级可执行程序，位于 `iCAX-Engine` 和具体 UI 宿主之上。

本目录的职责：

- 拥有并启动 `ApplicationRuntime`。
- 拥有 `CFrontendBridge`，为 H5/CEF、WPF、QT 等 UI 宿主提供统一的 SDO 入口。
- 隔离 Engine 生命周期和 UI 技术选择。
- 提供进程入口 `wWinMain`，负责创建应用对象、读取 `UIContainer.Setting`、通过 `CUIContainerFactory` 挂载 UI 容器并按顺序启动/停止。

`CFrontendBridge` 拥有前端 `EventLoopTaskScheduler`。前端原生异步操作在初始 Task/`TaskCompletionSource` 创建时绑定 `GetFrontTaskScheduler()`，后续 `ContinueWith()` 默认继承；具体 UI 容器负责在自己的单线程 event loop 调用 `RunFrontTasks()`。

本目录不负责：

- 解析具体产品的 SDO 调用。
- 渲染 UI。
- 绑定 CEF/WPF/QT 等具体 UI 容器的窗口生命周期。

## 目录结构

```text
Application/
  Main.cpp
  Application.h / Application.cpp
  FrontendBridge.h / FrontendBridge.cpp
  Application.vcxproj
```

默认没有 UI 配置文件时，`TubeDesigner.exe` 使用 CEF/H5 UI 容器。需要切换 UI 技术时，可在 `Setting/UIContainer.Setting` 中配置 `type=wpf` 或 `type=qt`，前提是对应容器 DLL 实现了 `IUIContainer` 并注册到 `CUIContainerFactory`。需要无窗口验收时，可显式配置 `type=headless`。

可执行程序同目录的 `Application.Product.Setting` 声明启用的产品，每行一个产品清单路径；相对路径以可执行程序目录为基准。支持 UTF-8、空行和以 `#` 开头的注释。修改配置后重启即可生效，不扫描目录自动注册产品。空配置、缺失清单和重复产品 ID 都作为配置错误报告。未提供此文件时，当前发行配置默认启用 TubeDesigner。

配置一款产品时，使用该产品的启动画面并直接打开未保存的空项目；配置多款产品时，进入原来的产品／最近项目选择页，不自动创建项目。判断依据是原生注册的产品列表，不受 URL 中 `product` 参数影响。公共首屏为中性工作台画面，产品界面模块可通过 `getStartupPresentation()` 提供自己的启动文字和图案。

例如同时启用两款产品：

```text
apps/tube-designer/product.manifest.json
apps/five-axis-cam/product.manifest.json
```

三维 CAM 的开发启动目录可用 `src/tools/build/start_five_axis_cam.ps1` 准备和启动。其配置只启用三维 CAM，资源和模块路径按运行目录展开，同时部署平台和该产品的窗口图标。

启动器构建传入 `/p:ICAXApplicationTargetName=FiveAxisCAM` 时，从 `apps/branding/FiveAxisCAM.rc` 编译独立的程序图标和文件版本名称；默认 `TubeDesigner` 对应自身资源。通用版本布局仍由 `Application.rc` 提供。自定义发行版可指定 `ICAXApplicationResourceFile`。运行时窗口图标按 `apps/Branding.Setting` 和已配置产品列表选择，单产品使用产品图标，多产品使用平台图标。

应用默认把 Scene 资源历史版本根目录设置为 `Temp/ResourceVersions`。宿主也可以在启动前通过 `CApplicationConfig::RuntimeConfig.Paths.ResourceVersionDirectory` 覆盖；相对路径以 `Paths.InstallDirectory` 为基准。
