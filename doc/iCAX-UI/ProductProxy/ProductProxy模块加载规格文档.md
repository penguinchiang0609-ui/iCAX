# ProductProxy 模块加载规格文档

## 1. 定位

`ProductProxy` 模块中的 `productModuleLoader` 是产品前端模块加载能力。

它负责从 manifest 指定的 `webpage.entry` 加载产品 ESM 模块。

## 2. 产品模块接口

产品 webpage 模块可导出：

```js
export async function mountProduct(context) {}
export async function mountProject(context) {}
```

至少需要提供其中一个挂载函数。

## 3. Entry 规则

`webpage.entry` 可以是：

- 绝对路径。
- `apps/` 开头的产品入口标识，如 `apps/laser-3d-cam/webpage/entry.mjs`，由加载器解析到 `src/apps/laser-3d-cam/webpage/entry.mjs`。

默认不允许 `http(s):` 或 `file:` 外部入口。只有显式传入 `allowExternalEntries` 时才允许外部入口；产品化 manifest 应使用 `apps/<product-id>/webpage/entry.mjs`。

## 4. 边界

本模块不实现具体产品页面，只负责加载、缓存和挂载分发。

## 5. Product Context

调用 `mountProduct(context)` 或 `mountProject(context)` 时，context 至少包含：

- `product`：产品状态快照。
- `project`：项目状态快照，product 模式下可以为空。
- `mount`：DOM 挂载节点。
- `mode`：`product` 或 `project`。
- `actions`：Shell 提供的通用动作。

产品模块可以在 `mount` 下渲染自己的 UI。

## 6. 加载缓存

产品模块按 `productId` 缓存。

同一个产品重复进入 product/project 状态时，不重复 import 模块。

挂载函数可以同步返回，也可以返回 Promise。Shell 必须捕获挂载错误并展示，不允许形成反复 render 的错误循环。

## 7. 错误处理

以下情况应抛出明确错误：

- `productId` 为空。
- `frontendEntry` 为空。
- entry 路径无法解析。
- ESM 模块没有导出 `mountProduct` 或 `mountProject`。

## 8. 约束

产品模块不直接依赖 `window.icax`。

需要 backend 能力时，通过 context 中的 runtime/action 或 SDK 调用。

产品模块不得修改 Shell 的全局状态对象，只能通过 context 暴露的动作请求 Shell 更新状态。
