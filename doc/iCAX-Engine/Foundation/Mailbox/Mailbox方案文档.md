# Mailbox 方案文档

## 1. 目标问题

Mailbox 要解决的问题是：发送方和接收方不一定在同一段代码里即时调用，但需要在同一进程中交换低频消息。

解决方式是提供一个本地队列：

```text
发送方 DeliverMail
  -> 队列暂存 Mail
接收方 RetrieveMails
  -> 批量取出并处理
```

Mailbox 不执行消息，不解释消息体，只管理消息暂存和所有权边界。

## 2. 工程位置

```text
src/icax/foundation/Mailbox/
```

工程文件：

```text
Mailbox.vcxproj
```

## 3. 模块结构

```text
MailboxExport.h
  -> DLL 导出宏

Mail.h
  -> StampCode
  -> MailHeader
  -> MailData
  -> Mail

MailBox.h / MailBox.cpp
  -> CMailBox
```

## 4. 核心数据结构

### 4.1 MailHeader

`MailHeader` 保存消息路由和关联信息。

```cpp
uint64_t nMailId;
uint64_t nOriginId;
uint64_t nTypeCode;
StampCode nStamp;
```

设计意图：

- `nMailId` 标识当前消息。
- `nOriginId` 支持请求/回复关联。
- `nTypeCode` 支持按类型路由，常见值是 ProcedureCall 的 `PCID`。
- `nStamp` 表达通用处理状态。

### 4.2 MailData

`MailData` 保存 Payload 指针和长度。

```cpp
size_t nSize;
uint8_t* pData;
```

当前采用裸指针是为了兼容现有调用方式，并保持底层结构简单。

### 4.3 CMailBox

`CMailBox` 内部保存：

```cpp
std::mutex m_Mutex;
std::vector<Mail> m_vecMails;
```

这是一个加锁保护的先进先出批量取出队列。

## 5. 投递方案

`DeliverMail` 当前实现：

```cpp
void CMailBox::DeliverMail(const Mail& mail)
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_vecMails.push_back(mail);
}
```

特点：

- 追加到队列尾部。
- 对 `Mail` 做浅拷贝。
- 不复制 Payload 内容。
- 不检查 Payload 合法性。

浅拷贝意味着投递后 Payload 指针必须继续有效，直到 Mailbox 清空它，或者接收方取出并释放它。

## 6. 取出方案

`RetrieveMails` 当前实现：

```cpp
std::vector<Mail> CMailBox::RetrieveMails()
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    std::vector<Mail> mails;
    mails.swap(m_vecMails);
    return mails;
}
```

特点：

- 一次性返回当前所有消息。
- 返回顺序与投递顺序一致。
- 调用后内部队列为空。
- Payload 所有权转移给调用方。
- 使用 `swap` 把内部队列整体转移给调用方，避免浅拷贝后再清空造成所有权不清晰。

## 7. 清空方案

`ClearMails` 当前实现：

```cpp
std::lock_guard<std::mutex> lock(m_Mutex);
for (auto& mail : m_vecMails)
{
    delete[] mail.Payload.pData;
    mail.Payload.pData = nullptr;
    mail.Payload.nSize = 0;
}
m_vecMails.clear();
```

它只负责清理仍在队列中的消息。

如果消息已经通过 `RetrieveMails` 返回给调用方，Mailbox 不再负责释放。

## 8. 移动与拷贝方案

`CMailBox` 禁止拷贝：

```cpp
CMailBox(const CMailBox&) = delete;
CMailBox& operator=(const CMailBox&) = delete;
```

原因是 `MailData::pData` 是裸指针。拷贝队列会造成多个 Mailbox 持有同一段 Payload，后续容易重复释放。

`CMailBox` 支持移动：

- 移动构造锁住源对象，然后通过 `swap` 转移内部 vector。
- 移动赋值同时锁住源对象和目标对象，先清理目标已有消息，再通过 `swap` 转移源队列。

## 9. 生命周期方案

Mailbox 的生命周期围绕 Payload 所有权展开。

```text
投递前：
  调用方拥有 Payload

DeliverMail 后：
  Mailbox 队列持有 Payload 指针

RetrieveMails 后：
  调用方重新获得 Payload 所有权

ClearMails 或析构：
  Mailbox 释放仍在队列中的 Payload
```

关键约束：

- 投递后的 Payload 不应由发送方立即释放。
- 取出后的 Payload 必须由接收方释放。
- Payload 必须与 `delete[]` 匹配。

## 10. 与其他项目的关系

### 10.1 ProcedureCall

Mailbox 可以把 `MailHeader::nTypeCode` 当作 ProcedureCall 的 `PCID`。

Mailbox 不依赖 ProcedureCall，也不负责查找函数。

### 10.2 Services

`Services` 使用 `CMailBox` 按引擎 ID 管理 inbox/outbox。

Foundation Mailbox 只提供队列；Services 提供装配和访问入口。

### 10.3 PDO

Mailbox 用于低频消息。PDO 用于高频可丢弃数据。

## 11. 线程方案

当前 `CMailBox` 使用 `std::mutex` 保护内部队列。

加锁范围：

- `DeliverMail`：锁住队列并追加消息。
- `RetrieveMails`：锁住队列并把内部 vector 交换到局部变量。
- `ClearMails`：锁住队列并释放仍在队列中的 Payload。
- 移动构造：锁住源对象并转移队列。
- 移动赋值：同时锁住源对象和目标对象，避免死锁后转移队列。

线程安全边界：

- 方法级并发访问是安全的。
- 对象析构仍需要外部保证没有其他线程继续访问该对象。
- Payload 指向的业务数据不由 Mailbox 保护。

## 12. 测试方案

测试工程：

```text
src/tests/icax/foundation/Mailbox/MailboxTest/MailboxTest.vcxproj
```

测试覆盖：

- `Mail` 默认值。
- 投递和取出顺序。
- Header 和 Payload 保持。
- `RetrieveMails` 清空队列。
- `ClearMails` 清空队列。
- 空 Payload。
- 移动构造。
- 移动赋值。
- 并发投递和取出。

验证命令：

```powershell
& .\src\tools\build\run_tests_debug_x64.ps1
```
