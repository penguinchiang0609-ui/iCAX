# src

`src` 是当前代码主线入口，按 Engine、Application、UI、具体产品、测试和工具分层。

`iCAX.slnx` 是当前主线 Visual Studio 解决方案，适合 Visual Studio 2022 17.14 及以上版本使用。默认构建包含 Engine、Application、UIContainer、WpfUIContainer 和 C++ 单元测试；`CefUIContainer` 已加入解决方案，但因依赖本机 CEF SDK，默认不参与整解构建。具体产品项目后续放入 `apps/` 后再纳入解决方案。

## 目录结构

- `iCAX-Engine/`：C++ 后台平台，包含 foundation、framework、Resources、Facades、PDO 和平台级扩展。
- `iCAX-Plugins/`：插件层，承载渲染、显示数据、几何内核适配、导入导出等可选扩展能力。
- `iCAX-Application/`：应用容器层，负责组合 Engine 与 UI bridge。
- `iCAX-UI/`：前端公共框架，包含 UIContainer、WpfUIContainer、CefUIContainer、AppShell、Proxy、SDK、Facades/PDO 前端适配和公共 UI 能力。
- `apps/`：具体产品目录。每个产品独立包含 manifest、backend、webpage 和 protocol；当前主线不内置旧示例产品。
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

公共前端框架位于 `src/iCAX-UI/`。Engine 与 UI 的组合入口位于 `src/iCAX-Application/`。直接运行 `src/x64/Debug/Application.exe` 时，默认使用 WPF UI 容器；如需无窗口验收，在 `Setting/UIContainer.Setting` 中配置 `type=headless`；如需 CEF/H5，配置 `type=cef` 和 `modulePath=CefUIContainer.dll`。
