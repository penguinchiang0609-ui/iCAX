# iCAX-UI 规格文档

## 1. 定位

iCAX-UI 是 iCAX 桌面应用的 H5 前端框架层：

```text
WebPageHost + WebPage
```

- `WebPageHost` 是 H5 adapter 核心，连接 UI 宿主和 `iCAX-Application` 的 `FrontendBridge`。
- `CefWebPageHost` 是当前 CEF 浏览器宿主适配层。
- `WebPage` 是 H5 单页工作台，由 `AppShell` 和产品 `webpage` 模块组成。

iCAX-UI 不拥有 Engine。Engine 生命周期由 `iCAX-Application` 管理，因此未来 Qt/WPF UI 可以复用同一个应用容器。

## 2. 源码位置

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

src/apps/<product-id>/
  product.manifest.json
  backend/
  webpage/
  protocol/
```

具体产品不放在 `src/iCAX-UI` 下面。每个产品是独立目录，产品自己的 backend 扩展和 webpage 扩展都放在 `src/apps/<product-id>/`。

## 3. 模块划分

- `iCAX-Application/Application`：应用容器，拥有 Engine 和通用 `FrontendBridge`。
- `WebPageHost`：H5 adapter 核心，不拥有 Engine。
- `CefWebPageHost`：CEF 宿主适配层，负责浏览器窗口和 JS bridge。
- `SDK/AppShell`：SDK 自带的 H5 单页应用壳。
- `AppProxy`：前端应用代理，封装 application channel、产品列表、产品启动和按文件打开项目。
- `ProductProxy`：前端产品代理，封装 product channel、产品状态、产品级事件、项目打开入口和产品 webpage 加载。
- `ProjectProxy`：前端项目代理，封装 project channel、项目状态、项目级命令和 PDO 访问。
- `UI`：公共 UI 工具和组件。
- `SDK`：对 Shell 和产品前端暴露的统一导出入口，内部封装 Bridge、Mailbox、PDO。

`AppProxy/ProductProxy/ProjectProxy` 是前端自己的三层结构，与 backend 概念对齐，但不拥有 backend 数据。它们只保存前端通信入口、状态快照、事件订阅和视图所需的轻量引用。

## 4. WebPageHost

`WebPageHost` 是连接 WebPage 和 `FrontendBridge` 的 adapter。

它负责：

- 绑定已经 attach 到 Engine 的 `CFrontendBridge`。
- 被 CEF/WebView/Qt 等 UI 宿主适配层调用。
- 将 JS 发来的 mail 交给 `FrontendBridge`。
- 从 `FrontendBridge` 取出 Engine response/event，交给宿主适配器推送到 H5。

`WebPageHost` 不写具体产品逻辑，不知道平切、五轴、焊接等业务细节，不启动或停止 `ApplicationHost`。

CEF 浏览器窗口、`window.icax` 注入、PDO shared memory 到 JS 视图的映射、文件对话框、窗口标题和拖拽文件属于 `CefWebPageHost` 或更外层宿主适配器。

## 5. WebPage

`WebPage` 只有一个 HTML 入口：

```text
src/iCAX-UI/SDK/AppShell/index.html
```

它内部按运行态切换：

```text
AppProxy workspace
  -> ProductProxy workspace
    -> ProjectProxy workspace
```

产品前端不是新网页，而是由 manifest 指向的 ESM 模块：

```json
{
  "webpage": {
    "entry": "apps/flat-laser-cam/webpage/entry.mjs"
  }
}
```

`AppShell` 动态 import 该模块并挂载产品页面或项目页面。

## 6. Bridge 契约

H5 侧只依赖 `window.icax`：

```ts
interface IcaxBridge {
  getApplicationChannelId(): Promise<string>;
  registerProductChannel(productId: string): Promise<string>;
  registerProjectChannel(projectId: string): Promise<string>;
  postMail(mail: IcaxMailEnvelope): void | Promise<void>;
  subscribeMail(channelId: string, handler: (mail: IcaxMailEnvelope) => void): () => void;
  openFileDialog?(options: OpenFileDialogOptions): Promise<string | null>;
  onProjectFileOpen?(handler: (path: string) => void): () => void;
  pdo?: IcaxPDOBridge;
}
```

邮件信封：

```ts
interface IcaxMailEnvelope {
  channelId: string;
  id: number;
  originId: number;
  typeCode: string;
  stamp: number;
  payloadText: string;
}
```

`payloadText` 是 UTF-8 文本。页面业务代码不直接拼底层协议文本，由 `SDK` 负责封装。

## 7. Mailbox Promise 语义

前端和 Engine 线程独立。H5 发出 mail 后不能同步得到业务结果。

所有命令 API 返回 `Promise`：

```js
const app = await icax.connectApplication();
const product = await app.startProduct("icax.flat-laser-cam");
const opened = await app.openProjectFile("D:/projects/a.ilcam");
```

完成规则：

- request mail 带唯一 `id`。
- response mail 的 `originId` 等于 request mail 的 `id`。
- `stamp == 0` 表示成功。
- 非 0 stamp 或超时都会 reject。

## 8. 运行流程

普通启动：

```text
CApplication.Start()
  -> Engine ApplicationHost.Start()
  -> FrontendBridge.Attach(ApplicationHost)
CEF/WebView/Qt host starts
  -> WebPageHost.Start()
  -> AppShell loads
  -> window.icax.getApplicationChannelId()
  -> App.GetState
  -> render product list
  -> App.StartProduct(productId)
  -> load product webpage entry
```

双击项目文件：

```text
OS passes file path to application container
  -> ApplicationHost resolves product by magic
  -> ProductRuntime opens Project
  -> AppShell loads product webpage entry
  -> render project workspace
```

项目文件归属只由 magic 决定，H5 不传产品 ID 来强行打开项目。

## 9. PDO

Mailbox 用于低频命令和事件；PDO 用于高频、可丢弃状态数据。

H5 不直接操作 PDO header。宿主适配器负责：

- 打开 shared memory arena。
- BeginRead / EndRead。
- BeginWrite / EndWrite。
- 将 payload 暴露为 `ArrayBuffer` / `DataView`。

产品前端只使用：

```js
project.pdo.withRead("PreviewMesh", "MainViewport", view => {
  // read payload
});
```

PDO 不做运行时字段自描述。产品双方提前约定 `typeName + instanceName -> PDOID`、payload version、payload size 和二进制布局。

## 10. 产品扩展

新增产品只新增：

```text
src/apps/my-product/
  product.manifest.json
  backend/
  webpage/
  protocol/
```

`product.manifest.json` 同时服务 backend 和 frontend：

- backend 根据 `backend.modules` 加载 DLL。
- frontend 根据 `webpage.entry` 加载产品 H5 模块。
- project file magic 用于双击文件时定位产品类型。

底层 framework 和 frontend 不出现具体产品代码。
