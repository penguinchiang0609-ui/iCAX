# PDO 规格文档

## 1. 定位

`PDO` 是 backend 与 frontend 之间的高频过程数据通道。

它用于同步可以被覆盖、可以丢弃旧值的状态数据，例如视图状态、光标状态、预览状态、渲染实例矩阵等连续刷新数据。

低频命令、事件、请求响应不使用 PDO，应该走 Mailbox。

PDO 不做运行时字段自描述。它模拟硬件 PDO 的思路：通信双方提前约定 `PDOID + payload protocol version + payload size + 固定二进制 layout`。C++ 侧用固定结构写入，前端 JS 侧用配套 TypeScript wrapper 和 `ArrayBuffer/DataView/TypedArray` 解释。

PDO Slot 同时维护一份运行期 `dataVersion`。它不是 payload 协议版本，而是当前 payload 对应的源数据版本。例如 Mesh 自身版本没有变化时，写侧可以直接跳过序列化；只有 `mesh.Version() > slot.GetLatestDataVersion()` 时才需要写入 PDO。

当前 PDO 的唯一实现方式是 Windows OS shared memory。

## 2. 基本概念

```text
PDODecl
  描述一个 PDO 槽的版本、ID、方向和 Payload 大小。

IPDOSlot
  PDO 双缓冲槽抽象接口。接口稳定，具体实现为 CSharedPDOSlot。
  Slot 同时维护 published data version 和 ready data version，用于跳过重复序列化。

CSharedPDOArena
  一块 Windows shared memory Arena，包含 Arena header、Slot table 和所有 payload buffer。

CSharedPDOSlot
  引用 Arena 内一个 Slot header 和两个 payload buffer 的轻量对象。
  它不拥有内存，只操作共享内存中的状态字段和 payload。
  读侧通过 CPDOReadLease 声明读租约；写侧通过 CPDOWriteLease 写入。

CPDOReadLease / CPDOWriteLease
  Slot 的 RAII 读写租约。读租约析构时自动 EndRead；写租约未 Commit 时析构自动 CancelWrite。

PDOPayload
  固定 payload 布局辅助。产品结构可声明 S_PDOLayoutVersion，并通过 MakeTypedPDODecl<T>() 生成 PDODecl。

IPDOHub / CPDOHub
  一组 PDO 槽的集合，负责按方向统一交换缓冲。
  CPDOHub 内部创建并持有 CSharedPDOArena。
  IPDOHub 对外暴露 Arena name、Arena size 和 PDODecl 快照。
```

## 3. PDO ID

`PDOID` 是 `uint64_t`。

上层通过 `MakePDOID(typeName, instanceName)` 生成稳定 ID。

```cpp
auto id = iCAX::PDO::MakePDOID("Renderer.Camera", "Main");
```

同样的类型名和实例名必须得到同样的 ID，不同类型或不同实例必须尽量区分。

## 4. PDODecl 与 DataVersion

```cpp
struct PDODecl
{
    uint32_t nVersion;
    PDOID nID;
    PDODirection eDirection;
    int nPayloadSize;
};
```

- `nVersion`：Payload 协议版本，即结构布局版本。
- `nID`：PDO 槽 ID。
- `eDirection`：数据交换方向。
- `nPayloadSize`：Payload 字节数。

`PDODecl.nVersion` 不表达某个 Mesh、相机状态或实例矩阵当前是否发生变化。

运行时数据版本由 `IPDOSlot` 维护：

```cpp
uint64_t GetPublishedDataVersion() const;
uint64_t GetLatestDataVersion() const;
bool TryBeginWriteIfNewer(uint64_t dataVersion, void*& writeData);
void MarkWriteReady(uint64_t dataVersion);
void CancelWrite();
```

- `GetPublishedDataVersion()`：读侧当前可见 payload 的源数据版本。
- `GetLatestDataVersion()`：Slot 内最新版本，包含已经写完但尚未交换出去的 ready buffer。
- `TryBeginWriteIfNewer(dataVersion, writeData)`：只有传入版本大于 Slot 最新版本且写缓冲可立即取得时才返回写缓冲；缓冲繁忙时返回 false，本帧可丢弃。
- `MarkWriteReady(dataVersion)`：标记本次写入对应的源数据版本，版本必须大于 Slot 最新版本。
- `CancelWrite()`：取消已经取得但不发布的写缓冲；`CPDOWriteLease` 未提交析构时自动调用。

