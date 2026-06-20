# CommandHandler 规格文档

## 1. 定位

`CommandHandler` 是后端命令处理抽象。

它不是 Mailbox，也不是业务服务本体。它负责把已经收到的命令请求分发给对应 handler，再把执行结果整理成命令响应。

典型链路：

```text
Frontend Bridge
  -> Mailbox
  -> ApplicationHost mail adapter
  -> CCommandRequest
  -> CCommandDispatcher
  -> ICommandHandler
  -> ApplicationContext / Project / Repository / Service API
```

## 2. 核心概念

### 2.1 CommandRequest

```cpp
struct CCommandRequest
{
    uint64_t nCommandID;
    uint64_t nOriginID;
    uint64_t nTypeCode;
    std::vector<uint8_t> Payload;
};
```

`nTypeCode` 对应命令类型。Mailbox 的 `MailHeader::nTypeCode` 可以映射到这里。

### 2.2 CommandResponse

```cpp
struct CCommandResponse
{
    uint64_t nCommandID;
    uint64_t nOriginID;
    uint64_t nTypeCode;
    ECommandStatusCode nStatus;
    std::string strError;
    std::vector<uint8_t> Payload;
};
```

状态码：

```text
Ok
NoHandler
InvalidRequest
ExecutionError
```

### 2.3 CommandContext

`CommandContext` 是一次命令执行所需依赖的容器。它可以携带 `ApplicationContext`、`Project`、Repository、`ResourceLibrary`、服务等对象。

CommandHandler 项目本身不直接依赖这些对象的具体项目。

依赖访问分两类：

- `GetDependency<T>()`：依赖可选，缺失时返回 `nullptr`。
- `RequireDependency<T>()`：依赖必需，缺失时抛出异常，由 `CommandDispatcher` 转换为 `ExecutionError`。

### 2.4 CommandRegistry

`CommandRegistry` 维护：

```text
typeCode -> ICommandHandler
```

### 2.5 CommandDispatcher

`CommandDispatcher` 查找 handler，执行并把异常转换为状态码：

- 找不到 handler：`NoHandler`
- `std::invalid_argument`：`InvalidRequest`
- 其他异常：`ExecutionError`

## 3. 使用样例

```cpp
auto registry = std::make_shared<CCommandRegistry>();
CCommandDispatcher dispatcher(registry);

registry->Register(kUndoCommand, std::make_shared<CFunctionCommandHandler>(
    [](const CCommandRequest& request, ICommandContext& context) {
        auto repository = context.RequireDependency<IRepository>();
        bool ok = repository->Undo();

        CCommandResponse response;
        response.nStatus = ok ? ECommandStatusCode::Ok : ECommandStatusCode::ExecutionError;
        return response;
    }));

auto response = dispatcher.Dispatch(request, context);
```

项目级命令由 `ApplicationHost` 从项目邮局收到邮件后分发，`CommandContext` 会带上当前项目：

```cpp
registry->Register(kImportFbxCommand, std::make_shared<CFunctionCommandHandler>(
    [](const CCommandRequest& request, ICommandContext& context) {
        auto project = context.RequireDependency<CProject>();
        auto repository = context.RequireDependency<IRepository>();
        auto resources = context.RequireDependency<CResourceLibrary>();

        repository->BeginUndoCommand("Import FBX");
        auto model = resources->Load<ModelResource>("D:/assets/robot.fbx");
        // 根据 model 创建 EC 数据
        repository->EndUndoCommand();

        CCommandResponse response;
        response.nStatus = ECommandStatusCode::Ok;
        return response;
    }));
```

应用级命令由应用邮局分发，通常只依赖 `ApplicationContext`、应用设置服务、项目目录等对象，不应假定一定存在当前项目。

## 4. 边界

- CommandHandler 不直接拥有撤销还原状态。
- CommandHandler 不直接拥有资源库。
- CommandHandler 不直接依赖 Mailbox。
- 具体业务命令可以放在 framework 通用命令模块或业务 command 模块。
