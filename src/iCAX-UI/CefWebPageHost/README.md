# CefWebPageHost

`CefWebPageHost` 是 `CWebPageHost` 的 CEF 适配层。它负责初始化 CEF、创建浏览器窗口、注入 `window.icax` bridge，并把 JS 调用转发到 `CWebPageHost`。

## 目录结构

- `CefWebPageHost.h` / `CefWebPageHost.cpp`：CEF runtime、浏览器窗口、JS bridge 和 Engine 邮件轮询。
- `CefWebPageHost.vcxproj`：CEF 适配层工程。

## CEF SDK 要求

本工程不把 CEF 二进制提交到仓库。构建前需要设置：

- `ICAX_CEF_ROOT`：CEF binary distribution 根目录，目录下应包含 `include/cef_app.h`。
- `ICAX_CEF_LIB_DIR`：可选，`libcef.lib/libcef.dll` 所在目录。默认 Debug 使用 `$(ICAX_CEF_ROOT)/Debug`，Release 使用 `$(ICAX_CEF_ROOT)/Release`。
- `ICAX_CEF_WRAPPER_LIB_DIR`：可选，`libcef_dll_wrapper.lib` 所在目录。默认优先使用 `$(ICAX_CEF_ROOT)/build/libcef_dll_wrapper/$(Platform)/$(Configuration)`，不存在时再尝试 `$(ICAX_CEF_ROOT)/build/libcef_dll_wrapper/$(Configuration)`。

CEF 的 `libcef_dll_wrapper.lib` 必须由同一份 CEF SDK 编译出来，不能跨版本混用。

## 边界

`CefWebPageHost` 不拥有 Engine，不实现业务命令，不解释 PDO payload，不直接修改 backend 数据。它只是 H5 UI 的 CEF 宿主适配层。

`CWebPageHost` 仍然是 H5 adapter 核心；CEF 依赖只停留在本目录。
