# Mail 通信规格文档

## 1. 定位

Mail 通信用于同一进程内两个运行单元之间传递低频消息，包括 Facade 调用、结果、事件和低频状态同步。

Mailbox 只提供通用双端点模型：

```text
EndA <-> EndB
```

它不绑定前端、后端、产品或项目。具体运行体可以把 EndA/EndB 映射成自己的业务角色。

Mail 与 PDO 的分工：

- Mail：低频、可靠、有序的控制消息。
- PDO：高频、可丢帧的数据同步。

## 2. 数据结构

### 2.1 MailHeader

```cpp
struct MailHeader
{
    uint64_t nMailId = 0;
    uint64_t nOriginId = 0;
    uint64_t nTypeCode = 0;
    StampCode nStamp = kMailOk;
};
```

- `nMailId`：本封邮件 ID，由发送侧分配。
- `nOriginId`：原始邮件 ID，回复邮件用它关联请求；`0` 表示非回复邮件。
- `nTypeCode`：上层业务类型码。当前 Facades 使用它承载 `FacadeName.MethodName` 的 64 位紧凑编码。
- `nStamp`：通用处理状态。业务错误细节由业务 payload 表达。

### 2.2 StampCode

```cpp
enum StampCode : uint16_t
{
    kMailOk = 0,
    kMailNotFound = 1,
    kMailInvalidPayload = 2,
    kMailExecutionError = 3,
    kMailTimeout = 4,
};
```

### 2.3 MailData

```cpp
struct MailData
{
    size_t nSize = 0;
    uint8_t* pData = nullptr;
};
```

`MailData` 还包含队列池租约字段，业务代码不直接访问这些字段。

调用方必须遵守：

- 空 payload 使用 `nSize == 0` 且 `pData == nullptr`。
- 非空 payload 可以来自普通 `new[]`，也可以来自 `CMailQueue` 的预分配池。
- 业务代码一律调用 `ReleaseMailPayload(mail)` 释放。
- 不允许对 `Payload.pData` 手写 `delete[]`。

### 2.4 Mail

```cpp
struct Mail
{
    MailHeader Header;
    MailData Payload;
};
```

Mailbox 不解释 payload。面向 H5 的普通 Facade 调用建议 payload 使用 UTF-8 Variant 文本。

## 3. CMailQueue

`CMailQueue` 是单向邮件队列。

```cpp
struct CMailQueueCreateInfo
{
    size_t nMaxMailCount = 4096;
    size_t nPayloadPoolBytes = 4ull * 1024ull * 1024ull;
    size_t nMaxPayloadBytesPerMail = 256ull * 1024ull;
};

class CMailQueue
{
public:
    CMailQueue();
    explicit CMailQueue(const CMailQueueCreateInfo& createInfo);

    void Enqueue(const Mail& mail);
    bool TryEnqueue(const Mail& mail);

    void EnqueuePayload(const MailHeader& header, const void* payload, size_t payloadSize);
    bool TryEnqueuePayload(const MailHeader& header, const void* payload, size_t payloadSize);

    std::vector<Mail> Drain();
    void Clear();

    size_t GetPendingCount() const;
    size_t GetFreeRecordCount() const;
    size_t GetFreePayloadBytes() const;
};
```

行为规则：

- 队列创建时一次性分配 record 池和 payload 池。
- `Enqueue` / `EnqueuePayload` 复制 `MailHeader`，并把 payload 拷贝到队列预分配池。
- 发送方仍拥有传入邮件的 payload，发送后仍需释放自己的临时邮件。
- `TryEnqueue` / `TryEnqueuePayload` 在 record 或 payload 空间不足时返回 `false`。
- `Enqueue` / `EnqueuePayload` 在空间不足时抛出 `std::runtime_error`。
- payload 大小超过 `nMaxPayloadBytesPerMail` 时抛出 `std::runtime_error`。
- `Drain()` 取出当前 pending 邮件并清空 pending 队列。
- `Drain()` 返回的邮件处理完成后必须调用 `ReleaseMailPayload`。
- `Clear()` 只释放仍滞留在队列里的 pending 邮件，不影响已经 `Drain()` 出去的邮件。
- `CMailQueue` 方法级线程安全。

## 4. CMailPostOffice

`CMailPostOffice` 是某一端看到的轻量邮局视图。它不拥有队列，只保存收件队列和发件队列的弱引用。

```cpp
class CMailPostOffice
{
public:
    bool IsValid() const noexcept;

    void Send(const Mail& mail) const;
    void SendPayload(const MailHeader& header, const void* payload, size_t payloadSize) const;
    void SendText(const MailHeader& header, const std::string& payloadText) const;

    std::vector<Mail> Receive() const;
    void ClearIncoming() const;
    void ClearOutgoing() const;
};
```

行为规则：

- `Send(mail)` 将邮件写入对端收件队列。
- `SendPayload` 直接发送 bytes，避免调用方先构造临时 `Mail` payload。
- `SendText` 直接发送 UTF-8 文本 payload，H5 Facade 调用通常使用该接口发送 Variant 文本。
- `Receive()` 从本端收件队列取出当前全部邮件。
- `ClearIncoming()` 清空本端收件队列。
- `ClearOutgoing()` 清空本端发件队列，也就是对端收件队列。
- 默认构造的 `CMailPostOffice` 未绑定队列，`IsValid()` 返回 `false`。
- 对无效邮局调用收发或清空接口会抛出 `std::logic_error`。