源数据版本由产品数据对象负责维护，PDO 只负责保存和比较版本号。

一个 Slot 同一时刻只允许一个 active write lease。写缓冲处于 `writing` 状态时，第二次 `TryBeginWrite()` 或 `TryBeginWriteIfNewer()` 必须返回 false，不能再次暴露同一块 buffer。

## 5. 数据方向

```cpp
enum PDODirection
{
    kDirectionNil = 0,
    kDirection2Inner = 1,
    kDirection2External = 2
};
```

- `kDirection2Inner`：外部写入，内部读取。通常在 backend 帧开始时交换。
- `kDirection2External`：内部写入，外部读取。通常在 backend 帧结束时交换。

每个 Slot 只允许一个写入方向。双向数据必须声明成两个 PDO，例如 `View.Input` 使用 `kDirection2Inner`，`View.State` 使用 `kDirection2External`。`static_cast<PDODirection>(3)` 这类双写方声明会被拒绝。

## 6. Shared PDO Arena 生命周期

创建方通过 `CSharedPDOArena::Create(name, decls)` 创建共享内存：

```cpp
auto arena = iCAX::PDO::CSharedPDOArena::Create(
    L"Local\\iCAX.PDO.Project.1",
    {
        { 1, cameraId, iCAX::PDO::kDirection2External, sizeof(RenderCameraPDO) },
        { 1, inputId, iCAX::PDO::kDirection2Inner, sizeof(ViewInputPDO) }
    });
```

另一侧通过同名 mapping 打开：

```cpp
auto arena = iCAX::PDO::CSharedPDOArena::Open(L"Local\\iCAX.PDO.Project.1");
```

Arena 的生命周期由持有 `CSharedPDOArena` 的进程对象控制。最后一个 mapping handle 关闭后，Windows 会释放对应共享内存对象。

Arena 通常由 Project 通过 `CProjectCreateInfo::PDODeclarations` 创建和持有。Mailbox 可以用于通知前端：“某个项目的 PDO Arena 名称是什么”。Project 关闭后会释放自己的 PDOHub，前端宿主应停止访问对应 Arena。

## 7. Slot 使用规则

写入方优先通过 `CPDOWriteLease::TryBeginIfNewer(slot, dataVersion)` 获取写租约。返回 `std::nullopt` 表示 Slot 内已有不低于该版本的数据，或本帧写缓冲繁忙；PDO 允许丢帧，上层应直接跳过本帧序列化。

读取方通过 `CPDOReadLease` 获取当前 published buffer。读租约析构时自动释放 reader count。

只有调用 `SwapBuffersIfReady()` 后，已经标记 ready 的写缓冲区才会成为新的读缓冲区。

```cpp
struct ViewStatePDO
{
    ICAX_DECLARE_PDO_PAYLOAD(1);

    float zoom;
    float panX;
    float panY;
};

auto id = iCAX::PDO::MakePDOID("View.State", "Main");

auto arena = iCAX::PDO::CSharedPDOArena::Create(
    L"Local\\iCAX.PDO.View",
    { iCAX::PDO::MakeTypedPDODecl<ViewStatePDO>(id, iCAX::PDO::kDirection2External) });

auto slot = arena->GetSlot(id);

ViewStatePDO state{ 1.5f, 10.0f, 20.0f };
auto write = iCAX::PDO::CPDOWriteLease::TryBeginIfNewer(slot, viewStateVersion);
if (write.has_value())
{
    std::memcpy(write->Data(), &state, sizeof(ViewStatePDO));
    write->Commit();
}

slot.SwapBuffersIfReady();

ViewStatePDO read{};
iCAX::PDO::CPDOReadLease readLease(slot);
std::memcpy(&read, readLease.Data(), sizeof(ViewStatePDO));
auto dataVersion = readLease.DataVersion();
```

## 8. Hub 使用规则

`GeneratePDOHub()` 根据一组 `PDODecl` 创建 Hub。Hub 内部同样使用 shared memory Arena，只是 Arena 名称由 `CPDOHub` 自动生成。

上层通过 `GetSlot(id)` 访问指定槽。

