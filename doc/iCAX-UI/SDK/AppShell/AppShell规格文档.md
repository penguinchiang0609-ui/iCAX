# AppShell 规格文档

## 1. 定位

`AppShell` 是唯一 H5 页面入口。

它是桌面应用壳，不是传统网站，也不做多 HTML 页面跳转。

## 2. 对外行为

`AppShell` 必须：

- 初始化 SDK。
- 连接 `window.icax`。
- 获取 application Facade 入口。
- 查询可用产品。
- 启动产品。
- 打开项目文件。
- 动态加载产品 webpage 模块。
- 提供产品和项目 UI 的挂载点。

## 3. 页面状态

```text
AppProxy
  -> ProductProxy
    -> ProjectProxy
```

同一个 HTML 页面承载全部状态切换。

## 4. 边界

`AppShell` 不实现产品页面，不解释 PDO 二进制数据，不直接访问 backend C++ 对象。

## 5. 状态模型

`AppShell` 维护前端工作台状态：

- `bridgeStatus`：宿主 bridge 连接状态。
- `bridge`：当前使用的宿主 bridge 或 mock bridge。
- `appState`：application state 快照。
- `activeProductState`：当前选中的 product state 快照。
- `activeProductProxy`：当前产品代理对象。
- `activeProjectState`：当前项目 state 快照。
- `activeProjectProxy`：当前项目代理对象。
- `productModules`：已加载产品 ESM 模块缓存。
- `productSurfaceErrorKey`：产品挂载失败保护键，避免同一挂载错误反复 render。
- `pendingCount`：未完成请求数量。
- `logs`：前端操作日志。
- `error`：当前错误信息。

状态只作为 UI 渲染输入，不作为 backend 数据源。

## 6. 交互流程

启动流程：

```text
bootstrap()
  -> connectApplication()
  -> App.GetState
  -> render
```

产品流程：

```text
selectProduct(productId)
startProduct(productId)
  -> App.StartProduct
  -> load product webpage module
  -> render product workspace
```

项目流程：

```text
openProjectFromPath(path)
  -> App.OpenProjectFile
  -> update product/project state
  -> load product webpage module
  -> render project workspace
```

项目公共动作：

```text
undoProject()
  -> Project.Undo
  -> Project.GetState
  -> refresh project undoRedo state

redoProject()
  -> Project.Redo
  -> Project.GetState
  -> refresh project undoRedo state
```

项目文件保存不属于公共 Shell 固定动作，应由具体产品/文件模块提供命令和 UI。

## 7. 产品挂载点

`AppShell` 提供两个产品挂载点：

- `data-product-surface="product"`：产品主页模式。
- `data-product-surface="project"`：项目工作区模式。

挂载分发由 `ProductProxy` 完成。

## 8. 错误处理

Shell 只负责展示错误。

错误来源：

- bridge 初始化失败。
- Facade 请求失败。
- 产品模块加载失败。
- 产品模块挂载失败。
- 用户输入为空或非法。

Shell 捕获用户动作中的异步异常，记录到日志，并显示在底部日志区域。产品模块挂载失败时，Shell 必须阻止同一个挂载点反复触发 render 错误循环。

## 9. 验收要求

- 没有真实宿主时可以通过 mock bridge 启动。
- 可以显示产品列表。
- 可以启动产品。
- 可以打开 mock 项目。
- 项目工作区可以触发撤销/重做并刷新按钮状态。
- 可以加载产品 `webpage.entry`。
- 不出现多 HTML 页面跳转。

