# Bridge 规格文档

## 1. 定位

`Bridge` 是 SDK 内部的 H5 与原生宿主 bridge 适配模块。

## 2. Bridge 契约

H5 侧只依赖：

```js
window.icax
```

bridge 至少需要提供：

- `getApplicationChannelId()`
- `registerProductChannel(productId)`
- `registerSceneChannel(projectId, sceneId)`
- `subscribeFacadeFrames(channelId, handler)`
- `postFacadeFrame(frame)`

可选宿主能力：

- `openFileDialog(options)`
- `pdo.withRead(options, reader)`

## 3. 行为

真实宿主存在时使用真实 bridge。

开发环境没有真实宿主时，可以使用 mock bridge。

`createBridge()` 会校验真实 bridge 的必需方法。产品化宿主可以传入 `{ allowMock: false }`，禁止缺失真实宿主时自动启用 mock。

## 4. 边界

本模块不处理业务命令，不解释 payload，不维护应用状态。产品页面不直接依赖本模块，应通过 `SDK/index.mjs` 和三层 Proxy 访问宿主能力。

## 5. 方法规格

### 5.1 getApplicationChannelId

```js
const channelId = await bridge.getApplicationChannelId();
```

返回 application 级 channel id 字符串。

要求：

- 返回值不能为空。
- 返回值必须是 backend 可识别的 channel id。
- 调用失败时抛出异常或 reject。

### 5.2 registerProductChannel

```js
const channelId = await bridge.registerProductChannel(productId);
```

登记已启动产品的 channel，并返回 product channel id。

要求：

- 只能在 `App.StartProduct` 成功后调用。
- 调用失败时抛出异常或 reject。
- 重复登记同一产品应返回同一个 channel id。

### 5.3 registerSceneChannel

```js
const channelId = await bridge.registerSceneChannel(projectId, sceneId);
```

登记已打开 Scene 的 channel，并返回 scene channel id。

要求：

- 只能在项目和 Scene 已经由 backend 创建后调用。
- 调用失败时抛出异常或 reject。
- 重复登记同一 Scene 应返回同一个 channel id。

### 5.4 subscribeFacadeFrames

```js
const unsubscribe = bridge.subscribeFacadeFrames(channelId, handler);
```

订阅指定 channel 上到达的 Facade frame。

frame 的 `kind` 包括：

- `Request`：后端调用前端提供的 Facade；
- `Report`：一次调用的中间汇报；
- `Response`：完成对应 Promise；
- `Event`：对端主动发送的事件。

要求：

- `handler` 接收 Facade frame。
- 返回值必须是可调用的取消订阅函数。
- 同一个 channel 可以有多个订阅者。
- 取消订阅后不得再调用该 handler。

### 5.5 postFacadeFrame

```js
await bridge.postFacadeFrame(frame);
```

将前端 Facade frame 投递到 backend。

要求：

- `postFacadeFrame` 只表达投递成功，不表达业务成功；
- 业务成功或失败通过后续 Response 表达；
- `postFacadeFrame` 不允许同步阻塞等待 backend 执行业务方法；
- Request、Report 和 Response 必须保持同一个 `callId`。

### 5.6 openFileDialog

```js
const path = await bridge.openFileDialog(options);
```

调用宿主文件选择能力。

取消选择时返回空字符串、`null` 或 `undefined` 均可，调用方必须按未选择处理。

### 5.7 pdo.withRead

```js
await bridge.pdo.withRead(options, reader);
```

获取 PDO 读租约。

`options` 至少包含：

- `arenaName`
- `id`
- `version`
- `payloadSize`

reader 形态：

```js
const result = bridge.pdo.withRead(options, (buffer, meta) => {
  return decode(buffer, meta);
});
```

要求：

- reader 必须同步执行，不允许返回 `Promise`。
- reader 执行期间数据视图有效。
- reader 返回后，前端不得继续持有并异步使用该租约视图。
- 如果 PDO 不存在或版本不匹配，应抛出明确错误。
- CEF 宿主下 `buffer` 是 shared memory payload 的 V8 快照，不是 JS 层 external shared memory。

## 6. Mock 行为

mock bridge 用于开发期和白盒测试。

mock 必须满足真实 bridge 的最小接口，但不承诺性能和 shared memory 语义。

产品代码不得依赖 mock bridge 的私有字段。
