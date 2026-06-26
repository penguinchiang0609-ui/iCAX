# ApplicationHost

`ApplicationHost` 是前端启动后最先连接的后台入口。它负责构造 `ApplicationContext`、解析框架服务、维护产品定义与产品运行时、驱动应用级后台线程、发布生命周期事件，并提供应用级 mailbox。

ApplicationHost 不直接打开 `ProjectCatalog`，也不直接管理项目。前端启动后先向应用级邮箱发送 `App.GetState` 或 `App.ListProducts` 获取产品清单，再发送 `App.StartProduct` 启动某个产品。产品启动后，前端使用返回的 `productChannelId` 连接产品级邮箱，由 `ProductRuntime` 负责打开 ProjectCatalog 并返回项目邮箱入口。

ApplicationHost 维护产品定义列表，产品定义只保存静态能力，例如前端入口、项目文件 magic 和模块路径。最近项目和产品用户设置属于产品运行数据，由对应的 ProductRuntime 读取和保存。双击项目文件启动时，ApplicationHost 只读取少量文件头，通过唯一 magic 识别产品，然后把打开动作转交给 ProductRuntime。

ApplicationHost 通过应用级 `CMailChannelService` 创建自己的应用级 mail channel。前端启动后由宿主向前端 bridge 发放 `applicationChannelId` 对应的 frontend post office；ApplicationHost 自己在后台线程中读取同一 channel 的 backend post office。

ApplicationHost 只持有应用级共享对象：`CServiceProvider` 和 `IMailChannelService`。BehaviourRegistry、CommandRegistry、产品级 MetaRegistry 和产品级 ResourceLoaderRegistry 由 `ProductRuntime` 创建并按产品模块路径回放注册；Project 只拿到明确注入的产品/项目依赖，不共享项目数据。

## 目录结构

- `ApplicationHost.vcxproj`：应用宿主工程。
- `ApplicationHost.h` / `ApplicationHost.cpp`：应用宿主入口，负责应用上下文、产品运行时、应用级命令分发、事件订阅和故障诊断。
- `ApplicationHostCommands.h` / `ApplicationHostCommands.cpp`：应用级内置命令常量和 Variant payload 编解码。
- `ApplicationHostConfig.h`：应用宿主配置、产品定义列表和可选启动产品。
- `ProductFileResolver.h` / `ProductFileResolver.cpp`：根据项目文件 magic 和产品定义识别文件归属产品。
