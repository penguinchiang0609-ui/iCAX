# Mailbox 规格文档

## 1. 定位

`Mailbox` 是进程内普通消息队列。

它解决的是 backend 与 frontend 之间，以及 backend 内部装配层之间的低频消息暂存和转交问题。发送方把消息投递到 Mailbox，接收方在自己的安全时机一次性取出并处理。

Mailbox 本身不处理业务逻辑。它只提供消息结构和队列能力。

```text
发送方
  -> 构造 Mail
  -> DeliverMail

接收方
  -> RetrieveMails
  -> 逐条处理 Mail
```

## 2. 使用场景

Mailbox 适用于这些场景：

- frontend 向 backend 投递命令。
- backend 向 frontend 投递回复或事件。
- backend 主循环在固定位置批量取出待处理消息。
- `Services` 按引擎 ID 管理 inbox/outbox。
- 与 ProcedureCall 配合，用 `MailHeader::nTypeCode` 找到处理函数。

Mailbox 与 PDO 的分工：

- Mailbox：低频、命令、事件、请求响应。
- PDO：高频、可丢弃状态数据。

## 3. 基本概念

### 3.1 MailHeader

`MailHeader` 是消息头。

```cpp
struct MailHeader
{
    uint64_t nMailId = 0;
    uint64_t nOriginId = 0;
    uint64_t nTypeCode = 0;
    StampCode nStamp = kMailOk;
};
```

字段含义：

- `nMailId`：当前消息 ID。
- `nOriginId`：原始消息 ID。回复消息使用它关联请求；`0` 表示没有原始消息。
- `nTypeCode`：消息类型码。常见用法是保存 ProcedureCall 的 `PCID`。
- `nStamp`：消息基础状态，主要用于回复。

### 3.2 StampCode

`StampCode` 是消息基础状态。

```cpp
enum StampCode : uint16_t
{
    kMailOk = 0,
    kMailUnknownType = 1,
    kMailNoHandler = 2,
    kMailInvalidPayload = 3,
    kMailExecutionError = 4,
    kMailTimeout = 5,
};
```

`StampCode` 只表达通用处理状态。业务错误码由业务 Payload 自己表达。

### 3.3 MailData

`MailData` 是消息体。

```cpp
struct MailData
{
    size_t nSize = 0;
    uint8_t* pData = nullptr;
};
```

Mailbox 不解析 `pData`，也不知道 Payload 的类型。

### 3.4 Mail

`Mail` 由消息头和消息体组成。

```cpp
struct Mail
{
    MailHeader Header;
    MailData Payload;
};
```

### 3.5 CMailBox

`CMailBox` 是消息队列。

```cpp
class CMailBox
{
public:
    void DeliverMail(const Mail& mail);
    std::vector<Mail> RetrieveMails();
    void ClearMails();
};
```

## 4. 行为规则

### 4.1 投递

`DeliverMail(mail)` 把消息追加到队列尾部。

规则：

- 投递顺序会被保留。
- `DeliverMail` 对 `Mail` 做浅拷贝。
- 如果 `Payload.pData` 非空，投递后这段内存进入 Mailbox 生命周期管理，直到被取出或清空。
- `DeliverMail` 内部加锁，可与其他 `DeliverMail`、`RetrieveMails`、`ClearMails` 并发调用。

### 4.2 取出

`RetrieveMails()` 返回当前所有消息，并清空队列。

规则：

- 返回顺序与投递顺序一致。
- 调用后 Mailbox 内部队列为空。
- 返回消息的 Payload 所有权转移给调用方。
- 调用方处理完返回消息后负责释放 `Payload.pData`。
- `RetrieveMails` 内部加锁，返回的是某个锁保护时间点之前已经进入队列的消息集合。

### 4.3 清空

`ClearMails()` 清空当前仍在队列中的消息。

规则：

- 会对仍滞留在队列里的每条消息执行 `delete[] Payload.pData`。
- 清空后队列为空。
- 已经通过 `RetrieveMails()` 取出的消息不再由 Mailbox 管理。
- `ClearMails` 内部加锁，可与投递和取出并发调用。

## 5. 生命周期与所有权

