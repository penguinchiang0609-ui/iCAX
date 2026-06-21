# Mail 通信规格文档

## 1. 定位

Mail 通信是进程内低频消息交互能力，用于两个运行单元之间传递命令、事件和请求响应。

Framework 层只提供通用双端点模型：

```text
EndA <-> EndB
```

模块分成三层：

```text
CMailQueue
  单向邮件队列，只负责入队、取出、清空。

CMailChannel
  双向邮件通道，内部包含 A->B 与 B->A 两个 CMailQueue。

CMailPostOffice
  从 CMailChannel 的某个端点初始化出来的邮局，提供 Send / Receive。
```

Mail 通信不处理业务逻辑，不解析 Payload。需要按消息类型分发命令时，上层可把 `MailHeader::nTypeCode` 映射到 `CommandHandler` 的命令类型。

## 2. 使用场景

- 任意一端向另一端发送命令。
- 任意一端向另一端发送回复或事件。
- 接收端在自己的安全时机批量取出待处理邮件。
- `Services` 可把通用 EndA/EndB 映射成具体应用中的两个运行角色。

Mail 通信与 PDO 的分工：

- Mail 通信：低频、命令、事件、请求响应。
- PDO：高频、可丢弃状态数据。

## 3. 数据结构

### 3.1 MailHeader

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

- `nMailId`：当前邮件 ID。
- `nOriginId`：原始邮件 ID，回复邮件使用它关联请求；`0` 表示没有原始邮件。
- `nTypeCode`：邮件类型码，常见用法是保存命令、事件或响应类型。
- `nStamp`：通用处理状态，业务错误码由业务 Payload 自己表达。

### 3.2 StampCode

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

### 3.3 MailData

```cpp
struct MailData
{
    size_t nSize = 0;
    uint8_t* pData = nullptr;
};
```

Mail 通信不解析 `pData`，也不知道 Payload 的类型。

### 3.4 Mail

```cpp
struct Mail
{
    MailHeader Header;
    MailData Payload;
};
```

## 4. CMailQueue

`CMailQueue` 是单向队列。

```cpp
class CMailQueue
{
public:
    void Enqueue(const Mail& mail);
    std::vector<Mail> Drain();
    void Clear();
};
```

行为规则：

- `Enqueue(mail)` 把邮件追加到队列尾部。
- 队列保留入队顺序。
- `Enqueue` 复制 `MailHeader`，并深拷贝 Payload 内容。
- 调用方仍拥有传入 `Mail` 的 Payload；如果传入邮件使用 `new[]` 分配 Payload，发送后仍应由调用方释放。
- `Drain()` 返回当前全部邮件，并清空队列。
- `Drain()` 返回后，Payload 所有权转移给调用方。
- `Clear()` 清空仍滞留在队列里的邮件，并释放其 Payload。
- `CMailQueue` 禁止拷贝，支持移动。
- `CMailQueue` 的方法级访问是线程安全的。

## 5. CMailPostOffice

`CMailPostOffice` 是某一端看到的邮局。它不拥有队列，只绑定一个收件队列弱引用和一个发件队列弱引用。

它是轻量非拥有视图。获取 `CMailPostOffice` 不创建新队列，也不转移任何队列所有权。底层 `CMailChannel` 被移除或销毁后，旧邮局会失效，继续收发会抛出 `std::logic_error`。

```cpp
class CMailPostOffice
{
public:
    bool IsValid() const noexcept;
    void Send(const Mail& mail) const;
    std::vector<Mail> Receive() const;
    void ClearIncoming() const;
    void ClearOutgoing() const;
};
```

行为规则：

- `Send(mail)` 将邮件写入对端的收件队列。
- `Receive()` 从本端收件队列取出当前全部邮件。
- `ClearIncoming()` 清空本端收件队列。
- `ClearOutgoing()` 清空本端发件队列，也就是对端的收件队列。
- 默认构造的 `CMailPostOffice` 未绑定队列，`IsValid()` 返回 `false`。
- 对未绑定邮局调用 `Send`、`Receive`、`ClearIncoming`、`ClearOutgoing` 会抛出 `std::logic_error`。

## 6. CMailChannel

`CMailChannel` 是完整的双向通道。

```cpp
enum MailChannelEnd : uint8_t
{
    kMailEndA = 0,
    kMailEndB = 1,
};

class CMailChannel
{
public:
    CMailPostOffice GetPostOffice(MailChannelEnd end) noexcept;
    CMailPostOffice GetEndAPostOffice() noexcept;
    CMailPostOffice GetEndBPostOffice() noexcept;
    void Clear();
    void Reset();
};
```

内部包含两个方向：