```cpp
auto hub = iCAX::PDO::GeneratePDOHub({
    { 1, cameraId, iCAX::PDO::kDirection2Inner, sizeof(CameraInputPDO) },
    { 1, previewId, iCAX::PDO::kDirection2External, sizeof(PreviewStatePDO) }
});

auto& cameraSlot = hub->GetSlot(cameraId);
```

`SwapInSlot()` 只交换 `kDirection2Inner` 的槽。

`SwapOutSlot()` 只交换 `kDirection2External` 的槽。

需要把 Arena 名称交给 CEF renderer 时，可以显式创建 `CSharedPDOArena`，或者通过 `IPDOHub` 获取：

```cpp
auto name = hub->GetSharedArenaName();
auto size = hub->GetSharedArenaSize();
auto decls = hub->GetPDODeclarations();
```

注意：只应该把 Arena name 和必要的元信息传给前端宿主。当前进程的 base address 不能跨进程传递，renderer 必须自己打开同名 mapping。

## 9. CEF/JS Bridge 约束

CEF/JS 侧不能直接修改 Slot header 中的 `bufferState`、`readerCount`、`publishedIndex`、`writeIndex`、`readyIndex`、`sequence` 和 data version 字段。它只能读取 payload buffer，或在 native bridge 授权下写入 payload buffer。

推荐 native bridge 暴露的最小操作：

```text
openArena(name) -> arenaHandle
listPDO(arenaHandle) -> PDODecl[]
beginRead(arenaHandle, pdoId) -> { payloadOffset, payloadSize, sequence, bufferIndex, dataVersion }
endRead(arenaHandle, pdoId, bufferIndex)
tryBeginWrite(arenaHandle, pdoId, dataVersion) -> { payloadOffset, payloadSize } | null
commitWrite(arenaHandle, pdoId, dataVersion)
cancelWrite(arenaHandle, pdoId)
```

JS 可以把 payload 范围包装成 `DataView` 或 `TypedArray`，但 header 状态切换必须由 native bridge 调用 C++ PDO API 完成。这样可以保留零拷贝读取，同时避免 JS 写坏共享状态。

## 10. 固定 Payload 约束

PDO Payload 必须是跨进程稳定的二进制布局：

- 只使用固定宽度类型，如 `uint32_t`、`int32_t`、`float`、`double`。
- 不使用 `std::string`、`std::vector`、虚函数对象、智能指针或普通指针。
- 不使用 `size_t`、平台相关 enum 宽度或编译器相关对象布局。
- 多字节字段按 little endian 解释。
- 结构体对齐必须由产品定义固定，并在 C++ 与 TS wrapper 中保持一致。

示例：

```cpp
struct RenderCameraPDO
{
    ICAX_DECLARE_PDO_PAYLOAD(1);

    uint32_t flags;
    float viewMatrix[16];
    float projectionMatrix[16];
};
```

C++ 侧用同一结构生成声明：

```cpp
auto decl = iCAX::PDO::MakeTypedPDODecl<RenderCameraPDO>(
    cameraId,
    iCAX::PDO::kDirection2External);
```

TS wrapper：

```ts
class RenderCameraPDOView {
  private view: DataView;

  constructor(private buffer: ArrayBuffer, private offset: number) {
    this.view = new DataView(buffer, offset);
  }

  get flags() {
    return this.view.getUint32(0, true);
  }

  get viewMatrix() {
    return new Float32Array(this.buffer, this.offset + 4, 16);
  }
}
```

## 11. 共享内存布局

共享 Arena 的整体布局：

```text
SharedPDOArenaHeader
SharedPDOSlotHeader[slotCount]
payload buffer area
```

每个 Slot 固定两个 payload buffer：

```text
published buffer: 读侧可见的最新稳定数据
write buffer:     写侧正在写入或等待发布的数据
```

每个 buffer 有状态：

```text
Free      空闲，可作为下一次写入缓冲。
Writing   写侧正在写入。
Ready     写侧已经写完，等待帧边界发布。
Published 当前读侧可见。
Reading   读侧正在读取；写侧不可复用。
```

Slot header 保存 `PDOID/version/direction/payloadSize/bufferOffset/bufferState/readerCount/publishedIndex/writeIndex/readyIndex/sequence/bufferDataVersion/publishedDataVersion/latestDataVersion` 等共享状态。

