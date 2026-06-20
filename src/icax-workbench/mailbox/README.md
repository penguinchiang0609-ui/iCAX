# mailbox

`mailbox` 放置 H5 侧 Mailbox 客户端封装。这里负责消息发送、响应分发、事件订阅和错误处理。

Mailbox 用于普通命令、请求响应和低频状态同步。H5 页面不应直接拼底层 mail，应通过 AppClient、ProductClient、ProjectClient 这样的语义客户端发送命令。

## 目录结构

- `README.md`：本目录说明。
- `commandNames.mjs`：通用应用级、产品级命令名。
- `mailboxClient.mjs`：request id 分配、pending promise、response mail 分发、超时和错误处理。
- `clients.mjs`：AppClient、ProductClient、ProjectClient 语义客户端。

## 客户端层级

```text
AppClient      -> application mail id
ProductClient  -> product mail id
ProjectClient  -> project mail id
```

## 请求响应语义

前端和后端运行在不同线程中，`postMail` 只表示请求已经投递，不能同步得到业务结果。

`MailboxClient.send()` 会立即返回 `Promise`，并记录 request mail 的 `id`。后端处理完成后，需要投递一个 response mail，response mail 的 `originId` 必须等于 request mail 的 `id`。只有订阅到这个 response mail 后，前端 Promise 才会 resolve 或 reject。

```js
const state = await appClient.getState();
await appClient.openProjectFile("D:/projects/sample.icax");
```

请求 mail：

```js
{
  id: 1,
  originId: 0,
  command: "App.GetState",
  stamp: "Ok",
  payload: {}
}
```

响应 mail：

```js
{
  id: 100,
  originId: 1,
  command: "App.GetState",
  stamp: "Ok",
  payload: { /* command result */ }
}
```
