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
- `registerProjectChannel(projectId)`
- `subscribeMail(channelId, handler)`
- `postMail(mail)`

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

### 5.3 registerProjectChannel

```js
const channelId = await bridge.registerProjectChannel(projectId);
```

登记已打开项目的 channel，并返回 project channel id。

要求：

- 只能在项目已经由 backend 创建后调用。
- 调用失败时抛出异常或 reject。
- 重复登记同一项目应返回同一个 channel id。

### 5.4 subscribeMail

```js
const unsubscribe = bridge.subscribeMail(channelId, handler);
```

订阅指定 mailbox 的返回消息或事件。

该回调会收到两类 mail：

- response mail：`originId != 0`，对应前端某个 request。
- event mail：`originId == 0`，由 backend 主动发送。

要求：

- `handler` 接收 mail envelope。
- 返回值必须是可调用的取消订阅函数。
- 同一个 mailbox 可以有多个订阅者。
- 取消订阅后不得再调用该 handler。

### 5.5 postMail

```js
await bridge.postMail(envelope);
```

将前端 envelope 投递到 backend。

要求：

- `postMail` 只表达投递成功，不表达业务成功。
- 业务成功或失败通过后续 response mail 表达。
- `postMail` 不允许同步阻塞等待 backend 执行业务命令。

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

要求：

- reader 执行期间数据视图有效。
- reader 返回后，前端不得继续持有并异步使用该租约视图。
- 如果 PDO 不存在或版本不匹配，应抛出明确错误。

## 6. Mock 行为

mock bridge 用于开发期和白盒测试。

mock 必须满足真实 bridge 的最小接口，但不承诺性能和 shared memory 语义。

产品代码不得依赖 mock bridge 的私有字段。
