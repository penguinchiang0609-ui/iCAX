# MailHandler 规格文档

## 1. 定位

MailHandler 是 Mailbox 与 CommandHandler 的桥接层。

职责边界：

- Mailbox：只负责 `Mail` 收发。
- CommandHandler：只负责命令路由和分发。
- MailHandler：把 `Mail` 转成 `CCommandRequest`，把 `CCommandResponse` 转成回复 `Mail`。

MailHandler 不依赖 ApplicationHost、ProductRuntime 或 Project。运行体负责传入自己的 PostOffice、CommandDispatcher 和上下文。

## 2. 核心类

```cpp
class CMailCommandHandler final
{
public:
    using MailIDAllocator = std::function<uint64_t()>;

    CCommandRequest ToCommandRequest(const Mail& mail) const;
    StampCode ToMailStamp(ECommandStatusCode status) const noexcept;

    size_t DispatchAvailableMails(
        const CMailPostOffice& postOffice,
        const CCommandDispatcher& dispatcher,
        IApplicationContext& applicationContext,
        IProductContext* productContext,
        IProjectContext* projectContext,
        ISceneContext* sceneContext,
        const MailIDAllocator& allocateResponseMailID) const;
};
```

## 3. 行为规则

- `ToCommandRequest` 从 `MailHeader::nMailId` 设置 `CCommandRequest::nCommandID`。
- `ToCommandRequest` 从 `MailHeader::nOriginId` 设置 `CCommandRequest::nOriginID`。
- `ToCommandRequest` 从 `MailHeader::nTypeCode` 构造命令路由。
- `ToCommandRequest` 会复制 payload 到 `CCommandRequest::Payload`。
- 邮件声明非空 payload 但 `pData == nullptr` 时抛出 `std::invalid_argument`。
- `DispatchAvailableMails` 从 `postOffice.Receive()` 取出当前全部邮件。
- 每封邮件分发后都会通过同一个 `postOffice` 发送响应。
- 响应邮件 `nOriginId` 等于请求邮件 `nMailId`。
- 响应邮件 `nTypeCode` 等于响应命令路由码。
- 命令错误文本在响应 payload 为空时作为 UTF-8 bytes 写入邮件 payload。
- 命令 handler 抛出的异常不吞掉，原样向外传播。
- 已接收邮件的 payload 由 MailHandler 在处理结束或异常展开时释放。

## 4. 使用样例

```cpp
CMailCommandHandler mailHandler;

mailHandler.DispatchAvailableMails(
    backendPostOffice,
    commandDispatcher,
    applicationContext,
    productContext,
    projectContext,
    sceneContext,
    [&]() { return AllocateBackendMailID(); });
```

应用级命令：

```cpp
mailHandler.DispatchAvailableMails(
    applicationPostOffice,
    commandDispatcher,
    applicationContext,
    nullptr,
    nullptr,
    nullptr,
    [&]() { return AllocateBackendMailID(); });
```

产品级命令：

```cpp
mailHandler.DispatchAvailableMails(
    productPostOffice,
    commandDispatcher,
    applicationContext,
    productContext,
    nullptr,
    nullptr,
    [&]() { return AllocateBackendMailID(); });
```

项目级命令：

```cpp
mailHandler.DispatchAvailableMails(
    projectPostOffice,
    commandDispatcher,
    applicationContext,
    productContext,
    projectContext,
    sceneContext,
    [&]() { return AllocateBackendMailID(); });
```

## 5. 测试

测试工程：

```text
src/tests/icax-engine/framework/MailHandler/MailHandlerTest/
```

覆盖：

- Mail 到 CommandRequest 的转换。
- 成功命令响应。
- 无 handler 的响应状态和错误 payload。
