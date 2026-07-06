# Bridge

`Bridge` 是 SDK 内部模块，负责发现和适配宿主注入的 `window.icax` bridge。

## 目录结构

- `createBridge.mjs`：统一创建 bridge。真实宿主存在时使用真实 bridge，否则使用 mock bridge。
- `mockHostBridge.mjs`：开发期 mock bridge，用于脱离真实宿主验证 AppShell 和 SDK。

## 边界

本模块只处理 JS 与宿主 bridge 的入口差异，不解释业务命令，不解析 PDO 数据。产品页面不直接依赖本目录。

宿主 bridge 的必需能力是 application/product/scene channel 注册、`postMail` 和 `subscribeMail`。`openFileDialog(options)` 是可选 UI 容器能力，CEF 容器会提供；没有该方法时，产品页面仍可以让用户手动输入路径。

