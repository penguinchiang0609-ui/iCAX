# CommandHandler 规格文档

## 1. 定位

`CommandHandler` 是后端命令处理抽象。

它不是 Mailbox，也不是业务服务本体。它负责把已经收到的命令请求分发给对应 handler，再把执行结果整理成命令响应。

典型链路：

```text
H5
  -> Mailbox
  -> Mail adapter
  -> CCommandRequest
  -> CCommandDispatcher
  -> ICommandHandler
  -> Database / Service / Behaviour API
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

`CommandContext` 是一次命令执行所需依赖的容器。它可以携带 `ApplicationContext`、Repository、ResourceService、产品服务等对象。

CommandHandler 项目本身不直接依赖这些对象的具体项目。

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
        auto repository = context.GetDependency<IRepository>();
        bool ok = repository->Undo(domainId);

        CCommandResponse response;
        response.nStatus = ok ? ECommandStatusCode::Ok : ECommandStatusCode::ExecutionError;
        return response;
    }));

auto response = dispatcher.Dispatch(request, context);
```

## 4. 边界

- CommandHandler 不直接拥有撤销还原状态。
- CommandHandler 不直接拥有资源池。
- CommandHandler 不直接依赖 Mailbox。
- 具体业务命令可以放在 framework 通用命令模块或产品 backend command 模块。
