# SceneProxy

`SceneProxy` 是前端 Scene 代理模块。Project 只表达项目容器，Scene 才表达可运行现场。

目录结构：

- `SceneProxy.mjs`：封装 scene channel、Scene 状态、撤销重做、事件订阅、PDO 和 Resource URL 访问。

使用边界：

- 产品页面通过 `sceneProxy.invoke("Machine.Jog", { machineId, axis, delta })` 调用 SDO 方法。
- 产品页面通过 `sceneProxy.pdo` 读取当前 Scene 的 PDO。
- 产品页面通过 `sceneProxy.resources` 或 `sceneProxy.fetchResource(url)` 直接访问资源，不经过 SDO 邮件。
- `SceneProxy` 不解释业务参数、SQL 结果、FlatBuffer Schema 或 Component。
