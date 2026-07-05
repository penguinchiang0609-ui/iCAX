# ProductProxy

`ProductProxy` 是前端产品代理模块，表达某个已启动 Engine product runtime 的前端会话。

## 目录结构

- `ProductProxy.mjs`：前端 product 代理对象。它持有 product channel、产品状态快照和本产品下的 `ProjectProxy` 对象表。
- `productModuleLoader.mjs`：产品 webpage 入口解析、动态 import、缓存和挂载分发。

## 边界

本模块不等同于具体产品页面。具体产品页面仍放在 `src/apps/<product-id>/webpage/`，由 `productModuleLoader.mjs` 加载；本模块负责产品级 mailbox、产品级事件、项目打开入口和产品页面挂载入口。

Project 本身不拥有 mailbox。`ProductProxy` 创建或更新 `ProjectProxy` 时，会根据 backend 返回的主 Scene 信息调用 `bridge.registerSceneChannel(projectId, mainSceneId)`，让原生宿主把主 Scene 的 post office 注册到当前 bridge 会话。
