# ApplicationContext 方案文档

## 1. 工程位置

```text
src/icax/framework/ApplicationContext/
```

`ApplicationContext` 放在 framework，是因为它描述应用运行环境，会被 ApplicationHost、Behaviour、Service、CommandHandler 使用；但它本身不应依赖这些上层项目。

## 2. 和 ApplicationHost 的关系

`ApplicationHost` 是后台应用宿主，负责：

- 读取配置文件。
- 构造 `ApplicationContext`。
- 维护产品定义并启动 `ProductRuntime`。
- 驱动应用级和产品级 mailbox 轮询。

`ApplicationContext` 是 ApplicationHost 构造出来的上下文对象，负责被查询，不负责启动流程。

```text
ApplicationHost
  -> Load settings
  -> Build ApplicationContext
  -> Start ProductRuntime
  -> ProductRuntime opens ProjectCatalog
  -> Project creates Repository / UniverseContext / Universe
  -> Pass application context to Service / CommandHandler
```

## 3. 配置读写

配置读写拆成三层：

```text
CApplicationSettings
  配置数据快照

IApplicationConfigStore
  配置读写接口

CFileApplicationConfigStore
  默认文件读写实现

CApplicationSettingsService
  修改配置、保存配置、重新加载配置
```

这样 `ApplicationContext` 不会变成既被到处依赖、又自己读写文件的重对象。

## 4. 依赖原则

允许：

```text
Behaviour -> ApplicationContext
Service -> ApplicationContext
CommandHandler -> ApplicationContext
ApplicationHost -> ApplicationContext
```

禁止：

```text
ApplicationContext -> Behaviour
ApplicationContext -> Service
ApplicationContext -> Database
ApplicationContext -> ApplicationHost
```

## 5. 当前不做

当前不做：

- 插件加载。
- 主循环。
- 项目打开保存。
- 项目资源管理。
- 用户账户系统。

这些能力由 ApplicationHost、ProductRuntime、CommandHandler、ResourceService 或业务服务承载。
