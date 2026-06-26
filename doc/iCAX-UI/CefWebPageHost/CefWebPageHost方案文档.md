# CefWebPageHost 方案文档

## 1. 代码位置

```text
src/iCAX-UI/CefWebPageHost/
  CefWebPageHost.h
  CefWebPageHost.cpp
  CefWebPageHost.vcxproj
  README.md
```

## 2. 依赖方向

```text
CefWebPageHost
  -> WebPageHost
    -> iCAX-Application FrontendBridge
      -> Engine ApplicationHost
        -> ProductRuntime
          -> Project
```

`WebPageHost` 不反向依赖 `CefWebPageHost`。`CefWebPageHost` 也不直接引用 Engine 项目。

## 3. CEF 接入策略

当前采用 CEF message router：

- renderer 侧在 V8 context 创建时注入 `window.icax`。
- JS 调用 `window.cefQuery()` 进入 browser 侧。
- browser 侧 query handler 调用 `CWebPageHost`。
- Engine response/event 由轮询线程取出，再投递到 CEF UI 线程执行 JS 分发。

## 4. Mailbox 流程

```text
JS AppProxy/ProductProxy/ProjectProxy
  -> MailboxClient.postMail()
  -> window.icax.postMail()
  -> cefQuery("postMail")
  -> CWebPageHost.PostMail()
  -> CFrontendBridge.PostMail()
  -> Engine post office
  -> Engine response mail
  -> CFrontendBridge.PollMails()
  -> CWebPageHost.PollMails()
  -> window.icax.__dispatchMail(mail)
  -> MailboxClient.receive()
  -> Promise resolve/reject
```

## 5. CEF 二进制处理

`CefWebPageHost.vcxproj` 通过环境变量定位 CEF：

- `ICAX_CEF_ROOT`
- `ICAX_CEF_LIB_DIR`
- `ICAX_CEF_WRAPPER_LIB_DIR`

PostBuild 会把 `libcef.dll` 所在目录的 DLL、CEF resources 和 locales 复制到输出目录。

`libcef_dll_wrapper.lib` 默认优先查找：

```text
$(ICAX_CEF_ROOT)/build/libcef_dll_wrapper/$(Platform)/$(Configuration)
$(ICAX_CEF_ROOT)/build/libcef_dll_wrapper/$(Configuration)
```

## 6. 当前限制

- 文件对话框尚未接入 CEF bridge。
- PDO shared memory 到 JS `ArrayBuffer` 的映射尚未接入 CEF bridge。
- CEF 子进程 EXE 尚未单独拆出；默认要求宿主 EXE 早期调用 `ExecuteSubProcess()`。

这些限制不影响 mailbox 基础链路，但会影响完整桌面宿主体验。
