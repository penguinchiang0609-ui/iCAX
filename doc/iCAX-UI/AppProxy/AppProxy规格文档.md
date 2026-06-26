# AppProxy 规格文档

## 1. 定位

`AppProxy` 是前端应用代理层。它是 H5 进入 Engine 的第一层对象，对应 Engine `ApplicationHost` 的前端会话表达。

前端 app proxy 不拥有业务数据，只保存：

- application channel id。
- 全局 `MailboxClient`。
- application state 快照。
- 已启动 product 的 `ProductProxy` 对象表。

## 2. 对外接口

创建：

```js
const app = await AppProxy.create(bridge);
```

方法：

- `getState()`：获取 application state。
- `listProducts()`：获取可用产品和已启动产品状态。
- `startProduct(productId)`：启动产品并返回 `ProductProxy`。
- `stopProduct(productId)`：停止产品并移除前端 product 对象。
- `resolveProjectFile(projectPath)`：按文件 magic 判断项目归属产品。
- `openProjectFile(projectPath, options)`：按文件路径打开项目。
- `openProject(projectPath, options)`：`openProjectFile` 的实现入口。
- `subscribe(command, handler)`：订阅 application channel 的指定事件。
- `subscribeAll(handler)`：订阅 application channel 的所有事件。
- `getProduct(productId)`：获取已启动产品的前端对象。

## 3. 生命周期

`AppProxy.create()` 必须先从 bridge 获取 application channel，然后创建 `MailboxClient`。

产品启动、停止、项目文件打开都通过 application channel 发起。`AppProxy` 收到 backend state 后，同步本地 `ProductProxy` 对象表。

## 4. 错误处理

缺少 `bridge`、`mailboxClient` 或 application channel id 时立即抛出异常。命令失败和超时由 `MailboxClient` 转换为 rejected Promise。

## 5. 验收要求

- 页面可以只通过 bridge 创建 `AppProxy`。
- 可以列出产品、启动产品、停止产品。
- 可以按项目文件打开项目，并返回对应的 `ProductProxy` 与 `ProjectProxy`。
- application 事件可以被订阅。
