# ApplicationHost 方案文档

## 1. 工程位置

```text
src/icax/framework/ApplicationHost/
```

`ApplicationHost` 放在 framework 下，是因为当前 `src/icax` 本身就是 C++ 后台主体，不再额外设置 `runtime` 或 `backend` 目录。它是后台应用宿主，不是基础库。

## 2. 职责划分

`ApplicationHost` 承担装配职责：

- 读取应用设置并构造 `ApplicationContext`。
- 创建 `Database::IRepository`。
- 创建 `Behaviour::IUniverse`。
- 获取服务体系中的 `IMailPostOfficeService`。
- 加载插件 DLL。
- 绑定启动组件对应的入口 Behaviour。
- 驱动每帧后台循环。

`ApplicationHost` 不承担以下职责：

- 不直接表达业务命令。
- 不保存项目数据。
- 不实现资源池。
- 不实现撤销还原、事务和快速保存日志。
- 不把 Mail 的 `nTypeCode` 当作过程调用入口。

## 3. 装配流程

```text
CApplicationHost::Load
  -> LoadApplicationSettings
  -> Create ApplicationContext
  -> Database::GenerateRepository
  -> Behaviour::GenerateUniverse
  -> Resolve IMailPostOfficeService
  -> LoadPluginMetas
  -> LoadLibraryA component / behaviour / service
  -> LoadStartup
  -> Bind first Behaviour
  -> Add first Component to meta entity
```

## 4. 主循环

```text
CApplicationHost::MainLoop
  -> Universe.PreSwapPDO
  -> Receive backend mails
  -> Universe.Tick
  -> Universe.PostSwapPDO
```

邮件命令后续应由 `CommandHandler` 适配层接入。`ApplicationHost` 只负责把后台循环推起来，不承载具体业务命令。

## 5. 后续演进

- 邮件请求解析与分发接入 `CommandHandler`。
- 插件加载策略从当前 Variant 文件读取逐步沉淀为可替换的插件描述加载机制。
