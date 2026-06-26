# src

`src` 是当前代码主线入口，按 Engine、Application、UI、具体产品、测试和工具分层。

`iCAX.sln` 是当前主线 Visual Studio 解决方案，按物理目录组织 solution folder。默认构建包含 Engine、Application、UI WebPageHost、apps 和 C++ 单元测试；`CefWebPageHost` 已加入解决方案，但因依赖本机 CEF SDK，默认不参与整解构建。

## 目录结构

- `iCAX-Engine/`：C++ 后台平台，包含 foundation、framework、Mailbox、PDO、插件和平台级扩展。
- `iCAX-Application/`：应用容器层，负责组合 Engine 与 UI bridge。
- `iCAX-UI/`：H5 前端公共框架，包含 WebPageHost、CefWebPageHost、AppShell、Proxy、SDK、Mailbox/PDO 前端适配和公共 UI 能力。
- `apps/`：具体产品目录。每个产品独立包含 manifest、backend、webpage 和 protocol。
- `tests/`：测试代码和测试资料，按源码主目录一一对应。
- `tools/`：项目维护、生成、迁移和编码处理工具。

## 产品目录

具体产品统一放在：

```text
src/apps/<product-id>/
  product.manifest.json
  backend/
  webpage/
  protocol/
```

公共 H5 前端框架位于 `src/iCAX-UI/`。Engine 与 UI 的组合入口位于 `src/iCAX-Application/`。
