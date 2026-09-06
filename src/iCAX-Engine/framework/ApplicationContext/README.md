# ApplicationContext

`ApplicationContext` 是 framework 层的应用作用域环境项目。它管理应用程序级描述、路径、用户配置、配置存储和应用服务环境；不承载项目数据、EC 数据或 Scene 资源。

`ApplicationRuntime` 创建并销毁 `ApplicationContext`，负责线程、调度和生命周期。Behaviour、Service、Product、Project、Scene 和普通 SDO 只获得 `const IApplicationContext` 视图；修改配置或应用服务环境必须回到 ApplicationRuntime 工作线程中的应用级 SDO。

应用级设置是与具体产品和具体项目都无关的 iCAX 应用程序参数。它跟随当前 iCAX 安装或当前用户环境，不跟随产品，也不跟随项目文件。

跨项目复用的用户业务数据保存到 `%LOCALAPPDATA%/iCAX/Profiles/local-default/user.db`，而不是塞进应用设置或浏览器缓存。底层 `IUserDataStore` 由 ApplicationContext 私有持有；ApplicationRuntime 根据产品清单中的 `ProductID` 和 `userData.features` 声明创建 `IProductUserDataStore` 并注入 ProductRuntime。产品只能填写 FeatureID、RecordType、Subject 和 RecordID，不能自行选择或伪造其他产品的作用域，也不能写入 manifest 未声明的数据类型。

数据库模式 v2 使用 `product_id + feature_id + record_type + record_id` 作为记录主键，Subject 字段表达主要作用对象，`user_record_links` 表达附加关系；Payload 只承载业务内容。记录包含 schema version、乐观并发 revision、时间和软删除状态。对仍被活动记录引用的用户记录默认采用安全的 restrict 删除策略。旧模式记录升级时保留在 `_legacy` 功能域，不按具体产品进行硬编码解释。

这个仓库固定表示 `user` 存储范围。设备布局进入设备本地设置，项目状态进入项目文件，临时交互状态只留在会话内存；它们不会因为共享一套字段而混入 `user.db`。默认实现使用系统 WinSQLite，不依赖外部 `sqlite.exe`。浏览器数据单独放在 `%LOCALAPPDATA%/iCAX/Browser`，与业务数据隔离。

典型应用级设置：

- UI 容器类型，例如 ECF、WebView、CEF、Qt 或 WPF。
- 应用安装路径、配置路径、缓存路径、临时路径和日志路径。
- 全局语言、主题和日志级别。
- 全局插件搜索目录。
- 应用级网络、更新、诊断和崩溃转储配置。

不应放入应用级设置：

- 某个产品自己的业务默认参数。
- 最近打开项目这类产品相关数据。
- 跟项目文件一起保存和打开的项目参数。
- 工件、刀路、图层、运动规划等项目业务对象。

## 目录结构

- `ApplicationDescriptor.*`：应用描述、支持的项目 magic 和版本。
- `ApplicationPaths.h`：应用安装、配置、缓存、临时、资源版本临时根目录和日志目录。`ResourceVersionDirectory` 为空时，Scene 使用 `TempDirectory/ResourceVersions`。
- `ApplicationContext.*` / `IApplicationContext.h`：上下文只读接口和默认实现，内部持有应用级 `PropertyBag`、配置存储和 `CServiceProvider`。
- `UserDataStore.*`：底层通用仓库、显式关系，以及框架绑定 `ProductID` 并应用 manifest 描述后交给产品的 user-scope 仓库。
- `IApplicationConfigStore.h` / `FileApplicationConfigStore.*`：配置读写抽象与文件实现。
- `ApplicationConfigService.*`：ApplicationRuntime 应用级 SDO 使用的配置写入器；实际配置状态和存储仍由 Context 管理。
- `ApplicationContextExport.h`：DLL 导出宏。
