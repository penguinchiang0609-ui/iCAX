# Bridge

`Bridge` 是 SDK 内部模块，负责发现和适配宿主注入的 `window.icax` bridge。

## 目录结构

- `createBridge.mjs`：统一创建 bridge。真实宿主存在时使用真实 bridge，否则使用 mock bridge。
- `mockHostBridge.mjs`：开发期 mock bridge，用于脱离真实宿主验证 AppShell 和 SDK。

## 边界

本模块只处理 JS 与宿主 bridge 的入口差异，不解释 SDO 业务语义，不解析 PDO 数据。产品页面不直接依赖本目录。

宿主 bridge 的必需能力是 application/product/scene channel 注册、`postSDOFrame`、`subscribeSDOFrames` 和直接资源访问 `requestResource`。资源请求只传 method、完整 URL、headers 和 body；URL 自身决定 Application/Product/Project/Scene 资源库。`openFileDialog(options)` 是可选 UI 容器能力，CEF 容器会提供；没有该方法时，产品页面仍可以让用户手动输入路径。

CEF 同时提供 `saveFileDialog({ title, defaultPath, defaultExtension, filters })`。`defaultExtension` 不带点；`filters` 为 `{ name, extensions }` 数组。返回所选本地路径，取消返回 `null`，覆盖已有文件由原生对话框确认。该方法只选择路径；实际保存通过主场景上的 `Project.Save` 完成，可调用 `ProjectProxy.save(projectPath)`。AppShell 首次保存选位置，后续保存使用现有路径；Ctrl+Shift+S 另存为，Ctrl+O 打开，Ctrl+S 保存。

