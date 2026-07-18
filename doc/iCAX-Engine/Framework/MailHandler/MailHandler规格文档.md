# MailHandler 规格文档

## 1. 核心接口

```cpp
class CMailFacadeHandler final
{
public:
    using MailIDAllocator = std::function<uint64_t()>;

    CFacadeCall ToFacadeCall(const Mail& mail) const;
    StampCode ToMailStamp(EFacadeCallStatus status) const noexcept;

    size_t HandleAvailableMails(
        const CMailPostOffice& postOffice,
        const CFacadeInvoker& invoker,
        IApplicationContext& applicationContext,
        IProductContext* productContext,
        IProjectContext* projectContext,
        ISceneContext* sceneContext,
        const MailIDAllocator& allocateResultMailID) const;
};
```

## 2. 转换规则

- `MailHeader::nMailId` -> `CFacadeCall::nCallID`
- `MailHeader::nOriginId` -> `CFacadeCall::nOriginID`
- `MailHeader::nTypeCode` -> `CFacadeCall::Method`
- Mail payload -> `CFacadeCall::Payload`
- 结果 Mail 的 `nOriginId` 等于请求 Mail 的 `nMailId`
- 结果 Mail 的 `nTypeCode` 等于 `CFacadeResult::Method.GetCode()`
- 结果 payload 为空且存在错误文本时，以 UTF-8 bytes 返回错误

邮件声明非空 payload 但 `pData == nullptr` 时，本次调用返回 `InvalidCall`。每封已接收邮件的 payload 都必须释放。

## 3. 边界

MailHandler 不解释参数，不查找业务对象，不拥有 registry、context 或 channel。它只适配 Mail 与 Facade 调用模型。
