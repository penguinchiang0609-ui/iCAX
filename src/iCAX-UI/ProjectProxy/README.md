# ProjectProxy

`ProjectProxy` 是前端项目代理模块，表达某个 backend project 的前端容器。

## 目录结构

- `ProjectProxy.mjs`：前端 project 代理对象。它持有项目状态快照、Scene 列表和主 Scene 代理。

## 标准能力

- `syncScenes(projectState)`：根据项目状态同步 Scene 代理。
- `adoptScene(sceneState)`：创建或更新单个 Scene 代理。
- `getScene(sceneId)`：获取指定 Scene 代理。
- `getMainScene()`：获取主 Scene 代理。
- `dispose()`：释放本项目下所有 Scene 订阅。

## 边界

本模块不持有 repository、resource pool、ECS 数据、Facade 或 PDO。项目级数据归 backend project 所有；可运行现场归 backend scene 所有。前端通过 `SceneProxy` 调用 Facade 方法、订阅事件和读取 PDO。
