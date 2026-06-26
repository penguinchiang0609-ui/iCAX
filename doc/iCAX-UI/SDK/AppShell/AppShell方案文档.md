# AppShell 方案文档

## 1. 当前实现

代码位置：

```text
src/iCAX-UI/SDK/AppShell/
```

主要文件：

- `index.html`
- `app/bootstrap.mjs`
- `theme/workbench.css`
- `dev-server.mjs`
- `run-app-shell.ps1`

## 2. 布局

```text
TopBar
ProductRail | Workspace | Inspector
BottomDock
```

`Workspace` 根据状态显示产品首页或项目工作区。

## 3. 模块依赖

`AppShell` 只依赖 SDK 公开入口：

- `connectApplication`
- `loadProductModule`
- `mountProductModule`
- `escapeText` / `escapeAttr`

产品模块动态加载和挂载分发由 SDK 导出的模块加载函数负责，Shell 只维护当前应用状态和页面骨架。

## 4. 开发运行

```powershell
node src\iCAX-UI\SDK\AppShell\dev-server.mjs
```

没有真实宿主注入时，自动使用 mock bridge。

## 5. 渲染方案

当前 Shell 采用轻量模板字符串渲染。

渲染入口：

```text
render()
  -> renderTopBar()
  -> renderProductRail()
  -> renderWorkspace()
  -> renderInspector()
  -> renderBottomDock()
```

DOM 更新后调用 `mountActiveProductSurface()`，将产品模块挂载到当前 product/project surface。

## 6. 事件方案

Shell 在根节点上使用事件委托：

```text
click [data-action]
  -> refresh
  -> select-product
  -> start-product
  -> open-project
  -> choose-project
  -> undo-project
  -> redo-project
```

这种方式避免每次 render 后重复绑定大量 DOM 事件。事件处理统一走 `runAction()`，保证异步错误会进入 Shell 日志，而不是形成未处理 Promise。

## 7. 产品模块加载

Shell 不直接解析产品入口。

加载流程：

```text
mountActiveProductModule()
  -> ProductProxy.loadProductModule()
  -> cache by productId
```

挂载流程：

```text
mountActiveProductSurface()
  -> ProductProxy.mountProductModule()
  -> catch mount error and record productSurfaceErrorKey
```

产品模块挂载可以返回 Promise。Shell 捕获挂载错误，并用 `productSurfaceErrorKey` 避免同一个产品/模式的错误在下一次 render 中反复触发。

## 8. 验收

开发服务验证：

```powershell
node src\iCAX-UI\SDK\AppShell\dev-server.mjs
```

访问：

```text
http://127.0.0.1:4173/
```

验收标准：

- `index.html` 返回 200。
- `bootstrap.mjs` 返回 200。
- 产品入口模块返回 200。
- mock bridge 下可以完成 application/product/project 流程。


