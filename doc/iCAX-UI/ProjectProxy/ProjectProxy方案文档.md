# ProjectProxy 方案文档

## 1. 代码位置

```text
src/iCAX-UI/ProjectProxy/
  ProjectProxy.mjs
  README.md
```

## 2. 依赖

`ProjectProxy` 依赖：

- `SDK/Mailbox`
- `SDK/PDO`

它不依赖 `AppProxy`，只通过构造参数持有上级 `ProductProxy` 引用。

## 3. 命令流程

```text
ProjectProxy.execute(command, payload)
  -> MailboxClient.request(projectChannelId, command, payload)
  -> bridge.postMail()
  -> WebPageHost
  -> backend project command handler
  <- response mail
  -> Promise resolve/reject
```

标准项目命令由 `SDK/Mailbox/commandRoute.mjs` 中的 `ProjectCommands` 定义：

```js
ProjectCommands.getState;
ProjectCommands.undo;
ProjectCommands.redo;
ProjectCommands.getUndoRedoState;
```

`ProjectProxy` 在这些命令之上提供便捷方法：

```js
await project.getState();
await project.undo();
await project.redo();
const state = await project.getUndoRedoState();
```

这些方法只表达前端公共约定。项目文件保存由具体产品/文件模块定义命令，因为保存格式、路径策略和资源打包方式都属于产品协议。

## 4. PDO 流程

```text
project state contains pdo descriptors
  -> new PDOClient(projectState.pdo)
  -> product webpage reads project.pdo
  -> bridge maps shared memory view
```

`ProjectProxy` 只负责把 PDO 入口挂到代理对象上，不负责驱动渲染，也不负责解释业务二进制布局。
