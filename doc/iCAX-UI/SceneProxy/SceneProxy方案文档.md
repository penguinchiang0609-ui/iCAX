# SceneProxy 方案文档

## 1. 代码位置

```text
src/iCAX-UI/SceneProxy/
  SceneProxy.mjs
  README.md
```

## 2. 依赖

`SceneProxy` 依赖：

- `SDK/SDO`
- `SDK/PDO`
- `Bridge` 提供的已注册 scene channel。

它不依赖 `AppProxy`，只通过构造参数持有上级 `ProjectProxy` 引用。

## 3. SDO 调用流程

```text
SceneProxy.invoke(sdoMethod, payload)
  -> SDOClient.invoke(sceneChannelId, sdoMethod, payload)
  -> bridge.postSDOFrame()
  -> UIContainer
  -> backend scene SDO invoker
  -> SDO(application, product, project, scene)
  <- Response frame
  -> Promise resolve/reject
```

标准方法由 `SDK/SDO/sdoMethod.mjs` 中的 `ProjectSDO` 定义。这里保留 `Project` SDO 名，是因为方法操作的是项目数据；当前 Scene channel 只是 SDO 的传输边界，不属于 SDO 方法名。

## 4. PDO 流程

```text
scene state contains pdo descriptors
  -> new PDOClient(sceneState.pdo, bridge)
  -> product webpage reads scene.pdo
  -> bridge maps shared memory view
```

`SceneProxy` 只负责提供 PDO client，不解释 payload。PDO 的 `typeName + instanceName`、版本和二进制布局由产品协议定义。

## 5. 状态刷新

`SceneProxy.getState()` 通过当前 Scene channel 请求后端状态。后端响应通常是 project state，并带有 `activeScene`。前端优先使用 `activeScene` 更新当前代理；如果响应中没有 `activeScene`，则从 `mainScene` 和 `scenes` 中按 `sceneId` 查找。
