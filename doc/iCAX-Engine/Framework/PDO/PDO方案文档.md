# PDO 方案文档

## 1. 目标

PDO 解决 backend 与 frontend 之间高频过程状态同步的问题。

它不提供消息队列、不提供命令分发、不保证每次写入都被消费。它的设计重点是低成本读取最新状态，并通过运行期 data version 避免 Mesh 等重数据在未变化时重复序列化。

当前方案统一采用 Windows OS shared memory，不再维护其他缓冲实现路径。

## 2. 所属层级

PDO 位于 Framework：

```text
src/icax-engine/framework/PDO/
```

原因是 PDO 服务于 iCAX 的 backend/frontend 框架通信模型，不再作为完全独立的 Foundation 基础类型存在。

PDO 依赖 Foundation/Data 的稳定 ID 能力。

## 3. 文件结构

```text
PDO.vcxproj
PDO.h
PDODecl.h / PDODecl.cpp
PDOPayload.h
PDOLease.h
IPDOSlot.h
IPDOHub.h / IPDOHub.cpp
SharedPDOArena.h / SharedPDOArena.cpp
PDOHub.h / PDOHub.cpp
```

- `PDODecl.*`：定义 `PDOID`、方向和固定 payload 声明。
- `PDOPayload.h`：定义固定 payload 布局辅助和 `MakeTypedPDODecl<T>()`。
- `PDOLease.h`：定义 `CPDOReadLease` / `CPDOWriteLease`，封装读写租约生命周期。
- `IPDOSlot.*`：定义双缓冲 Slot 的抽象访问接口。
- `IPDOHub.*`：定义 Hub 抽象接口，并暴露共享内存名称、大小和声明快照。
- `SharedPDOArena.*`：实现共享内存 Arena 和共享内存 Slot。
- `PDOHub.*`：实现按方向批量交换的一组 Slot。

## 4. ID 设计

`MakePDOID(typeName, instanceName)` 复用 `iCAX::Data::MakeStableId()`。

这样 PDO 本身不维护哈希算法，也不引入新的 ID 体系。

PDOID 只负责定位约定好的 PDO 槽，不承担字段描述、版本迁移或跨版本协商职责。

## 5. 共享内存 Arena 设计

`CSharedPDOArena` 使用 Windows OS shared memory：

```text
CreateFileMappingW
OpenFileMappingW
MapViewOfFile
```

创建方创建 Arena。另一侧按名称打开同一块 mapping。CEF renderer bridge 可以把 payload 范围包装成 V8 `ArrayBuffer`，JS 侧再创建 TypedArray/DataView 视图。Slot header 的状态切换必须由 native bridge 调用 C++ API 完成，不能让 JS 直接写 header。

Arena 总体布局：

```text
SharedPDOArenaHeader
SharedPDOSlotHeader[slotCount]
payload buffer area
```

Arena header：

```cpp
struct SharedPDOArenaHeader
{
    uint32_t nMagic;
    uint32_t nVersion;
    uint32_t nSlotCount;
    uint32_t nSlotCapacity;
    uint32_t nHeaderSize;
    uint64_t nArenaSize;
    uint64_t nSlotTableOffset;
    uint64_t nPayloadOffset;
    uint64_t nPayloadSize;
    volatile long nDefragState;
};
```

Slot header：

```cpp
struct SharedPDOSlotHeader
{
    uint32_t nActive;
    PDOID nID;
    uint32_t nVersion;
    uint32_t nDirection;
    uint32_t nPayloadSize;
    uint64_t nPayloadBlockOffset;
    uint64_t nPayloadBlockSize;
    uint64_t nBufferOffset[2];
    volatile long nBufferState[2];
    volatile long nReaderCount[2];
    volatile long nPublishedIndex;
    volatile long nWriteIndex;
    volatile long nReadyIndex;
    volatile long nSequence;
    volatile unsigned long long nBufferDataVersion[2];
    volatile unsigned long long nPublishedDataVersion;
    volatile unsigned long long nLatestDataVersion;
};
```

