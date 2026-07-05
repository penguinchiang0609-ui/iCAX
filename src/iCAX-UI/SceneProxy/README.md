# SceneProxy

`SceneProxy` 是前端 Scene 代理模块。Project 只表达项目容器，Scene 才表达可运行现场。

目录结构：

- `SceneProxy.mjs`：封装 scene channel、Scene 状态、撤销重做、事件订阅和 PDO 访问。

使用边界：

- 产品页面通过 `sceneProxy.execute()` 向后端发送业务命令。
- 产品页面通过 `sceneProxy.pdo` 读取当前 Scene 的 PDO。
- `SceneProxy` 不解释业务 payload，不直接访问 Database、Resource 或 Component。
