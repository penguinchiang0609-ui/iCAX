# ProjectProxy 方案文档

## 1. 代码位置

```text
src/iCAX-UI/ProjectProxy/
  ProjectProxy.mjs
  README.md
```

## 2. 依赖

`ProjectProxy` 依赖：

- `SDK/Facades`
- `SceneProxy`

它不依赖 `AppProxy`，只通过构造参数持有上级 `ProductProxy` 引用。

## 3. Scene 同步流程

```text
ProjectProxy.syncScenes(projectState)
  -> collect projectState.mainScene + projectState.scenes
  -> adoptScene(sceneState)
      -> bridge.registerSceneChannel(projectId, sceneId)
      -> new/update SceneProxy
  -> dispose closed SceneProxy
```

标准项目方法由 `SceneProxy` 调用。`SDK/Facades/facadeMethod.mjs` 中的 `ProjectFacade` 表示后端对外提供的项目 Facade 方法：

```js
ProjectFacade.getState;
ProjectFacade.undo;
ProjectFacade.redo;
ProjectFacade.getUndoRedoState;
```

`SceneProxy` 在这些命令之上提供便捷方法：

```js
const scene = project.getMainScene();
await scene.getState();
await scene.undo();
await scene.redo();
const state = await scene.getUndoRedoState();
```

这些方法只表达前端公共约定。项目文件保存由具体产品/文件模块定义 Facade 方法，因为保存格式、路径策略和资源打包方式都属于产品协议。

## 4. PDO 流程

```text
scene state contains pdo descriptors
  -> new PDOClient(sceneState.pdo)
  -> product webpage reads scene.pdo
  -> bridge maps shared memory view
```

`ProjectProxy` 不挂 PDO。`SceneProxy` 只负责把 PDO 入口挂到代理对象上，不负责驱动渲染，也不负责解释业务二进制布局。