`nSlotCount` 是 active slot 数，`nSlotCapacity` 是 slot table 容量。`nActive == 0` 的 slot header 表示空闲表项，可被后续动态分配复用。

`nVersion` 是 payload 协议版本；`nBufferDataVersion`、`nPublishedDataVersion` 和 `nLatestDataVersion` 是运行期源数据版本，语义上全部是 `uint64_t`。二者职责不同，不能混用。`nLatestDataVersion` 是单调的 Slot 最新源数据版本，即使 Ready buffer 被拿回去重写，它也不会回退。

所有 payload offset 都是相对 Arena base 的偏移，不存指针。browser process 和 renderer process 的映射地址可能不同，保存绝对指针是不可靠的。

## 6. 双缓冲设计

每个 Slot 固定两个 payload buffer：

```text
published buffer: 读侧可见
write buffer:     写侧写入
```

每个 buffer 还有一个状态：

```text
Free
Writing
Ready
Published
Reading
```

写入流程：

```text
CPDOWriteLease::TryBeginIfNewer(slot, dataVersion)
如果 dataVersion <= latest data version，跳过本次序列化
如果 write buffer 繁忙，返回 nullopt，本帧丢弃
buffer state -> Writing
写入固定 payload
Commit()
buffer data version = dataVersion
buffer state -> Ready
SwapBuffersIfReady()
ready buffer -> Published
published data version = ready buffer data version
old published buffer -> Free
sequence++
```

读取流程：

```text
CPDOReadLease(slot)
reader count++
buffer state -> Reading
返回 sequence、buffer index、data version 和 payload 指针
按固定 layout 解释 payload
lease 析构或 Release()
reader count--
如果该 buffer 已不是当前 published，则 buffer state -> Free
```

当前保持双缓冲，是为了贴近 EtherCAT PDO 这类“周期交换最新过程数据”的模型。PDO 接受丢帧，不保证每一帧都被消费；如果写侧在读侧处理前发布多次，读侧只关心最新可用状态。

双缓冲也意味着读侧不能长期持有读租约。一次交换之后，旧 published buffer 会成为下一轮可写 buffer；如果读侧还没释放读租约，阻塞写会等待，非阻塞写会返回 false 并丢弃本帧。PDO 接受丢帧，但不接受写侧覆盖正在读取的 buffer。

如果 write buffer 已经处于 `Ready`，但帧边界还没有执行交换，写侧可以用更高的 data version 重新进入 `Writing` 并覆盖该 ready buffer。这样 Mesh 版本连续变化时，PDO 会保留最近版本，丢弃中间未发布版本。

如果 write buffer 已经处于 `Writing`，说明当前存在 active write lease。此时不能再次返回同一块 buffer；非阻塞写返回 false，阻塞写会等待该租约 Commit 或 Cancel。

如果覆盖 ready buffer 的写租约最终 Cancel，Slot 会恢复原来的 ready buffer。这样序列化失败或中途放弃时，不会把已经准备好但尚未交换出去的旧版本误删。

## 7. Hub 设计

`CPDOHub` 内部持有一块 `CSharedPDOArena`，并使用：

```cpp
std::unordered_map<PDOID, std::shared_ptr<CSharedPDOSlot>>
```

管理槽对象。

`CPDOHub::Intialize()` 会：

1. 生成一个唯一的 `Local\\iCAX.PDO.Hub.*` mapping 名称。
2. 调用 `CSharedPDOArena::Create()` 创建共享内存 Arena。
3. 根据创建参数中的初始声明分配初始 slot，保存到 map。

动态 Hub 使用 `CPDOHubCreateInfo` 创建：

```cpp
iCAX::PDO::CPDOHubCreateInfo info;
info.nArenaSize = 64ull * 1024ull * 1024ull;
info.nSlotCapacity = 4096;
auto hub = iCAX::PDO::GeneratePDOHub(info);
```

