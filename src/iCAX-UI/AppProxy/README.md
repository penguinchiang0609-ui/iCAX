# AppProxy

`AppProxy` 是前端应用代理模块，负责连接宿主 bridge，获得 application channel，并把应用级 Facade 调用封装成前端对象。

## 目录结构

- `AppProxy.mjs`：前端 application 代理对象。它持有 `MailboxClient`、application channel 和已启动产品的 `ProductProxy` 对象表。

## 边界

本模块不拥有 backend 数据，不渲染具体产品页面，不直接解释 PDO。它只维护前端会话状态、应用级 mailbox 入口和 `ProductProxy` 生命周期。

backend state 里的 `productChannelId` 是 product runtime 身份，不等于当前 bridge 已接入该 channel。`AppProxy` 创建或更新已启动产品的 `ProductProxy` 前，必须调用 `bridge.registerProductChannel(productId)` 完成原生 post office 注册。
