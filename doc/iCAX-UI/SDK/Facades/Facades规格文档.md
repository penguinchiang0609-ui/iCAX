# Facades 规格文档

## 1. 定位

`Facades` 是前端 SDK 的调用边界。页面通过 Proxy 或 `FacadeClient` 调用后端 Facade，也可以用 `expose()` 向后端提供前端 Facade。公开 API 只表达方法、参数、汇报和结果，不要求页面理解内部传输实现。

## 2. 方法模型

公开名称固定为 `FacadeName.MethodName`，只允许两个 PascalCase 标识符段。例如 `Machine.Jog` 合法，`Cam.Machine.Import` 非法。实例信息必须放在参数中：

```js
facades.invoke(channelId, "Machine.Jog", { machineId, axis, delta });
```

前端与 C++ 对两个名称段执行相同的 FNV-1a 32 位编码，再组合为 64 位方法码。JavaScript 使用十进制字符串保存该值，避免超过安全整数范围。

## 3. 异步调用

`FacadeClient.invoke()` 立即返回 Promise，不同步等待后端。返回的 Promise 额外提供：

- `callId`：本次调用 ID；
- `onReport(handler)`：订阅中间汇报，并返回取消函数；
- `getLatestReport()`：读取最近一次汇报。

每次调用必须设置正数超时。响应状态非 `Ok`、投递失败或超时时 Promise reject；成功响应完成 Promise。Report 会刷新超时计时，但不会完成 Promise。

## 4. 内部帧

Bridge 内部传递的 frame 包含 `channelId`、`callId`、`methodCode`、`kind`、`status` 和 `payloadText`。`kind` 为 `Request`、`Report`、`Response` 或 `Event`。

同一次 Request、零到多个 Report 和唯一 Response 始终使用同一个 `callId`。不存在额外的消息 ID 或来源 ID。

## 5. 前端提供 Facade

```js
const unexpose = facades.expose(channelId, "Workbench.GetSelection", async (payload, context) => {
  await context.report({ stage: "reading" });
  return { selection: readSelection(payload) };
});
```

后端请求到达后，handler 以异步方式执行；返回值生成 Response，抛出的错误生成失败 Response。`context.report()` 可以发送中间汇报。

## 6. 事件与生命周期

- `subscribe()` 订阅指定 Facade 事件；
- `subscribeAll()` 订阅 channel 的全部事件；
- `stop()` 停止 channel、清理公开方法与订阅，并拒绝该 channel 的 pending 调用；
- `dispose()` 停止全部 channel 并释放 Client。

Facades 不承载 PDO 高频数据，也不把调用重新抽象为 Command 或 Target。