运行期新增高频数据时，业务层调用：

```cpp
iCAX::PDO::CPDOHubAllocationCallbacks callbacks;
callbacks.OnDefragmentBegin = []()
{
    // 可选：通知前端暂停使用旧 offset。
};
callbacks.OnDefragmentEnd = [](const std::vector<iCAX::PDO::CPDOHubDefragMove>& moves)
{
    // 可选：根据 moves 通知 SlotMoved，然后通知 DefragEnd。
};

auto& slot = hub->AllocateSlot({ version, pdoId, direction, payloadSize }, callbacks);
```

删除高频数据时调用：

```cpp
hub->FreeSlot(pdoId);
```

`AllocateSlot()` 从 Arena payload 区分配连续空间。`FreeSlot()` 释放空间，并在 Hub 的空闲块表中合并相邻块。

当分配失败但空闲总容量足够时，说明 free list 已经碎片化。此时 `CPDOHub` 会在 PDO 模块内部自动执行碎片整理：设置 Arena `nDefragState`，等待相关 slot 的读写租约结束，把 active slot 的 payload block 按 offset 顺序向前搬移，更新 slot header 中的 `nPayloadBlockOffset` 和 `nBufferOffset[]`，最后重建 free list 并重试分配。

外部不能直接调用碎片整理，也不能参与 free list 管理。需要通知前端时，只能通过 `AllocateSlot(decl, callbacks)` 传入开始/结束回调。开始回调用于发送 `DefragBegin`；结束回调携带移动列表，调用方可以发送 `SlotMoved(PDOID)` 和 `DefragEnd`。前端必须按 `PDOID` 重新解析 offset，不得缓存旧 offset。

`SwapInSlot()` 遍历槽集合，只交换 `kDirection2Inner` 槽。

`SwapOutSlot()` 遍历槽集合，只交换 `kDirection2External` 槽。

每个 Slot 只允许一个写入方向。需要双向过程数据时，产品应声明两个 PDOID，而不是让同一个 Slot 双写。

这个设计让 Project 或运行时调度可以在固定帧点统一处理：

```text
Frame Begin
  -> SwapInSlot()
  -> backend update
  -> SwapOutSlot()
Frame End
```

如果某个场景需要把共享内存信息交给前端，则上层可以：

- 直接使用 `CSharedPDOArena::Create(name, decls)` 创建具名 Arena。
- 通过 `IPDOHub::GetSharedArenaName()` 取得自动生成的名称。
- 通过 `IPDOHub::GetSharedArenaSize()` 取得 Arena 大小。
- 通过 `IPDOHub::GetPDODeclarations()` 取得声明快照。

跨进程只传 Arena name 和必要元信息，不传当前进程 base address。

## 8. 与 Mailbox 的关系

Mailbox 负责低频、必须被处理的消息。

PDO 负责高频、只关心最新值的状态。

二者可以同时存在，但互不调用。

在 CEF/H5 场景下，Mailbox 负责控制面：

```text
打开项目
创建/关闭 PDO Arena
通知前端 Arena 名称
通知前端某个 PDO slot 已分配、释放、移动或 arena 正在整理碎片
```

PDO 负责数据面：

```text
固定 payload layout
双缓冲
buffer state
reader count
sequence
共享内存 raw bytes
```

## 9. 与 Data 的关系

PDO 只依赖 Data 的稳定 ID。

`PDOID` 的语义仍归 PDO 管理，具体 ID 生成算法由 Data 提供。

## 10. 并发边界

`CSharedPDOSlot` 使用 Windows `Interlocked*` 操作维护双缓冲索引、buffer state、reader count、ready 状态和 sequence。

Payload 本身的读写一致性由调用顺序和读租约共同保证：写侧必须完整写入 payload 后再 `Commit()` 或 `MarkWriteReady(dataVersion)`；读侧必须用 `CPDOReadLease` 或 `BeginRead/EndRead` 包围读取过程。泄漏读租约会使阻塞写等待，并使非阻塞写持续返回 false。

