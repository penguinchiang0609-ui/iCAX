# ApplicationRuntime

`ApplicationRuntime` 是前端启动后最先连接的后台入口。它负责构造并持有 `ApplicationContext`、维护产品定义与产品运行时、驱动应用级后台线程、发布生命周期事件，并提供应用级 Facade。应用级服务容器由 `ApplicationContext` 自身管理。

ApplicationRuntime 不直接打开 `ProjectCatalog`，也不直接管理项目。前端启动后先向应用级邮箱发送 `App.GetState` 或 `App.ListProducts` 获取产品清单，再发送 `App.StartProduct` 启动某个产品。产品启动后，前端使用返回的 `productChannelId` 连接产品级邮箱，由 `ProductRuntime` 负责打开 ProjectCatalog 并返回主 Scene 邮箱入口。

ApplicationRuntime 维护产品定义列表，产品定义只保存静态能力，例如前端入口、项目文件 magic 和模块路径。最近项目和产品用户设置属于产品运行数据，由对应的 ProductRuntime 读取和保存。双击项目文件启动时，ApplicationRuntime 只读取少量文件头，通过唯一 magic 识别产品，然后把打开动作转交给 ProductRuntime。

ApplicationRuntime 直接持有应用级 `CFacadeChannelRegistry`，并创建自己的应用级 Facade channel。前端启动后由运行时向前端 bridge 发放 `applicationChannelId` 对应的 frontend Facade endpoint；ApplicationRuntime 自己在后台线程中读取同一 channel 的 backend Facade endpoint。

ApplicationRuntime 工作循环同时消费 `GetApplicationTaskScheduler()` 的任务队列，并 Tick 运行时的 `CCoroutineRuntime`。运行时代码先通过该 scheduler 回到 ApplicationRuntime 工作线程，再调用 `StartCoroutine()`；协程的 `NextFrame`、`WaitForSeconds` 和 Task 唤醒都在该线程恢复。`Stop()`、异常退出或一次生命周期结束时会取消 Runtime 内所有未完成协程，不会为协程新增线程。

ApplicationRuntime 持有执行机制和生命周期对象：工作线程、Task/Coroutine 调度、应用级 Facade channel 与 `CFacadeChannelRegistry`。应用级配置和 `CServiceProvider` 由 `ApplicationContext` 管理；ApplicationRuntime 拥有该 Context，并且只有运行在其工作线程中的应用级 Facade 可以取得写能力。产品模块的 Service、Behaviour、Facade、Meta 和 ResourceLoader 注册回放到各自 ProductContext 环境，不修改 ApplicationContext。

## 目录结构

- `ApplicationRuntime.vcxproj`：应用运行时工程。
- `ApplicationRuntime.h` / `ApplicationRuntime.cpp`：应用运行时入口，负责应用上下文、产品运行时、应用级 Facade 调用、事件订阅和故障诊断。
- `ApplicationRuntimeFacades.h` / `ApplicationRuntimeFacades.cpp`：应用级内置 Facade 方法常量和 Variant payload 编解码。
- `ApplicationRuntimeConfig.h`：应用运行时配置、产品定义列表和可选启动产品。
- `ProductFileResolver.h` / `ProductFileResolver.cpp`：根据项目文件 magic 和产品定义识别文件归属产品。
