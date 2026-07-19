# ApplicationContext 方案文档

## 1. 工程位置

```text
src/icax-engine/framework/ApplicationContext/
```

`ApplicationContext` 放在 framework，是因为它描述应用运行环境，会被 ApplicationRuntime、Behaviour、Service、Facades 使用；但它本身不应依赖这些上层项目。

## 2. 和 ApplicationRuntime 的关系

`ApplicationRuntime` 是后台应用宿主，负责：

- 读取配置文件。
- 构造 `ApplicationContext`。
- 维护产品定义并启动 `ProductRuntime`。
- 驱动应用级和产品级 Facade 轮询。

`ApplicationContext` 是 ApplicationRuntime 构造出来的应用作用域环境，管理描述、路径、配置状态、配置存储和应用级 ServiceProvider。ApplicationRuntime 管理线程、调度、Facade channel 以及 Context 自身的生命周期。

```text
ApplicationRuntime
  -> Load settings
  -> Build ApplicationContext
  -> Start ProductRuntime
  -> ProductRuntime opens ProjectCatalog
  -> Project creates Repository / Universe
  -> Pass application context to Service / Facades
```

## 3. 配置读写

配置读写拆成三层：

```text
PropertyBag
  配置数据快照，由 ApplicationContext 持有

IApplicationConfigStore
  配置读写接口

CFileApplicationConfigStore
  默认文件读写实现

CApplicationConfigService
  由 ApplicationRuntime 的应用级 Facade 使用，修改配置、保存配置、重新加载配置
```

配置存储是 Context 管理的环境能力，具体序列化仍委托给 `IApplicationConfigStore`。ApplicationRuntime 保留活动 Context 的唯一可写引用；ProductRuntime、Project、Scene、Behaviour、Service 和普通 Facade 只能获得 `const IApplicationContext`。应用级服务的注册、首次初始化和卸载也必须由 ApplicationRuntime 工作线程完成；只读 Context 只能取得已经初始化的 `const` 服务实例。

## 4. 依赖原则

允许：

```text
Behaviour -> ApplicationContext
Service -> ApplicationContext
Facades -> ApplicationContext
ApplicationRuntime -> ApplicationContext
ApplicationContext -> Services
```

禁止：

```text
ApplicationContext -> Behaviour
ApplicationContext -> Database
ApplicationContext -> ApplicationRuntime
```

## 5. 当前不做

当前不做：

- 插件加载。
- 主循环。
- 项目打开保存。
- 项目资源管理。
- 用户账户系统。

这些能力由 ApplicationRuntime、ProductRuntime、Facades、ResourceService 或业务服务承载。
