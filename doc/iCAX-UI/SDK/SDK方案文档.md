# SDK 方案文档

## 1. 当前实现

代码位置：

```text
src/iCAX-UI/SDK/
  index.mjs
  runtime.mjs
  AppShell/
  Bridge/
  Mailbox/
  PDO/
```

`index.mjs` 是公开门面。`runtime.mjs` 提供高层连接入口。`AppShell` 是 SDK 自带的 H5 应用容器。`Bridge/Mailbox/PDO` 是 SDK 内部通信模块。

## 2. 依赖关系

公开方向：

```text
AppShell / product webpage
  -> SDK/index.mjs
```

宿主加载入口：

```text
CefUIContainer
  -> src/iCAX-UI/SDK/AppShell/index.html
```

SDK 内部方向：

```text
SDK/runtime
  -> SDK/Bridge
  -> AppProxy

AppProxy/ProductProxy/ProjectProxy/SceneProxy
  -> SDK/Mailbox
  -> SDK/PDO
```

`SDK/Mailbox`、`SDK/PDO` 不依赖 `AppProxy/ProductProxy/ProjectProxy`，避免循环依赖。

## 3. 使用方式

Shell 或产品前端应这样使用：

```js
import { connectApplication, ProjectFacade } from "../../iCAX-UI/SDK/index.mjs";

const app = await connectApplication();
const product = await app.startProduct("icax.laser-3d-cam");
```

产品前端不直接 import：

```text
SDK/Mailbox/*
SDK/PDO/*
```

这些路径只服务框架内部和白盒测试。

## 4. 封装策略

Mailbox 封装在代理对象中：

- `AppProxy` 使用 application channel。
- `ProductProxy` 使用 product channel。
- `ProjectProxy` 管理项目容器，不使用 mailbox。
- `SceneProxy` 使用 scene channel。

PDO 封装在 `SceneProxy` 中：

- Scene 状态携带 PDO 描述。
- `SceneProxy` 创建 `scene.pdo`。
- 产品前端通过 `scene.pdo.withRead(typeName, instanceName, reader)` 读取高频数据。

Bridge 封装在 `connectApplication()` 中：

- 优先使用真实 `window.icax`。
- 没有真实宿主时，开发期可以使用 `MockHostBridge`。

## 5. 依赖方向约束

允许：

```text
SDK/index -> SDK/runtime
SDK/AppShell -> SDK/index
SDK/index -> AppProxy/ProductProxy/ProjectProxy/SceneProxy
SDK/index -> ProductProxy module loader
SDK/index -> UI
AppProxy/ProductProxy/SceneProxy -> SDK/Mailbox
SceneProxy -> SDK/PDO
SDK/PDO -> SDK/Mailbox command route
SDK/Bridge -> SDK/Mailbox
```

不允许：

```text
SDK/Mailbox -> AppProxy/ProductProxy/ProjectProxy/SceneProxy
SDK/PDO -> SceneProxy
SDK/Bridge -> AppProxy/ProductProxy/ProjectProxy/SceneProxy
产品 webpage -> SDK/Mailbox 或 SDK/PDO
```

## 6. 验收标准

- `AppShell` 只从 `SDK/index.mjs` 导入公共能力。
- `SDKTest.mjs` 公开 API 测试从 SDK 入口导入。
- 内部 Mailbox/PDO 白盒测试直接使用 `SDK/Mailbox`、`SDK/PDO` 路径。
- 产品目录下不需要 import SDK 内部深层路径。

