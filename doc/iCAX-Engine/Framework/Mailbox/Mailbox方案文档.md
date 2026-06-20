# Mail 通信方案文档

## 1. 目标问题

Mail 通信要解决的是同一进程内两个运行单元之间的低频消息交换问题。发送方不直接调用接收方代码，而是把邮件放入通道；接收方在自己的安全时机批量取出。

Framework 层不限定通信双方。它采用“双向通道 + 单向队列 + 端点邮局”的通用方案：

```text
EndA post office
  Send
    -> AToB queue
      -> EndB post office Receive

EndB post office
  Send
    -> BToA queue
      -> EndA post office Receive
```

具体应用通信只是上层服务对 EndA/EndB 的一种映射，不进入 Framework 的核心通信模型。

## 2. 工程位置

```text
src/icax/framework/Mailbox/
```

工程文件：

```text
Mailbox.vcxproj
```

## 3. 模块结构

```text
MailExport.h
  -> DLL 导出宏

Mail.h
  -> StampCode
  -> MailHeader
  -> MailData
  -> Mail

MailQueue.h / MailQueue.cpp
  -> CMailQueue，单向队列

MailChannel.h / MailChannel.cpp
  -> MailChannelEnd 与 CMailChannel，双向通道

MailPostOffice.h / MailPostOffice.cpp
  -> CMailPostOffice，某一端的邮局
```

## 4. 核心设计

### 4.1 Mail

`Mail` 保持简单结构：

```cpp
struct Mail
{
    MailHeader Header;
    MailData Payload;
};
```

`MailHeader::nTypeCode` 是业务类型码。Mailbox 不解释它，命令语义由上层 CommandHandler 或业务协议解释。

### 4.2 CMailQueue

`CMailQueue` 是底层单向队列。

内部结构：

```cpp
std::mutex m_Mutex;
std::vector<Mail> m_vecMails;
```

方法：

```cpp
void Enqueue(const Mail& mail);
std::vector<Mail> Drain();
void Clear();
```

设计要点：

- `Enqueue` 只做浅拷贝，避免模块参与 Payload 序列化。
- `Drain` 使用 `swap` 整体取出队列，降低锁内开销。
- `Clear` 释放仍滞留在队列中的 Payload。
- 队列禁止拷贝，避免多个队列持有同一批裸指针 Payload。

### 4.3 CMailChannel

`CMailChannel` 拥有两个队列：

```cpp
std::shared_ptr<CMailQueue> m_AToB;
std::shared_ptr<CMailQueue> m_BToA;
```

端点枚举：

```cpp
enum MailChannelEnd : uint8_t
{
    kMailEndA = 0,
    kMailEndB = 1,
};
```

它负责根据端点获取邮局：

```cpp
CMailPostOffice GetPostOffice(MailChannelEnd end) noexcept;
CMailPostOffice GetEndAPostOffice() noexcept;
CMailPostOffice GetEndBPostOffice() noexcept;
void Clear();
void Reset();
```

返回的 `CMailPostOffice` 是非拥有弱引用视图，不分配队列；底层通道释放或 `Reset()` 后，旧邮局会失效并在使用时抛出异常。

EndA 邮局绑定：

```text
incoming = BToA
outgoing = AToB
```

EndB 邮局绑定：

```text
incoming = AToB
outgoing = BToA
```

### 4.4 CMailPostOffice

`CMailPostOffice` 是一个轻量收发入口，不拥有队列。

内部结构：

```cpp
std::weak_ptr<CMailQueue> m_pIncomingQueue;
std::weak_ptr<CMailQueue> m_pOutgoingQueue;
```

方法：

```cpp
bool IsValid() const noexcept;
void Send(const Mail& mail) const;
std::vector<Mail> Receive() const;
void ClearIncoming() const;
void ClearOutgoing() const;
```

`Send` 写入 outgoing queue，`Receive` 从 incoming queue 取出。

`CMailChannel` 禁止拷贝和移动。这样已经获取到的 `CMailPostOffice` 不会因为通道对象被移动而指向错位的队列。通道销毁后，邮局弱引用失效，不会悬空访问。

