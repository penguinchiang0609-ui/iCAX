# SDK

`SDK` 是 H5 页面和产品 `webpage` 模块使用的前端统一入口。

它隐藏以下 backend 和宿主通信细节：

- `Facade` / `FacadeEndpoint`
- `FacadeName.MethodName` 64 位紧凑编码
- `VariantSerializer` 文本格式
- PDO shared memory 读写租约
- H5 内置三维 viewport
- AppProxy/ProductProxy/ProjectProxy 三层运行时 channel id

产品前端只通过 `connectApplication()`、`AppProxy`、`ProductProxy`、`ProjectProxy`、`SceneProxy` 和 SDK 导出的公共能力工作。

## 目录结构

- `index.mjs`：SDK 公开导出。
- `runtime.mjs`：高层连接入口。
- `AppShell/`：SDK 自带的 H5 应用壳，宿主加载其 `index.html`。
- `Bridge/`：真实宿主 bridge 发现、校验和 mock bridge。
- `Facades/`：Facade client、Facade 方法编码和 Variant 文本编解码。
- `PDO/`：PDO 前端访问代理，面向 shared memory lease。
- `Viewport/`：RenderPDO 解析和 H5 默认 Three.js 视口。
- `ThirdParty/`：SDK 自带的 H5 runtime 依赖，目前包含 Three.js。

## 边界

`AppShell/Bridge/Facades/PDO/Viewport` 是 SDK 内部模块。产品页面通过 `SDK/index.mjs` 使用能力，不直接 import 这些目录。

公开三层对象仍放在同级模块：

- `../AppProxy/`
- `../ProductProxy/`
- `../ProjectProxy/`
- `../UI/`

SDK 负责把它们组合成稳定入口。