```text
EndA -> EndB
EndB -> EndA
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

`Clear()` 只清空两个方向上的待处理邮件，已经发出的 `CMailPostOffice` 仍然有效。

`Reset()` 会丢弃当前两个方向的队列并创建新队列，已经发出的旧 `CMailPostOffice` 会失效。生命周期所有者关闭一个运行单元时，应使用 `Reset()`，避免旧邮局继续向已关闭的通道投递邮件。

## 7. 生命周期与所有权

当前使用裸指针 Payload，因此所有权规则必须明确。

- 空 Payload 使用 `nSize == 0` 且 `pData == nullptr`。
- 非空 Payload 必须使用 `new[]` 分配，才能由 `delete[]` 释放。
- `Send()` / `Enqueue()` 会深拷贝 Payload，调用方继续拥有传入邮件的 Payload。
- 邮件进入 `CMailQueue` 后，队列只负责释放自己拷贝出来的 Payload。
- `Drain()` 或 `Receive()` 返回后，队列中那份 Payload 所有权转移给调用方。
- 调用方处理完返回邮件后必须释放 `Payload.pData`。
- `CMailPostOffice` 不拥有队列，底层队列释放后旧邮局自动失效。
- `CMailChannel` 拥有两个底层 `CMailQueue`，因此由它负责释放仍未被接收的 Payload。
- `CMailChannel` 销毁后，由它获取到的 `CMailPostOffice` 不再可用，调用 `Send` / `Receive` / `ClearIncoming` / `ClearOutgoing` 会抛出异常。
- `CMailChannel::Reset()` 后，由它旧队列获取到的 `CMailPostOffice` 不再可用；重新获取邮局会绑定新队列。
- `CMailChannel` 禁止拷贝和移动，避免已经获取的 `CMailPostOffice` 指向失效或错位的队列。
- 可以随时从 `CMailChannel` 获取 `CMailPostOffice`，不会产生新队列。

## 8. 线程模型

`CMailQueue` 是方法级线程安全的。

- 多线程可以同时 `Enqueue()`。
- 一个线程可以 `Drain()`，同时其他线程 `Enqueue()`。
- 一个线程可以 `Clear()`，同时其他线程 `Enqueue()` 或 `Drain()`。

`CMailPostOffice` 的收发线程安全来自其绑定的 `CMailQueue`。

线程安全边界：

- `CMailChannel` 或 `CMailQueue` 的析构必须由生命周期所有者保证安全，不能在其他线程仍可能访问时析构。
- `CMailChannel::Reset()` 也属于生命周期操作，应在没有其他线程同时通过同一通道获取新邮局时调用；旧邮局会安全失效。
- `CMailPostOffice` 是轻量弱引用对象，背后的 `CMailChannel` 销毁后会变为无效邮局。
- Payload 指向的业务数据不由 Mail 通信模块加锁保护。

## 9. 上层使用样例

### 9.1 EndA 发送命令，EndB 接收

```cpp
#include <Mailbox/MailChannel.h>

using namespace iCAX::Mail;

CMailChannel channel;
auto endAOffice = channel.GetEndAPostOffice();
auto endBOffice = channel.GetEndBPostOffice();

Mail command;
command.Header.nMailId = 1;
command.Header.nTypeCode = 1001;
command.Payload.nSize = sizeof(int);
command.Payload.pData = new uint8_t[sizeof(int)];

endAOffice.Send(command);
delete[] command.Payload.pData;
command.Payload.pData = nullptr;
command.Payload.nSize = 0;

auto mails = endBOffice.Receive();
for (auto& mail : mails)
{
    // EndB 按 nTypeCode 处理 mail.Payload。
    delete[] mail.Payload.pData;
}
```

### 9.2 EndB 回复 EndA

```cpp
Mail reply;
reply.Header.nMailId = 2;
reply.Header.nOriginId = request.Header.nMailId;
reply.Header.nTypeCode = request.Header.nTypeCode;
reply.Header.nStamp = kMailOk;

endBOffice.Send(reply);

auto replies = endAOffice.Receive();
```

### 9.3 服务层映射示例

```cpp
auto role1Office = channel.GetPostOffice(kMailEndA);
auto role2Office = channel.GetPostOffice(kMailEndB);

role1Office.Send(command);
auto role2Mails = role2Office.Receive();
```

这里的变量名称属于服务层或应用层，`CMailChannel` 本身只知道 EndA/EndB。

## 10. 测试要求

单元测试目录：

```text
src/tests/icax/framework/Mailbox/MailboxTest/
```

测试覆盖：

- `Mail` 默认值。
- `CMailQueue` 入队顺序。
- `CMailQueue::Drain()` 清空队列。
- `CMailQueue::Clear()` 释放队列中 Payload。
- 空 Payload。
- `CMailQueue` 移动构造和移动赋值。
- 并发入队和取出。
- 默认 `CMailPostOffice` 未绑定时抛出异常。
- `CMailPostOffice` 是轻量非拥有视图，通道销毁后安全失效。
- EndA -> EndB 路由。
- EndB -> EndA 路由。
- 双向消息互不串包。
- 清空单向收件队列。
- 清空整个 `CMailChannel`。
- 重置 `CMailChannel` 后旧邮局失效，新邮局可继续收发。
