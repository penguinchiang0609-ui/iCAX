# ProjectProxy 规格文档

## 1. 定位

`ProjectProxy` 是前端项目代理层。它表达某个 backend project 的前端容器。

前端 project proxy 不拥有 repository、resource pool、ECS 或项目文件数据，只保存：

- project id。
- project state 快照。
- Scene 状态表。
- 主 `SceneProxy`。

## 2. 对外接口

构造 `ProjectProxy` 时必须提供带 `projectId` 的 project state。Project 本身不拥有 channel，也不直接暴露 PDO。

方法：

- `updateState(projectState)`：更新项目状态快照。
- `syncScenes(projectState)`：按 project state 创建、更新和释放 `SceneProxy`。
- `adoptScene(sceneState)`：根据 scene state 创建或更新单个 `SceneProxy`。
- `getScene(sceneId)`：获取指定 Scene 的前端代理。
- `getMainScene()`：获取主 Scene 的前端代理。

属性：

- `mainSceneId`：主 Scene ID。
- `scenes`：SceneProxy 表。

## 3. Scene 边界

`ProjectProxy` 只表达项目容器，不执行业务命令，不订阅 Scene 事件，不读取 PDO。可运行现场由 `SceneProxy` 表达。业务命令、撤销重做和 PDO 入口都走 `SceneProxy`。

## 4. 验收要求

- Project 本身不得要求独立通信通道字段。
- `ProjectProxy` 必须能根据 project state 同步多个 Scene。
- 主 Scene 可以通过 `getMainScene()` 获取。
- `ProjectProxy` 释放时必须释放其拥有的所有 `SceneProxy` 订阅。
- 项目文件保存依赖具体产品/文件模块，不作为 `ProjectProxy` 前端公共层的固定命令。
