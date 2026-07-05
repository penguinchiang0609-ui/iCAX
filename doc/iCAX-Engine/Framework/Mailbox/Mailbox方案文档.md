# Mail 通信方案文档

## 1. 目标

Mail 通信解决同一进程内两个运行单元之间的低频消息交换。发送方不直接调用接收方代码，而是把邮件写入对端队列；接收方在自己的安全时机批量取出。

本模块目标：

- 保持通用 EndA/EndB 模型，不绑定业务角色。
- 保证同一方向内消息有序。
- 减少每封邮件带来的堆分配释放频次。
- 保持 payload 生命周期清晰，接收方统一调用 `ReleaseMailPayload`。

## 2. 总体结构

```text
CMailChannel
  EndA -> EndB: CMailQueue
  EndB -> EndA: CMailQueue

CMailPostOffice(EndA)
  incoming = EndB -> EndA
  outgoing = EndA -> EndB

CMailPostOffice(EndB)
  incoming = EndA -> EndB
  outgoing = EndB -> EndA
```

`CMailPostOffice` 只保存两个队列的弱引用，因此获取邮局成本很低，通道释放后旧邮局会自然失效。

## 3. 池化队列

`CMailQueue` 内部不再保存 `std::vector<Mail>` 和每封邮件独立 `new[]` 的 payload，而是使用两个预分配池：

```text
record pool
  CRecord[nMaxMailCount]

payload pool
  uint8_t[nPayloadPoolBytes]
```

每个 record 保存：

- `MailHeader`
- payload offset
- payload size
- payload block size
- generation
- state

record 状态：

```text
Free -> Pending -> Leased -> Free
```

发送流程：

```text
Send / Enqueue
  -> 从 free record 取一个 record
  -> 从 payload free list 切一段空间
  -> 复制 header 和 payload
  -> record 进入 Pending
```

接收流程：

```text
Receive / Drain
  -> Pending record 转成 Mail
  -> Mail payload 指向队列 payload pool
  -> Mail 携带 IMailPayloadLease
  -> record 进入 Leased
```

释放流程：

```text
ReleaseMailPayload
  -> 如果 payload 携带 lease，则调用 ReleaseMailPayloadLease
  -> record 回到 Free
  -> payload block 回到 free list
  -> 相邻 free block 合并
```

如果 `CMailQueue` 不是由 `std::shared_ptr` 拥有，`Drain()` 无法给返回邮件挂安全租约，会退回为深拷贝 payload。正常业务通过 `CMailChannel` 使用队列，通道内部队列由 `shared_ptr` 拥有，因此返回的是池租约。

## 4. Payload free list

payload pool 使用空闲块表管理：

```cpp
struct CFreeBlock
{
    size_t nOffset;
    size_t nSize;
};
```

分配策略：

- first-fit。
- 分配时按 8 字节对齐。
- 分配失败时 `TryEnqueue` 返回 `false`。
- `Enqueue` 把失败转换为 `std::runtime_error`。

释放策略：

- 归还 block 后按 offset 排序。
- 相邻或重叠 block 合并。
- 队列初始化时预留 `recordCount + 1` 个 free block 容量，释放路径不依赖临时扩容。

Mailbox 不做 PDO 那种碎片整理。邮件 payload 生命周期短，且接收方可能正在读取已经租出的 payload；移动 payload 会让问题复杂化。当前策略依赖相邻合并和容量配置解决低频控制消息的性能问题。

## 5. 统一释放入口

`ReleaseMailPayload` 是唯一合法释放入口：

```cpp
void ReleaseMailPayload(Mail& mail) noexcept;
```

实现逻辑：

- 如果 `mail.Payload.pLease` 非空，归还队列 record 和 payload block。
- 否则对 `mail.Payload.pData` 执行 `delete[]`。
- 释放后清空 `pData`、`nSize` 和租约字段。

因此业务层不需要知道 payload 来源。相应地，业务层禁止手写 `delete[] mail.Payload.pData`。

