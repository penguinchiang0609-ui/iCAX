# Facades 方案文档

## 1. 实现组成

代码位于 `src/iCAX-UI/SDK/Facades/`：

- `facadeClient.mjs`：双向调用、汇报、响应、事件、超时和生命周期；
- `facadeMethod.mjs`：与 C++ 一致的稳定方法编码；
- `variantSerializer.mjs`：JavaScript 值与 Variant 文本互转；
- `channelId.mjs`：channel ID 校验。

## 2. 对称调用模型

```text
前端调用后端：FacadeClient.invoke() -> Promise
后端调用前端：FacadeClient.expose() 处理 Request -> Response
```

方向只决定哪一端发 Request。双方使用相同的 `callId/methodCode/kind/status/payloadText` frame 契约；Request、Report 和 Response 共享同一个 `callId`。

## 3. Pending Promise

`FacadeClient` 维护：

```text
callId -> { resolve, reject, timer, reportHandlers }
```

收到 Report 时更新最近汇报、通知 handler 并重置超时；收到 Response 时删除 pending，再根据状态 resolve 或 reject。找不到 pending 的迟到响应不会影响其他调用。

`postFacadeFrame()` 只表示 frame 已投递，不表示业务完成。业务完成只由后续 Response 表达，因此 Bridge 不同步阻塞等待业务线程。

## 4. 前端方法分发

`expose(channelId, facadeMethod, handler)` 按 channel 和方法码登记前端实现。收到 Request 后：

```text
deserialize payload
  -> await handler(payload, context)
  -> optional context.report(payload)
  -> post Response
```

方法不存在返回 `MethodNotFound`，handler 抛错返回 `InvalidInvocation`。调用执行与结果都通过 Promise/async 传播，不在 Bridge 收帧动作中同步等待后端或前端业务结束。

## 5. 事件

Event 不进入 pending map。Client 按 `channelId:methodCode` 和 `channelId:*` 两类 key 分发给精确订阅与通配订阅。

## 6. 验收标准

- 前端调用后端可由 Response 正确完成或拒绝 Promise；
- 后端 Request 可调用 `expose()` 的前端方法，并收到 Report 与 Response；
- Request、Report、Response 的 `callId` 保持一致；
- timeout 会拒绝 Promise，迟到响应不会二次完成；
- Event 与取消订阅行为正确；
- Variant 文本往返后结构一致；
- 64 位方法码在 JavaScript 中不发生精度损失。
