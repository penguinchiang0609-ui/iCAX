# UI 方案文档

## 1. 当前实现

代码位置：

```text
src/iCAX-UI/UI/
```

文件：

- `html.mjs`

## 2. HTML 工具

`escapeText()` 用于文本节点内容。

`escapeAttr()` 用于属性值。

## 3. 公共 UI 扩展方案

公共 UI 组件应该优先放在本模块，而不是散落在 `AppShell` 或具体产品里。

产品自己的强业务面板仍然放在 `src/apps/<product-id>/webpage`。

## 4. 文件组织建议

随着 UI 增多，本模块可以扩展为：

```text
UI/
  html.mjs
  controls/
  layout/
  theme/
  icons/
```

当前只保留 `html.mjs`，因为公共组件体系还没有稳定。

## 5. 使用规则

Shell 和产品页面使用公共 UI 时，应从 `SDK` 或 `UI` 引入。

示例：

```js
import { escapeText } from "../SDK/index.mjs";
```

## 6. 验收标准

- `escapeText()` 正确转义 `& < > "`。
- `escapeAttr()` 在 `escapeText()` 基础上转义 `'`。
- 公共 UI 文件不导入 Facade、PDO、runtime。