Mailbox 当前使用裸指针 Payload，因此所有权规则必须明确。

规则：

- 空 Payload 使用 `nSize == 0` 且 `pData == nullptr`。
- 非空 Payload 必须使用 `new[]` 分配，才能由 `delete[]` 释放。
- 投递后，如果消息仍在队列中，Mailbox 负责释放 Payload。
- 调用 `RetrieveMails()` 后，Payload 所有权转移给调用方。
- 调用方不能在投递后立即释放 Payload，否则 Mailbox 会持有悬空指针。
- 调用方不能对取出的 Payload 重复释放。
- `CMailBox` 禁止拷贝，避免多个队列持有同一批 Payload。
- `CMailBox` 支持移动，移动后 Payload 所有权随队列转移。

## 6. 线程模型

`CMailBox` 是方法级线程安全的消息队列。

线程安全范围：

- 多个线程可以同时调用 `DeliverMail()`。
- 一个线程可以调用 `RetrieveMails()`，同时其他线程调用 `DeliverMail()`。
- 一个线程可以调用 `ClearMails()`，同时其他线程调用 `DeliverMail()` 或 `RetrieveMails()`。
- 每次方法调用内部都会对队列加锁，保证内部 `std::vector<Mail>` 不发生数据竞争。

线程安全边界：

- `CMailBox` 的析构必须由对象生命周期所有者保证安全，不能在其他线程仍可能访问该对象时析构。
- 移动构造和移动赋值会加锁转移队列，但调用方仍应保证移动后没有线程继续访问被移动对象。
- Payload 指针指向的业务数据不由 Mailbox 加锁保护；业务数据在投递前和取出后的并发访问由调用方负责。

## 7. 上层使用样例

### 7.1 投递消息

```cpp
#include <Mailbox/MailBox.h>

#include <cstring>

using namespace iCAX::Mailbox;

struct RenameInput
{
    char Name[64];
};

CMailBox inbox;

RenameInput input{};
std::strcpy(input.Name, "Part001");

Mail mail;
mail.Header.nMailId = 1;
mail.Header.nTypeCode = 1001;
mail.Header.nStamp = kMailOk;
mail.Payload.nSize = sizeof(RenameInput);
mail.Payload.pData = new uint8_t[sizeof(RenameInput)];
std::memcpy(mail.Payload.pData, &input, sizeof(RenameInput));

inbox.DeliverMail(mail);
```

### 7.2 取出并释放 Payload

```cpp
auto mails = inbox.RetrieveMails();

for (auto& mail : mails)
{
    // 按 nTypeCode 和业务协议处理 mail.Payload。

    delete[] mail.Payload.pData;
    mail.Payload.pData = nullptr;
    mail.Payload.nSize = 0;
}
```

### 7.3 构造回复消息

```cpp
Mail reply;
reply.Header.nMailId = 2;
reply.Header.nOriginId = request.Header.nMailId;
reply.Header.nTypeCode = request.Header.nTypeCode;
reply.Header.nStamp = kMailOk;
reply.Payload.nSize = 0;
reply.Payload.pData = nullptr;

outbox.DeliverMail(reply);
```

### 7.4 与 ProcedureCall 配合

```cpp
#include <ProcedureCall/IPCRegistry.h>

auto mails = inbox.RetrieveMails();

for (auto& mail : mails)
{
    auto fn = iCAX::PC::GetGlobalPCRegistry()->Find(mail.Header.nTypeCode);
    int result = fn(&context, &mail.Payload, nullptr);

    delete[] mail.Payload.pData;
}
```

Mailbox 只负责把消息交到处理位置。ProcedureCall 负责把 `nTypeCode` 映射到函数。

## 8. 测试要求

单元测试目录：

```text
src/tests/icax/foundation/Mailbox/MailboxTest/
```

测试覆盖：

- `Mail` 默认值。
- 投递顺序。
- Header 保持。
- Payload 保持。
- `RetrieveMails()` 后队列清空。
- `ClearMails()` 后队列清空。
- 空 Payload。
- 移动构造转移队列。
- 移动赋值先清空目标再转移队列。
- 并发投递和取出。
