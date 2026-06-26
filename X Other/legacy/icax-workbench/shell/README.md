# shell

`shell` 放置 H5 工作台壳的启动和生命周期代码。

Shell 是 H5 侧的 `FrontendHost`。它只负责应用启动、bridge 接入、全局错误边界和应用级状态初始化，不写具体产品业务页面。

## 目录结构

- `README.md`：本目录说明。
- `bootstrap.mjs`：H5 工作台入口，创建 bridge、MailboxClient、WorkbenchStore，并挂载主视图。

## 启动职责

```text
bootstrap
  -> create bridge adapter
  -> create mailbox client
  -> get application mail id
  -> App.GetState
  -> mount ApplicationWorkspace
```

所有 backend 命令都通过 mailbox 语义客户端返回 `Promise`。Shell 不假设 `postMail` 同步完成业务处理。