## 6. 发送接口分层

`CMailPostOffice` 提供三种发送方式：

```cpp
void Send(const Mail& mail) const;
void SendPayload(const MailHeader& header, const void* payload, size_t payloadSize) const;
void SendText(const MailHeader& header, const std::string& payloadText) const;
```

使用建议：

- 已经有 `Mail` 对象时用 `Send`。
- 只有 header + bytes 时用 `SendPayload`，避免构造临时 `Mail`。
- 普通 H5 JSON 命令或事件用 `SendText`。

`Send` 仍会复制 payload 到队列池中，调用方仍需释放传入的临时邮件。

## 7. 通道容量配置

通道创建参数：

```cpp
struct CMailChannelCreateInfo
{
    CMailQueueCreateInfo EndAToEndBQueue;
    CMailQueueCreateInfo EndBToEndAQueue;
};
```

两个方向容量独立。典型用法：

- 前端到后端命令少，可以较小。
- 后端到前端事件较多，可以较大。
- 项目通道可以比应用通道更大。

`CMailChannel::Reset()` 会按原始创建参数重建两个方向的队列。

## 8. Registry

`CMailChannelRegistry` 按 ChannelID 管理通道生命周期：

```cpp
bool CreateChannel(const uuid& channelID);
bool CreateChannel(const uuid& channelID, const CMailChannelCreateInfo& createInfo);
CMailPostOffice GetFrontendPostOffice(const uuid& channelID) const;
CMailPostOffice GetBackendPostOffice(const uuid& channelID) const;
bool RemoveChannel(const uuid& channelID);
void ClearChannels();
```

Registry 线程安全。它由 ApplicationHost 持有并显式注入 ProductRuntime / Project，不是全局单例。

`GetFrontendPostOffice` / `GetBackendPostOffice` 是 framework 装配层命名：

- frontend = EndA
- backend = EndB

Mailbox 核心层不理解这两个角色。

## 9. 线程模型

`CMailQueue` 每个公开方法内部加锁。

安全场景：

- 多线程同时发送。
- 接收线程 Drain，同时发送线程 Enqueue。
- Clear 与发送/接收并发。

生命周期场景由外部所有者负责：

- 通道关闭时调用 `RemoveChannel` 或 `Reset`。
- 旧 PostOffice 只持有弱引用，通道移除后使用时抛 `std::logic_error`。
- 已接收但未释放的 Mail 会通过租约延长队列寿命，直到 `ReleaseMailPayload`。

## 10. 与 WebView2 / OS shared memory 的关系

当前 Mailbox 是进程内预分配池，不直接暴露 OS shared memory 给 JS。

WebView2 的 JS 共享内存需要通过 WebView2 SharedBuffer 投递；这属于 UIContainer/Bridge 适配层，不放进 Mailbox 核心模块。Mailbox 的契约保持 `MailHeader + bytes payload`，后续可以在 bridge 层把同一套契约映射到 WebView2 SharedBuffer。

高频大数据仍由 PDO 负责，Mailbox 不承担 mesh、刀路、渲染数据的高频通道职责。

## 11. 测试方案

测试工程：

```text
src/tests/icax-engine/framework/Mailbox/MailboxTest/MailboxTest.vcxproj
```

核心测试：

- `SharedQueueDrainsPayloadAsLeaseAndReleaseReturnsStorage`
- `PreallocatedPoolRejectsWhenRecordOrPayloadSpaceIsFull`
- `SendTextWritesPayloadDirectlyToOutgoingQueue`
- `CreateChannelAcceptsPerDirectionQueueCapacity`

联动测试：

- `ProjectTest` 覆盖 Project 事件发送。
- `ProductTest` 覆盖 Product mailbox 命令和事件。
- `ApplicationHostTest` 覆盖 ApplicationHost/Product/Scene 通信链路。
- `RenderServiceTest` 覆盖 PDORenderService 通过 Scene mailbox 发 slot/defrag 事件。
