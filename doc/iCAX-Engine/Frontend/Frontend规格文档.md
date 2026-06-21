# Frontend 规格文档

## 1. 定位

Frontend 是 iCAX 的 H5 工作台前端。它运行在宿主 WebView 或浏览器容器中，通过 bridge 与 C++ backend 交互。

Frontend 不解析 Repository，不直接持有 ResourcePool，不直接调用 C++ 服务。它只保存 UI 状态和 backend 返回的轻量运行状态。

## 2. 层级

```text
FrontendHost
  ApplicationWorkspace
    ProductWorkspace*
      ProjectWorkspace*
```

- `FrontendHost`：H5 应用壳，负责启动、bridge 接入、全局状态、应用级命令和错误边界。
- `ApplicationWorkspace`：应用级页面，负责展示产品清单、启动产品、处理双击文件打开。
- `ProductWorkspace`：产品级工作台，负责最近项目、打开项目、产品设置和产品级面板。
- `ProjectWorkspace`：项目级工作台，负责编辑视图、属性面板、资源面板、日志、撤销重做等项目操作。

## 3. 主界面布局

```text
┌──────────────────────────────────────────────────────────────┐
│ App Bar: 产品切换  当前项目  命令入口  全局状态  设置        │
├───────────────┬───────────────────────────────┬──────────────┤
│ Product Rail  │ Workspace                     │ Inspector    │
│ 产品运行入口   │ 产品首页 / 项目编辑区          │ 属性/上下文   │
├───────────────┴───────────────────────────────┴──────────────┤
│ Bottom Dock: 日志 / 任务 / 诊断 / 命令结果                    │
└──────────────────────────────────────────────────────────────┘
```

没有产品启动时，`Workspace` 显示产品清单。产品启动后，`Workspace` 显示该产品的 `ProductWorkspace`。项目打开后，`ProductWorkspace` 内部显示 `ProjectWorkspace`。

## 4. 启动流程

普通启动：

```text
H5 bootstrap
  -> bridge.getApplicationMailId()
  -> App.GetState
  -> render product list
  -> user selects product
  -> App.StartProduct(productId)
  -> load product frontend entry
  -> render ProductWorkspace
```

双击项目文件启动：

```text
H5 bootstrap receives file path
  -> App.OpenProjectFile(projectPath)
  -> backend resolves product by project file magic
  -> backend starts ProductRuntime
  -> backend opens ProjectCatalog
  -> H5 loads product frontend entry
  -> render ProductWorkspace + ProjectWorkspace
```

文件归属产品只由项目文件 magic 决定。H5 不传产品 ID 来打开项目文件。

## 5. 应用级命令

FrontendHost 通过 application mailbox 使用：

- `App.GetState`
- `App.ListProducts`
- `App.StartProduct`
- `App.StopProduct`
- `App.ResolveProjectFile`
- `App.OpenProjectFile`

`App.OpenProjectFile` 只接收项目文件路径和可选显示名称，不接收产品 ID。

## 6. 产品级命令

ProductWorkspace 通过 product mailbox 使用：

- `Product.GetState`
- `Product.ListProjectCatalogs`
- `Product.OpenProjectCatalog`
- `Product.CloseProjectCatalog`

产品级 mailbox 由 `App.StartProduct` 或 `App.OpenProjectFile` 返回的 `productMailId` 建立。

## 7. 项目级命令

ProjectWorkspace 通过 project mailbox 发送项目命令。项目命令由具体产品或项目模块扩展。

通用项目命令建议覆盖：

- 保存项目。
- 撤销、重做。
- 查询项目状态。
- 查询资源和实体摘要。
- 执行产品业务命令。

## 8. Bridge 要求

H5 侧期望宿主提供一个 bridge 对象：

```ts
interface IcaxMail {
  id: string | number;
  originId?: string | number;
  command: string;
  typeCode?: string;
  stamp?: string;
  payload?: unknown;
}

interface IcaxBridge {
  getApplicationMailId(): Promise<string>;
  postMail(mailId: string, mail: IcaxMail): void | Promise<void>;
  subscribeMail(mailId: string, handler: (mail: IcaxMail) => void): () => void;
  subscribePDO?(pdoId: string, handler: (value: unknown) => void): () => void;
  openFileDialog?(options: OpenFileDialogOptions): Promise<string | null>;
  onProjectFileOpen?(handler: (path: string) => void): () => void;
}
```

具体 bridge 可以由 WebView2、Qt WebEngine、CEF 或浏览器调试适配器实现。Frontend 只依赖这个抽象，不依赖宿主技术。

## 9. Mailbox Promise 语义

前端线程和后端线程相互独立。前端调用 `postMail` 之后，只能说明 request mail 已投递，不能立即得到业务结果。

前端所有命令 API 都返回 `Promise`：

```js
const state = await appClient.getState();
const opened = await appClient.openProjectFile("D:/projects/sample.icax");
```

Promise 的完成规则：

- request mail 必须带有唯一 `id`。
- request mail 应同时带 `command` 和 `typeCode`。`command` 是调试友好的文本名，`typeCode` 是 C++ `MailHeader::nTypeCode` 使用的 `uint64` 十进制字符串。
- request mail 的 `originId` 为 `0` 或空，表示它不是响应。
- response mail 的 `originId` 必须等于 request mail 的 `id`。
- `stamp == "Ok"` 时 Promise resolve `payload`。
- `stamp != "Ok"` 时 Promise reject，并保留 response mail 作为错误上下文。
- 超时未收到 response mail 时 Promise reject timeout。

页面代码不应假设 `postMail` 有返回值，也不应在发送后同步读取业务结果。

## 10. 约束

- 前端 UI 状态和 backend 数据状态分开维护。
- 前端不缓存完整项目数据，只缓存视图需要的摘要和 PDO 快照。
- 大资源不走 Mailbox payload，使用 Resource 机制或宿主提供的资源 URL。
- 高频状态通过 PDO，普通命令和请求响应通过 Mailbox。
- 产品 UI 通过 `frontendEntry` 加载，通用壳不写具体产品业务页面。