`Clear()` 只清空队列内容，不改变队列身份。`Reset()` 会先清空旧队列，再替换为新的 `CMailQueue`，用于关闭运行单元时切断所有旧邮局。

## 5. 运行体映射方案

`Mailbox` 不维护全局通道目录，也不按通信 ID 托管 `CMailChannel`。在具体应用框架里，可以由更高层服务或运行体把 EndA/EndB 映射成自己的两个运行角色：

```text
EndA = role 1
EndB = role 2
```

上层运行体可以保留业务友好的入口：

```cpp
CMailPostOffice GetFrontendPostOffice();
CMailPostOffice GetBackendPostOffice();
```

这里的角色命名属于运行体装配语义，不属于 `Mailbox` 的基础通道语义。

返回的 `CMailPostOffice` 是轻量对象，持有底层队列弱引用。生命周期所有者删除、重置或释放底层 `CMailChannel` 后，旧邮局变为无效对象，继续收发会抛出 `std::logic_error`。反复获取同一个端点的邮局不会产生新的队列。

当前 iCAX framework 使用 `Services/IMailChannelService` 作为应用级 channel 目录。ApplicationHost、ProductRuntime 和 Project 只保存自己的 mail id，通过该服务获取 frontend/backend post office；Mailbox 本身仍保持无业务角色的基础通信语义。

## 6. 生命周期方案

Payload 生命周期：

```text
发送前：
  调用方拥有 Payload

Send / Enqueue 后：
  目标队列持有 Payload 指针

Receive / Drain 后：
  调用方重新获得 Payload 所有权

Clear 或队列析构：
  队列释放仍滞留的 Payload
```

关键约束：

- 非空 Payload 必须由 `new[]` 分配。
- 接收方处理完邮件后必须 `delete[] Payload.pData`。
- 发送方在 `Send` 后不能再释放同一 Payload。
- `CMailPostOffice` 不负责队列生命周期。
- 生命周期所有者移除 `CMailChannel` 后，旧 `CMailPostOffice` 自动失效。
- 生命周期所有者调用 `CMailChannel::Reset()` 后，旧 `CMailPostOffice` 自动失效，新获取的邮局绑定新队列。
- 不能移动已经对外发放过邮局的 `CMailChannel`。

## 7. 线程方案

`CMailQueue` 方法级加锁，支持以下并发：

- 多线程同时 `Enqueue`。
- 一个线程 `Drain`，其他线程 `Enqueue`。
- 一个线程 `Clear`，其他线程 `Enqueue` 或 `Drain`。

`CMailChannel` 本身不额外加锁，双向线程安全由内部两个 `CMailQueue` 提供。

上层如果需要跨线程维护运行体目录，应自行保护目录结构；消息收发走 `CMailQueue` 自己的锁。

## 8. 与其他项目的关系

### 8.1 CommandHandler

Mailbox 只负责消息传输。需要处理前端命令时，上层适配器把 `Mail` 转换为 `CCommandRequest`，再交给 `CommandHandler` 分发。

### 8.2 运行体

当前 iCAX framework 中，`IMailChannelService` 负责按 mail id 找到 channel，`ApplicationHost`、`ProductRuntime` 和 `Project` 负责把通用 EndA/EndB 映射成当前层级里的 frontend/backend 邮局。

### 8.3 PDO

Mail 通信用于低频消息；PDO 用于高频状态数据。

## 9. 测试方案

测试工程：

```text
src/tests/icax/framework/Mailbox/MailboxTest/MailboxTest.vcxproj
```

测试覆盖：

- `CMailQueue` 入队、取出、清空、移动和并发。
- `CMailPostOffice` 默认未绑定时的异常行为。
- `CMailPostOffice` 轻量非拥有视图的复制语义和通道销毁后的失效行为。
- `CMailChannel` EndA/EndB 双向路由。
- 两个方向互不串包。
- 清空单方向和清空整个通道。
- Reset 后旧邮局失效，新邮局可继续收发。

验证命令：

```powershell
& .\src\tools\build\run_tests_debug_x64.ps1
```
