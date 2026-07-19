# CefUIContainer 规格文档

## 1. 定位

`CefUIContainer` 是 `IUIContainer` 的 CEF 实现。它不是中间 adapter，而是真实 UI 容器。

## 2. 职责

- 初始化 CEF runtime。
- 提供 CEF 子进程入口。
- 创建浏览器窗口。
- 加载 `AppShell/index.html` 或配置中的 `startURL`。
- 注入 `window.icax`。
- 将 JS bridge 调用转发给 `IFrontendBridge`。
- 轮询后端 Facade frame，并通过 `window.icax.__dispatchFacadeFrame(frame)` 派发给 JS。

## 3. 静态注册

`CefUIContainer` DLL 加载时注册：

```cpp
ICAX_REGISTER_UI_CONTAINER("cef", iCAX::Frontend::Cef::CCefUIContainer)
```

`Application.exe` 的配置：

```ini
type=cef
modulePath=CefUIContainer.dll
webPageRoot=.../src/iCAX-UI/SDK/AppShell
facadePollIntervalMS=16
```

## 4. CEF SDK

`CefUIContainer` 必须使用官方 CEF binary distribution，不使用被业务工程改造过的 CEF 包。当前验证版本：

- `cef_binary_149.0.5+g6770623+chromium-149.0.7827.197_windows64_minimal`
- 源码入口：`https://github.com/chromiumembedded/cef`
- 二进制入口：`https://cef-builds.spotifycdn.com/index.html`

推荐放置在仓库根目录：

```text
.deps/cef/cef_binary_149.0.5+g6770623+chromium-149.0.7827.197_windows64_minimal
```

`.deps/` 不提交到仓库。工程文件会优先使用该路径；如果不存在，再读取：

- `ICAX_CEF_ROOT`
- `ICAX_CEF_LIB_DIR`
- `ICAX_CEF_WRAPPER_LIB_DIR`

当前 H5 是主前端路线，主解决方案会构建 `CefUIContainer`。没有 CEF SDK 的机器应先准备 `.deps/cef`，或通过环境变量指定 CEF SDK 路径。

`libcef_dll_wrapper.lib` 必须由同一份 CEF SDK 编译得到，并且运行库需要和本工程一致。当前使用 `/MD`：

```powershell
cmake -S .deps\cef\cef_binary_149.0.5+g6770623+chromium-149.0.7827.197_windows64_minimal `
  -B .deps\cef\cef_binary_149.0.5+g6770623+chromium-149.0.7827.197_windows64_minimal\build_vs2022_x64_md `
  -G "Visual Studio 17 2022" -A x64 -DCEF_RUNTIME_LIBRARY_FLAG=/MD

cmake --build .deps\cef\cef_binary_149.0.5+g6770623+chromium-149.0.7827.197_windows64_minimal\build_vs2022_x64_md `
  --config Release --target libcef_dll_wrapper -- /m:1
```

## 5. 运行配置

```ini
type=cef
modulePath=CefUIContainer.dll
webPageRoot=.../src/iCAX-UI/SDK/AppShell
facadePollIntervalMS=16
remoteDebuggingPort=9223
allowFileAccessFromFiles=true
disableGpu=true
```

- `remoteDebuggingPort`：开启 CEF DevTools HTTP 端口，开发期用于确认页面加载和 JS 状态。
- `allowFileAccessFromFiles`：允许本地 `AppShell` 以 ES module 方式加载。
- `disableGpu`：开发环境可关闭 GPU，避免 GPU 子进程因机器图形环境不稳定而影响验证。

## 6. 边界

`CefUIContainer` 不拥有 Engine，不解析业务命令，不解释 PDO payload，不写具体产品逻辑。它只负责 H5 运行环境和 bridge 封送。
