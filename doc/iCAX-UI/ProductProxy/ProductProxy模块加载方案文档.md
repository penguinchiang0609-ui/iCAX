# ProductProxy 模块加载方案文档

## 1. 当前实现

代码位置：

```text
src/iCAX-UI/ProductProxy/
```

文件：

- `productModuleLoader.mjs`

## 2. 加载流程

```text
product.frontendEntry
  -> resolveFrontendEntry()
  -> dynamic import()
  -> cache by productId
  -> mountProductModule()
```

## 3. 挂载分发

`mountProductModule()` 根据 context mode 调用：

- `mode === "product"`：调用 `mountProduct()`。
- `mode === "project"`：调用 `mountProject()`。

## 4. 生命周期扩展点

产品模块卸载、热更新、错误边界可以在本模块扩展，不应塞回 `AppShell`。

建议扩展接口：

```js
export function unmountProduct(context) {}
export function unmountProject(context) {}
```

当前阶段不强制产品实现卸载函数；没有卸载函数时，Shell 清空挂载节点即可。

## 5. Entry 解析方案

`resolveFrontendEntry(entry, baseUrl)` 支持：

- `http://` 或 `https://` URL。
- `file:` URL。
- `/` 开头的绝对路径。
- `apps/` 开头的 src 相对产品路径。
- 普通相对路径。

`apps/` 产品入口标识以 `AppShell/app/bootstrap.mjs` 为基准解析到 `src/apps`。

## 6. 验收标准

- `apps/<id>/webpage/entry.mjs` 能正确解析到 `src/apps/<id>/webpage/entry.mjs`。
- 同一个 `productId` 重复加载时命中缓存。
- product 模式调用 `mountProduct()`。
- project 模式调用 `mountProject()`。
- 模块导出非法时抛出明确错误。

