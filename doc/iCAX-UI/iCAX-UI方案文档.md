# iCAX-UI 方案文档

## 1. 总体结构

```text
src/iCAX-Application/
  Application/

src/iCAX-UI/
  WebPageHost/
  CefWebPageHost/
  AppProxy/
  ProductProxy/
  ProjectProxy/
  UI/
  SDK/
    AppShell/
    Bridge/
    Mailbox/
    PDO/

src/apps/
  <product-id>/
    product.manifest.json
    backend/
    webpage/
    protocol/
```

`src/iCAX-UI` 是公共 H5 前端框架。`src/iCAX-Application` 是应用容器，负责组合 Engine 与 UI bridge。`src/apps` 是具体产品。三者通过 manifest、mailbox 和 PDO 契约连接。

## 2. WebPageHost 方案

`WebPageHost` 是 H5 adapter 核心。`CefWebPageHost` 是 CEF 原生适配层。

内部组成：

```text
iCAX-Application
  ApplicationHost
  FrontendBridge

WebPageHost
  non-owning FrontendBridge*

CefWebPageHost
  CEF Runtime
  CEF Browser
  JS Bridge Injection
  PDOArenaBridge
  NativeDialogBridge
```

当前 `src/iCAX-UI/WebPageHost/CWebPageHost` 已落地 H5 与 `FrontendBridge` 的 adapter 骨架：

- `Start()` 校验 `FrontendBridge` 已绑定 Engine。
- `GetApplicationChannelIDText()` 返回应用级入口。
- `RegisterProductChannel(productId)` 转发产品 channel 注册。
- `RegisterProjectChannel(projectId)` 转发项目 channel 注册。
- `PostMail(envelope)` 将 H5 mail 投递到 Engine。
- `PollMails()` 取出 Engine 发回 H5 的 response/event。

`CefWebPageHost` 已开始落地：

- 初始化 CEF runtime。
- 创建 CEF browser。
- 加载 `AppShell/index.html`。
- 注入 `window.icax`。
- 将 JS `postMail` 调到 `CWebPageHost::PostMail`。
- 轮询 `PollMails` 并推送 JS。

后续仍需要补：

- 将 PDO read/write lease 包装成 JS `ArrayBuffer`。
- 文件对话框、窗口标题、拖拽文件。

## 3. AppShell 方案

`AppShell` 是唯一网页入口：

```text
src/iCAX-UI/SDK/AppShell/index.html
```

页面内部布局：

```text
TopBar
ProductRail | Workspace | Inspector
BottomDock
```

Workspace 根据运行态显示：

- 无活动产品：产品列表。
- 产品已选择：产品主页和最近项目。
- 项目已打开：项目工作区，中央区域交给产品 `webpage` 模块。

不做多 HTML 跳转。产品切换和项目切换都是同一页面内的运行态变化。

## 4. 前端模块方案

公共前端拆成多个模块：

- `Bridge`：选择真实 `window.icax` 或 mock bridge。
- `Mailbox`：命令路由、payload 文本编解码、Promise mailbox client。
- `PDO`：PDO ID 和 PDO read lease 入口。
- `AppProxy`：前端 application 代理对象。
- `ProductProxy`：前端 product 代理对象和产品 ESM 模块加载。
- `ProjectProxy`：前端 project 代理对象和 PDO client 挂接点。
- `UI`：公共 UI 工具和公共组件。
- `SDK`：统一导出门面。

`SDK` 负责让产品前端代码保持简单：

```js
const app = await icax.connectApplication();
const product = await app.startProduct("icax.flat-laser-cam");
const project = await product.openProject("D:/projects/a.ilcam");
await project.execute("Nesting.Generate", {});
```

## 5. 产品 manifest

产品 manifest 示例：

```json
{
  "schema": "icax.product.manifest",
  "schemaVersion": 1,
  "productId": "icax.flat-laser-cam",
  "productName": "Flat Laser CAM",
  "productVersion": "0.1.0",
  "projectFile": {
    "magic": "ICAX_FLAT_LASER_CAM",
    "formatVersion": "1.0",
    "fileExtensions": [".ilcam"]
  },
  "backend": {
    "startupComponent": "FlatLaserCamStartupComponent",
    "modules": {
      "components": ["backend/bin/FlatLaserCamComponent.dll"],
      "behaviours": ["backend/bin/FlatLaserCamBehaviour.dll"],
      "commands": ["backend/bin/FlatLaserCamCommand.dll"],
      "services": ["backend/bin/FlatLaserCamService.dll"]
    }
  },
  "webpage": {
    "entry": "apps/flat-laser-cam/webpage/entry.mjs"
  }
}
```

`iCAX-Application` 启动前应把 manifest 转换成 Engine 产品定义。frontend 使用响应中的 `frontendEntry` 动态加载产品页面。

## 6. 产品 webpage 模块

产品前端模块导出：

```js
export function mountProduct(context) {}
export function mountProject(context) {}
```

`context` 包含：

- `product`：产品状态快照。
- `project`：当前项目状态快照。
- `mount`：产品模块挂载节点。
- `mode`：`product` 或 `project`。
- `actions`：通用工作台动作。

产品模块不直接访问 `window.icax`。需要 backend 能力时，通过 SDK runtime 或上下文动作调用。

## 7. 后端主动事件

Engine 主动发给前端的事件仍然走 mailbox，但不是 request/response。

约定：

- `originId = 0` 表示主动事件。
- `typeCode` 表示事件类型。
- `payloadText` 使用 UTF-8 文本。

前端侧订阅：

```js
const unsubscribe = mailbox.subscribe(projectChannelId, "Project.RepositoryChanged", event => {
  console.log(event.payload);
});
```

## 8. 当前落地内容

已新增：

- `src/iCAX-Application/Application`
- `src/iCAX-UI/WebPageHost`
- `src/iCAX-UI/CefWebPageHost`
- `src/iCAX-UI/SDK/AppShell`
- `src/iCAX-UI/AppProxy`
- `src/iCAX-UI/ProductProxy`
- `src/iCAX-UI/ProjectProxy`
- `src/iCAX-UI/SDK/Bridge`
- `src/iCAX-UI/SDK/Mailbox`
- `src/iCAX-UI/SDK/PDO`
- `src/iCAX-UI/UI`
- `src/iCAX-UI/SDK`
- `src/apps/flat-laser-cam/product.manifest.json`
- `src/apps/flat-laser-cam/webpage/entry.mjs`
- `src/tests/iCAX-UI/SDKTest.mjs`

旧 `X Other/legacy/icax-workbench` 和 `X Other/legacy/apps/flat-laser-cam/frontend` 属于上一版实验路径，不再作为新架构入口。
