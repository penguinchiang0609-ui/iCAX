# Bridge 方案文档

## 1. 当前实现

代码位置：

```text
src/iCAX-UI/SDK/Bridge/
```

文件：

- `createBridge.mjs`
- `mockHostBridge.mjs`

## 2. 创建策略

```text
globalThis.icax
  -> globalThis.icaxNativeBridge
    -> MockHostBridge
```

`createBridge()` 统一返回可用 bridge。

## 3. Mock 方案

`MockHostBridge` 模拟：

- application Facade。
- product Facade。
- scene Facade。
- 产品启动。
- 项目打开与主 Scene 登记。
- Facade response。

它只用于开发期和前端白盒测试，不作为产品运行时能力。

## 4. 真实 Bridge 接入方案

真实宿主注入对象统一命名为：

```js
window.icax
```

兼容开发调试时也允许：

```js
window.icaxNativeBridge
```

`createBridge()` 只负责选择 bridge，不在这里补业务兜底逻辑。

## 5. Mock 命令处理

`MockHostBridge` 内部用 command methodCode 分发 mock command。

模拟内容：

- `App.GetState`
- `App.ListProducts`
- `App.StartProduct`
- `App.OpenProjectFile`
- `Product.GetState`
- `Product.OpenProjectCatalog`
- `Project.GetState`
- `Project.Undo`
- `Project.Redo`
- `Project.GetUndoRedoState`
- backend 主动 Event，通过 `emitEvent(channelId, facadeMember, payload)` 触发。

mock 使用和真实 bridge 一样的 Facade frame 返回 Report、Response 和 Event，确保测试覆盖真实交互形态；它也可注入 Request，验证后端调用前端 Facade。

## 6. 验收标准

- `createBridge()` 在存在 `globalThis.icax` 时返回真实对象。
- `createBridge()` 在没有真实对象时返回 `MockHostBridge`。
- mock bridge 能完成 application/product/project 基础流程。
- `subscribeFacadeFrames()` 返回的取消订阅函数生效。
- `postFacadeFrame()` 与真实宿主使用同一 frame 字段和异步投递语义。

