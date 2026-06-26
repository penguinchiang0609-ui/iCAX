# AppProxy 方案文档

## 1. 代码位置

```text
src/iCAX-UI/AppProxy/
  AppProxy.mjs
  README.md
```

## 2. 依赖

`AppProxy` 依赖：

- `Mailbox`
- `ProductProxy`
- `Bridge` 的 bridge 契约

它不依赖 UI、PDO 或具体产品模块。

## 3. 创建流程

```text
AppProxy.create(bridge)
  -> bridge.getApplicationChannelId()
  -> new MailboxClient(bridge)
  -> new AppProxy(mailboxClient, applicationChannelId)
```

## 4. 产品同步流程

```text
App.GetState / App.ListProducts / App.StartProduct / App.OpenProjectFile
  -> backend returns application state
  -> AppProxy.#syncProductsFromState()
  -> adoptProduct(productState)
  -> bridge.registerProductChannel(productId)
  -> new/update ProductProxy
```

`registerProductChannel` 只是让 `FrontendBridge` 缓存 product post office，不代表业务启动。业务启动由 `App.StartProduct` 决定。

## 5. 按文件打开项目

```text
AppProxy.openProjectFile(path)
  -> App.OpenProjectFile
  -> Engine reads file magic and resolves product
  -> Engine opens project
  <- application state + product state + catalog state
  -> adoptProduct(product)
  -> product.adoptProject(catalog.mainProject)
```

H5 不传 product id 来强制打开项目，项目文件归属以 magic 为准。
