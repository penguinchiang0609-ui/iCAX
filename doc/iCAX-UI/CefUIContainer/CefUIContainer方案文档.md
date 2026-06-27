# CefUIContainer 方案文档

## 1. 结构

```text
CCefUIContainer : IUIContainer
  -> CEF runtime
  -> CEF browser
  -> window.icax
  -> IFrontendBridge
```

`CefUIContainer` 直接依赖 `UIContainer` 公共契约，不依赖 `iCAX-Application`。

## 2. JS Bridge

注入对象：

```js
window.icax = {
  getApplicationChannelId,
  registerProductChannel,
  registerProjectChannel,
  postMail,
  subscribeMail,
  pdo,
  __dispatchMail
}
```

`postMail` 进入 CEF query handler 后调用 `IFrontendBridge::PostMail`。后端 response/event 由轮询线程调用 `IFrontendBridge::PollMails` 后派发给 JS。

`pdo.withRead()` 不走 CEF query handler。该能力安装在 renderer 进程的 V8 context 中，renderer 进程按 arena 名称打开 PDO shared memory，并在同步 reader 回调期间提供当前 payload 的 `ArrayBuffer` 快照。

CEF 进程边界：

- browser/main 进程：拥有 `CCefUIContainer`、mail polling、`IFrontendBridge`。
- renderer 进程：拥有 H5、V8、`window.icax` 和 PDO read bridge。
- MAIL：browser/main 进程经 `cefQuery` 转发 JSON。
- PDO：renderer 进程直接打开 shared memory arena，不影响 MAIL。

官方 CEF 的 V8 sandbox 不支持把任意共享内存直接作为 external `ArrayBuffer` 暴露给 JS。当前实现会在 read lease 内把 payload 拷贝为 V8 `ArrayBuffer`，保证语义正确；若后续要求 JS 侧完全零拷贝，需要定制 CEF 或另行设计 native/GPU interop。

## 3. 启动

```text
Application.exe
  -> CApplication.Start()
  -> CUIContainerFactory.Create(type=cef,modulePath=CefUIContainer.dll)
  -> LoadLibrary(CefUIContainer.dll)
  -> static register "cef"
  -> CCefUIContainer.SetConfig()
  -> CCefUIContainer.Start()
```

## 4. 构建

`CefUIContainer` 依赖官方 CEF binary distribution。推荐目录：

```text
.deps/cef/cef_binary_149.0.5+g6770623+chromium-149.0.7827.197_windows64_minimal
```

wrapper 必须由同一份 CEF SDK 编译，且使用 `/MD`：

```powershell
cmake -S .deps\cef\cef_binary_149.0.5+g6770623+chromium-149.0.7827.197_windows64_minimal `
  -B .deps\cef\cef_binary_149.0.5+g6770623+chromium-149.0.7827.197_windows64_minimal\build_vs2022_x64_md `
  -G "Visual Studio 17 2022" -A x64 -DCEF_RUNTIME_LIBRARY_FLAG=/MD

cmake --build .deps\cef\cef_binary_149.0.5+g6770623+chromium-149.0.7827.197_windows64_minimal\build_vs2022_x64_md `
  --config Release --target libcef_dll_wrapper -- /m:1
```

构建容器：

```powershell
msbuild src\iCAX-UI\CefUIContainer\CefUIContainer.vcxproj `
  /p:Configuration=Release /p:Platform=x64
```

主解决方案中该项目默认 `Build=false`。

## 5. 运行

`Setting/UIContainer.Setting` 示例：

```ini
type=cef
modulePath=CefUIContainer.dll
webPageRoot=.../src/iCAX-UI/SDK/AppShell
mailPollIntervalMS=16
remoteDebuggingPort=9223
allowFileAccessFromFiles=true
disableGpu=true
```

启动后端入口：

```text
Application.exe
  -> ApplicationHost.Start()
  -> CUIContainerFactory.Create(type=cef)
  -> CCefUIContainer.Start()
  -> CEF Browser loads AppShell/index.html
  -> JS ApplicationProxy connects to backend ApplicationHost channel
```

运行目录必须包含 CEF runtime 文件。`CefUIContainer.vcxproj` 的 post-build 会复制 `libcef.dll`、`*.pak`、`icudtl.dat`、`locales/`、`Resources/` 等文件到输出目录。