所有 offset 都是相对 Arena base address 的偏移，不保存进程内指针，因为不同进程映射到的虚拟地址可能不同。

## 12. 线程与进程语义

`CSharedPDOSlot` 使用 Windows `Interlocked*` 操作维护双缓冲索引、buffer state、reader count、ready 状态和 sequence。

当前规格保证单个 Slot 的缓冲状态不会被普通数据竞争破坏。Payload 内容本身由调用方负责按约定写入和读取。

写入方必须完整写入 payload 后再调用 `Commit()` 或 `MarkWriteReady(dataVersion)`。

读取方通过 `CPDOReadLease` 取得当前 published buffer 的读租约，通过析构或 `Release()` 释放。写侧在复用该 buffer 前会等待 reader count 归零；高频写侧可以使用 `CPDOWriteLease::TryBeginIfNewer()`，缓冲繁忙时直接跳过本帧。

读侧推荐流程：

```text
lease = CPDOReadLease(slot)
dataVersion0 = lease.DataVersion()
读取 data 对应 payload
lease 析构或 Release()
```

读写语义：

- 写侧不覆盖正在 Reading 的 buffer。
- 写侧在复用旧 published buffer 前会等待其 reader count 归零。
- 读租约必须及时释放；泄漏读租约会导致写侧等待或非阻塞写持续返回 false。
- 写侧在下一次交换后可能把旧 published buffer 作为 write buffer 复用。
- PDO 仍然允许丢帧：如果写侧在读侧处理期间发布了更新，读侧可以只消费下一次读取时的最新帧。
- 当前 PDO 不保证每次发布都被读侧消费。
- 如果 ready buffer 还没交换，写侧又收到更高版本源数据，可以覆盖该 ready buffer；旧 ready 数据会被丢弃。
- 写侧应该优先使用 `CPDOWriteLease::TryBeginIfNewer()`，避免 Mesh 等重数据每帧重复序列化，并避免高频帧循环被读租约阻塞。

`CPDOHub` 的槽集合管理不是并发容器。初始化、清理和按 ID 访问应由上层生命周期保证。

## 13. 错误处理

`CPDOHub::GetSlot()` 查不到 ID 时抛出 `std::runtime_error`。

`CSharedPDOArena::Create()` 会拒绝空名称、空声明、重复 PDOID、非法方向和非正 payload size。

`CSharedPDOArena::Create()` 如果发现同名 mapping 已经存在，会抛出异常，避免错误复用旧 Arena。

`CSharedPDOArena::Open()` 找不到共享内存或 Arena 结构非法时抛出异常。

打开 Arena 时会校验：

- magic、version、header size、slot count。
- Arena size、slot table offset、payload offset 和范围。
- 每个 Slot 的 ID、version、direction、payload size。
- 每个 Slot 的 buffer offset、buffer state、reader count、published/write/ready index。
- 重复 PDOID 和两个 buffer offset 相同的非法结构。

## 14. 单元测试

测试目录：

```text
src/tests/icax-engine/framework/PDO/PDOTest/
```

覆盖内容：

- `MakePDOID()` 的稳定性和区分性。
- `MakeTypedPDODecl<T>()` 使用 C++ payload 固定布局生成声明。
- `CPDOReadLease` 析构释放 reader count。
- `CPDOWriteLease` 未提交析构自动取消写入。
- active write lease 存在时，第二个写侧不能取得同一块 write buffer。
- 覆盖 ready buffer 的写租约取消后，会恢复原来的 ready 数据。
- 共享 Slot 只有在 ready 后才交换读写缓冲。
- 共享 Slot 在旧 published buffer 被读取时，写侧会等待读租约释放后再复用该 buffer。
- 高频读写压力下，非阻塞写不会卡死帧循环。
- `CPDOHub` 根据方向交换入向和出向槽。
- `IPDOHub` 可暴露共享内存名称、大小和声明快照。
- 未知槽和 `Clear()` 后访问槽的异常行为。
- `CSharedPDOArena` 创建固定双缓冲 Slot。
- 通过同名共享内存打开的 Arena 可以读到发布后的 payload。
- Arena owner 销毁后，同名 mapping 可以重新创建。
- 共享 Arena 对重复 ID、非法声明、未知 Slot、缺失 mapping 和被破坏 header/slot 的异常行为。
