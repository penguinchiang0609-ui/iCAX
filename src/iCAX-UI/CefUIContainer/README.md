# CefUIContainer

`CefUIContainer` 是基于 CEF 的 `IUIContainer` 实现。它以 DLL 形式存在，加载后通过 `ICAX_REGISTER_UI_CONTAINER("cef", CCefUIContainer)` 静态注册到 `CUIContainerFactory`。

## 职责

- 初始化 CEF runtime。
- 创建 CEF browser。
- 加载 `AppShell/index.html` 或配置指定的 `startURL`。
- 注入 `window.icax` bridge。
- 将 JS 的 `getApplicationChannelId/registerProductChannel/registerSceneChannel/postFacadeFrame` 直接转发到 `IFrontendBridge`。
- 提供 `openFileDialog(options)` 宿主能力，返回用户选择的 UTF-8 文件路径；用户取消时返回 `null`。
- 轮询 `IFrontendBridge::PollFacadeFrames()`，把后端 request/report/response/event 派发给 JS 订阅者。

Facade 轮询同时向 CEF UI task runner 投递 Front Task pump；`IFrontendBridge::RunFrontTasks()` 因而在 CEF UI 线程执行，绑定 Front Task scheduler 的 continuation 不会落到后台线程池。

## CEF SDK

本工程使用官方 CEF binary distribution，不使用工程外部改造过的 CEF 包。当前验证版本：

- `cef_binary_149.0.5+g6770623+chromium-149.0.7827.197_windows64_minimal`
- 源码入口：`https://github.com/chromiumembedded/cef`
- 二进制入口：`https://cef-builds.spotifycdn.com/index.html`

推荐放置在仓库内：

```text
.deps/cef/cef_binary_149.0.5+g6770623+chromium-149.0.7827.197_windows64_minimal
```

`.deps/` 不提交到仓库。`CefUIContainer.vcxproj` 会优先使用上述路径；如果不存在，再读取：

- `ICAX_CEF_ROOT`
- `ICAX_CEF_LIB_DIR`
- `ICAX_CEF_WRAPPER_LIB_DIR`

`libcef_dll_wrapper.lib` 必须由同一份 CEF SDK 编译得到。

当前工程使用 `/MD` 运行库，wrapper 构建命令：

```powershell
cmake -S .deps\cef\cef_binary_149.0.5+g6770623+chromium-149.0.7827.197_windows64_minimal `
  -B .deps\cef\cef_binary_149.0.5+g6770623+chromium-149.0.7827.197_windows64_minimal\build_vs2022_x64_md `
  -G "Visual Studio 17 2022" -A x64 -DCEF_RUNTIME_LIBRARY_FLAG=/MD

cmake --build .deps\cef\cef_binary_149.0.5+g6770623+chromium-149.0.7827.197_windows64_minimal\build_vs2022_x64_md `
  --config Release --target libcef_dll_wrapper -- /m:1
```

## 配置

`Setting/UIContainer.Setting` 中使用：

```ini
type=cef
modulePath=CefUIContainer.dll
webPageRoot=.../src/iCAX-UI/SDK/AppShell
facadePollIntervalMS=16
remoteDebuggingPort=9223
allowFileAccessFromFiles=true
disableGpu=true
```

`Application.exe` 会先启动后端，再加载 `modulePath`，触发 CEF 容器静态注册，最后由工厂构造 `CCefUIContainer`。

`allowFileAccessFromFiles=true` 用于允许本地 `AppShell` 以 ES module 方式加载；`disableGpu=true` 适合当前开发环境，避免 GPU 子进程在没有稳定图形环境时反复崩溃。