版本判断由 Slot 头部的 data version 完成。产品侧典型写法：

```cpp
auto write = CPDOWriteLease::TryBeginIfNewer(slot, mesh.Version());
if (write.has_value())
{
    SerializeMeshToPDO(mesh, write->Data());
    write->Commit();
}
```

`CPDOHub` 使用 mutex 保护 slot map、动态分配和释放。释放 slot 后，旧 `IPDOSlot&` 引用立即失效；业务层必须通过生命周期和 mailbox 事件保证读写方不继续访问已释放 slot。

## 11. Arena 校验

`CSharedPDOArena::Create(name, decls)` 会校验输入声明，拒绝空名称、空声明、重复 ID、非法方向和非正 payload size。

`CSharedPDOArena::Create(name, createInfo)` 会校验 Arena 大小、slot capacity 和初始声明。动态 Arena 允许初始声明为空，但必须有可用 slot capacity。

`CSharedPDOArena::Open()` 会校验共享内存中的结构，避免 renderer 或其他进程打开坏 mapping 后继续拿到危险指针：

- magic、version、header size 必须匹配当前实现。
- slot count 不能超过 slot capacity，slot capacity 必须大于 0。
- slot table、payload offset 和 payload size 必须位于 Arena 范围内。
- 每个 active Slot 必须有合法 ID、version、direction 和 payload size。
- 每个 active Slot 的 payload block 和 buffer offset 必须 64 字节对齐，且 payload 范围不能越界。
- buffer state 必须是合法状态，published buffer 必须处于 `Published` 或 `Reading` 状态。
- reader count 必须为非负数；`Reading` 状态必须存在 reader。
- published/write/ready index 必须是合法双缓冲索引，published 和 write 不能指向同一个 buffer。
- Arena 内不能存在重复 PDOID。

## 12. JS/CEF 使用原则

PDO 不提供字段级 descriptor。前端 JS 通过产品配套 TS wrapper 解释 ArrayBuffer。

CEF native bridge 必须封装 Arena 打开、Slot 查询、读租约、写租约和释放逻辑。JS 不直接写 Slot header，不直接维护 reader count，也不直接修改 buffer state。

建议 bridge 操作边界：

```text
openArena(name)
listPDO()
beginRead(pdoId) -> payload view + sequence + bufferIndex + dataVersion
endRead(pdoId, bufferIndex)
tryBeginWrite(pdoId, dataVersion) -> payload view | null
commitWrite(pdoId, dataVersion)
cancelWrite(pdoId)
```

推荐产品侧为每个 PDO payload 生成或手写一份 wrapper：

```ts
class CameraPDOView {
  constructor(private buffer: ArrayBuffer, private offset: number) {}

  get version() {
    return new DataView(this.buffer, this.offset).getUint32(0, true);
  }
}
```

前端不应该把 PDO 当 JSON 或对象树解析。PDO 的价值就在于固定布局、直接视图和低拷贝成本。

## 13. 后续可演进点

- CEF bridge 中增加 V8 ArrayBuffer release callback 的生命周期联动。
- 为 `ICAX_DECLARE_PDO_PAYLOAD` 对应的 C++ payload 生成 TypeScript wrapper，降低产品侧手写偏移的风险。
- 增加更明确的 Arena 名称分配策略，便于日志诊断和前端调试。
- 在 Mailbox/Facades 层补充项目关闭通知，提醒前端宿主停止访问对应 Arena。

## 14. 测试

测试工程：

```text
src/tests/icax-engine/framework/PDO/PDOTest/PDOTest.vcxproj
```

测试运行后会生成 `PDOTest.exe`，用于验证 ID、payload 声明、RAII 租约、写租约独占、ready 覆盖取消恢复、共享 Slot、Hub、shared memory Arena、生命周期、轻量高频读写压力和异常行为。
