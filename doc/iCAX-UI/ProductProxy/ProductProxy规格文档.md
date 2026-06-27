# ProductProxy 规格文档

## 1. 定位

`ProductProxy` 是前端产品代理层。它表达某个 backend product runtime 的前端会话。

前端 product proxy 不拥有产品数据，只保存：

- product id。
- product channel id。
- product state 快照。
- 本产品下已打开 project 的 `ProjectProxy` 对象表。

## 2. 对外接口

构造 `ProductProxy` 时必须提供带 `productChannelId` 的 product state。

方法：

- `updateState(productState)`：更新产品状态快照。
- `getState()`：获取产品状态。
- `listProjectCatalogs()`：列出产品下的项目目录。
- `openProjectCatalog(projectPath, options)`：打开项目目录并返回 `ProjectProxy`。
- `closeProjectCatalog(catalogId)`：关闭项目目录。
- `subscribe(command, handler)`：订阅 product channel 的指定事件。
- `subscribeAll(handler)`：订阅 product channel 的所有事件。
- `getProject(projectId)`：获取已打开项目的前端对象。
- `adoptProject(projectState)`：根据项目状态创建或更新 `ProjectProxy`。

## 3. 边界

`ProductProxy` 模块包含两类能力：一类是 `ProductProxy` 类代表 backend product runtime 的前端通信代理，另一类是 `productModuleLoader` 负责加载产品 H5 ESM 模块。

`ProductProxy` 模块不解释产品协议。具体产品命令、事件和 PDO payload 由 `src/apps/<product-id>/protocol` 定义。

backend state 中的 `projectChannelId` 只表示 project runtime 的通信身份，不表示当前 H5 bridge 已经可以向该 channel 投递邮件。只要 `ProductProxy` 根据 catalog/project state 创建或更新 `ProjectProxy`，就必须调用 `bridge.registerProjectChannel(projectId)`，让原生宿主把 project frontend post office 注册到当前 bridge 会话。

`bridge.registerProjectChannel(projectId)` 返回的 channel id 才是本次前端会话实际可用的 `projectChannelId`。如果返回空、nil 或非法 channel id，必须立即抛出异常。

## 4. 验收要求

- product channel 不与 application/project channel 混用。
- 打开项目后可以自动登记 project channel。
- 产品级事件可以被订阅。
