# icax-workbench Tests

`icax-workbench` 测试目录与 `src/icax-workbench` 一一对应，放置 H5 工作台测试。

## 目录结构

- `mailboxClientTest.mjs`：验证 H5 Mailbox request/response Promise 语义、超时处理，以及 `App.OpenProjectFile` 不需要前端传产品 ID。
