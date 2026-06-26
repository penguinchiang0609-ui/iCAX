# WebPageHost

`WebPageHost` 是 iCAX-UI 的 H5 原生适配核心。它不拥有 Engine，也不启动 `ApplicationHost`，只把浏览器宿主传来的 JS 调用转发给 `iCAX-Application` 提供的 `FrontendBridge`。

## 职责

- 持有非拥有的 `CFrontendBridge` 指针。
- 校验 H5 adapter 启停状态。
- 将 H5 请求封装成 frontend mail envelope 并交给 `FrontendBridge`。
- 轮询 `FrontendBridge`，把 Engine response/event 交还给 CEF/WebView/Qt 等 UI 宿主适配器。
- 暴露 application/product/project channel 查询入口。

## 边界

`WebPageHost` 不处理具体产品 UI，不解析产品 manifest，不启动或停止 Engine，不直接管理 post office，也不解释 PDO payload。

Engine 生命周期由 `src/iCAX-Application/Application` 负责。CEF 窗口、JS bridge 注入、文件对话框、窗口标题、拖拽文件和 PDO 到 JS `ArrayBuffer` 的映射属于 `CefWebPageHost` 或其他 UI 宿主适配器。

## 当前代码

- `WebPageHost.h` / `WebPageHost.cpp`：H5 adapter 与 `CFrontendBridge` 的连接层。

实际 CEF 适配器由 `../CefWebPageHost` 提供，并调用 `CWebPageHost` 暴露的 adapter 方法。