## 5. CMailChannel

`CMailChannel` 是双向通道，内部拥有两个单向队列：

```text
EndA -> EndB
EndB -> EndA
```

```cpp
enum MailChannelEnd : uint8_t
{
    kMailEndA = 0,
    kMailEndB = 1,
};

struct CMailChannelCreateInfo
{
    CMailQueueCreateInfo EndAToEndBQueue;
    CMailQueueCreateInfo EndBToEndAQueue;
};

class CMailChannel
{
public:
    CMailChannel();
    explicit CMailChannel(const CMailChannelCreateInfo& createInfo);

    CMailPostOffice GetPostOffice(MailChannelEnd end) noexcept;
    CMailPostOffice GetEndAPostOffice() noexcept;
    CMailPostOffice GetEndBPostOffice() noexcept;

    void Clear();
    void Reset();
};
```

EndA 邮局：

```text
Receive() 读取 EndB -> EndA
Send()    写入 EndA -> EndB
```

EndB 邮局：

```text
Receive() 读取 EndA -> EndB
Send()    写入 EndB -> EndA
```

生命周期规则：

- `Clear()` 清空两个方向上的 pending 邮件，不改变队列身份，旧邮局仍然有效。
- `Reset()` 丢弃当前两个方向的队列并按原创建参数重建队列，旧邮局失效。
- `CMailChannel` 禁止拷贝和移动。
- 可以低成本反复获取 `CMailPostOffice`，不会创建新队列。

## 6. CMailChannelRegistry

`CMailChannelRegistry` 是线程安全的通道目录，由 ApplicationHost 等生命周期所有者持有。

```cpp
class CMailChannelRegistry
{
public:
    bool CreateChannel(const uuid& channelID);
    bool CreateChannel(const uuid& channelID, const CMailChannelCreateInfo& createInfo);
    bool HasChannel(const uuid& channelID) const;

    CMailPostOffice GetFrontendPostOffice(const uuid& channelID) const;
    CMailPostOffice GetBackendPostOffice(const uuid& channelID) const;

    bool RemoveChannel(const uuid& channelID);
    void ClearChannels();
};
```

命名中的 frontend/backend 只是 framework 当前装配约定：

- frontend 使用 EndA 邮局。
- backend 使用 EndB 邮局。

Mailbox 底层仍只认识 EndA/EndB。

## 7. 使用样例

### 7.1 发送 UTF-8 Facade 调用

```cpp
CMailChannel channel;
auto frontend = channel.GetEndAPostOffice();
auto backend = channel.GetEndBPostOffice();

MailHeader header;
header.nMailId = 1;
header.nTypeCode = MakeFacadeMethodCode("Project", "Open");

frontend.SendText(header, R"({"path":"D:/demo.robot"})");

auto mails = backend.Receive();
for (auto& mail : mails)
{
    auto text = GetMailPayloadText(mail);
    ReleaseMailPayload(mail);
}
```

### 7.2 使用临时 Mail 发送

```cpp
MailHeader header;
header.nMailId = 2;
header.nTypeCode = eventCode;

auto mail = CreateTextMail(header, R"({"event":"ready"})");
postOffice.Send(mail);

// Send 会深拷贝 payload，发送方仍需释放自己的临时邮件。
ReleaseMailPayload(mail);
```

### 7.3 接收并回复

```cpp
auto requests = backend.Receive();
for (auto& request : requests)
{
    MailHeader replyHeader;
    replyHeader.nMailId = AllocateMailID();
    replyHeader.nOriginId = request.Header.nMailId;
    replyHeader.nTypeCode = request.Header.nTypeCode;
    replyHeader.nStamp = kMailOk;

    backend.SendText(replyHeader, R"({"ok":true})");
    ReleaseMailPayload(request);
}
```

### 7.4 指定通道容量

```cpp
CMailChannelCreateInfo createInfo;
createInfo.EndAToEndBQueue.nMaxMailCount = 1024;
createInfo.EndAToEndBQueue.nPayloadPoolBytes = 2ull * 1024ull * 1024ull;
createInfo.EndBToEndAQueue.nMaxMailCount = 4096;
createInfo.EndBToEndAQueue.nPayloadPoolBytes = 8ull * 1024ull * 1024ull;

registry.CreateChannel(channelID, createInfo);
```

## 8. 线程模型

`CMailQueue` 是方法级线程安全的：

- 多线程可以同时发送。
- 一个线程可以接收，同时其他线程发送。
- 一个线程可以清空队列，同时其他线程发送或接收。

生命周期操作由所有者保证：

- 不应在其他线程仍使用同一通道时销毁 `CMailChannel`。
- `Reset()` / `RemoveChannel()` 用于关闭运行体时切断旧邮局。
- 已经 `Receive()` 出来的邮件会通过租约保持必要的队列存活，直到调用方 `ReleaseMailPayload`。

## 9. 测试要求

单元测试目录：

```text
src/tests/icax-engine/framework/Mailbox/MailboxTest/
```

测试覆盖：

- 队列入队、取出、清空和顺序。
- 文本 payload 创建、读取、替换。
- 队列池租约释放后归还 record 和 payload 空间。
- record 或 payload 池满时的失败行为。
- `SendText` 直接发送文本。
- EndA/EndB 双向路由与方向隔离。
- Channel reset 后旧邮局失效。
- Registry 显式创建、移除、清空和按方向配置容量。
- 并发发送和接收。
