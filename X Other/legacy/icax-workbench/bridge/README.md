# bridge

`bridge` 放置 H5 与宿主容器之间的抽象层。

宿主可以是 WebView2、Qt WebEngine、CEF 或调试浏览器。H5 工作台只依赖统一 bridge 接口，不直接依赖具体宿主 API。

## 职责

- 获取 application mail id。
- 发送 Mailbox 命令。
- 订阅 backend 推送或宿主事件。
- 接收双击项目文件路径。
- 打开文件选择对话框。
- 获取资源 URL 或资源流句柄。

## 目录结构

- `README.md`：本目录说明。
- `createBridge.mjs`：优先接入宿主注入的 bridge；没有宿主时使用 mock bridge。
- `hostBridge.mjs`：宿主 bridge 适配器，统一 `postMail`、`subscribeMail`、文件选择和项目文件打开事件。
- `mockBridge.mjs`：浏览器调试用 mock backend，异步返回 response mail。
## 开发态产品注入

没有原生宿主时，workbench 使用 `MockIcaxBridge`。开发调试具体产品时，可以通过 URL 注入产品 manifest：

```text
/?mockProductManifest=/apps/flat-laser-cam/frontend/productManifest.mjs
```

manifest 需要导出 `createMockProduct()`。这样通用 workbench 不需要硬编码任何具体产品信息。
