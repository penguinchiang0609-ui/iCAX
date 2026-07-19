# ProductProxy 方案文档

## 1. 代码位置

```text
src/iCAX-UI/ProductProxy/
  ProductProxy.mjs
  productModuleLoader.mjs
  README.md
```

## 2. 依赖

`ProductProxy` 依赖：

- `Facades`
- `ProjectProxy`
- `Bridge` 的 product/scene channel 登记能力

产品模块加载能力依赖浏览器原生 dynamic import，不依赖 backend 数据结构。

## 3. 打开项目流程

```text
ProductProxy.openProjectCatalog(path)
  -> Facade.request(productChannelId, Product.OpenProjectCatalog)
  <- catalog.mainProject + mainScene
  -> adoptProject(mainProject)
  -> bridge.registerSceneChannel(projectId, mainSceneId)
  -> new/update ProjectProxy
  -> ProjectProxy.syncScenes()
  -> return ProjectProxy + main SceneProxy
```

## 4. 状态同步

`getState()` 和 `listProjectCatalogs()` 返回 catalog 信息时，`ProductProxy` 会扫描其中的 `mainProject` 和 `projects`，同步本地 project 前端对象表。

该对象表只服务于前端会话复用，不表达数据所有权。

## 5. 产品页面加载

`productModuleLoader.mjs` 负责：

- 解析 manifest 中的 `webpage.entry`。
- 动态 import 产品 `webpage` ESM 模块。
- 按 `productId` 缓存模块。
- 根据 Shell 的运行态分发到 `mountProduct()` 或 `mountProject()`。

产品页面仍属于 `src/apps/<product-id>/webpage/`，公共 `ProductProxy` 模块只负责加载机制。
