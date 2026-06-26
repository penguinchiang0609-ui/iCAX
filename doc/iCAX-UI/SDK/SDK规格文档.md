# SDK 规格文档

## 1. 定位

`SDK` 是前端公共能力的唯一稳定入口。

产品 `webpage` 和 `AppShell` 应优先只依赖 `SDK/index.mjs`，避免直接感知 Bridge、Mailbox、PDO 等底层通信模块路径。

## 2. 公开导出

SDK 公开导出：

- `connectApplication()`：连接宿主 bridge，创建 `AppProxy`。
- `createBridge()` / `validateBridge()`：宿主 bridge 创建与校验能力，主要用于 Shell 和测试。
- `MockHostBridge`：开发期和白盒测试使用的 mock bridge。
- `AppCommands` / `ProductCommands` / `ProjectCommands`：框架标准命令名。
- `AppProxy` / `ProductProxy` / `ProjectProxy`：前端三层代理对象。
- `resolveFrontendEntry()` / `loadProductModule()` / `mountProductModule()`：产品 H5 模块加载能力。
- `escapeText()` / `escapeAttr()`：公共 HTML 转义工具。

## 3. 内部能力

以下能力物理上放在 `SDK` 下，但不从 `SDK/index.mjs` 作为产品侧公共 API 暴露：

- `SDK/AppShell`：SDK 自带的 H5 应用壳，由宿主加载 `index.html`。
- `SDK/Bridge`：真实宿主 bridge 发现、校验和 mock bridge。
- `SDK/Mailbox`：mailbox client、命令码、Variant 文本编解码。
- `SDK/PDO`：PDO 前端访问代理。

产品页面不应该直接 new `MailboxClient`，也不应该直接操作 PDO header。产品页面通过 `AppProxy/ProductProxy/ProjectProxy` 发送命令、订阅事件，通过 `project.pdo` 读取 PDO。

## 4. 边界

`SDK` 不承载业务实现。

具体产品能力必须放在 `src/apps/<product-id>/backend`、`src/apps/<product-id>/webpage` 和 `src/apps/<product-id>/protocol`。

SDK 可以封装通信细节，但不能解释具体产品的业务 payload 和 PDO 二进制布局。

## 5. 稳定性契约

产品前端优先从 `SDK/index.mjs` 引入公共能力。

SDK 公开导出名称一旦被产品使用，应保持稳定。

如果底层模块移动，只允许修改 SDK 内部 import/export，不应要求产品批量改 import。

## 6. 使用样例

```js
import { connectApplication } from "../../iCAX-UI/SDK/index.mjs";

const app = await connectApplication();
const product = await app.startProduct("icax.flat-laser-cam");
const opened = await product.openProjectCatalog("D:/projects/a.ilcam");

await opened.projectProxy.execute("Project.Save", {
  path: "D:/projects/a.ilcam",
});
```

## 7. 验收要求

- `SDK/AppShell` 可以只从 SDK 入口导入公共能力。
- 产品目录不需要 import `SDK/Mailbox` 或 `SDK/PDO`。
- SDK 不包含具体产品代码。
- SDK 不直接实现业务逻辑。
- 白盒测试可以直接 import `SDK/Mailbox`、`SDK/PDO` 验证内部实现。

