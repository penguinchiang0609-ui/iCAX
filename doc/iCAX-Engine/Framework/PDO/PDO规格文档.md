# PDO 规格文档

## 1. 定位

`PDO` 是高频过程数据通道。

它用于同步可以被覆盖、可以丢弃旧值的状态数据，例如视图状态、光标状态、预览状态或前后端之间的连续刷新数据。

低频命令、事件、请求响应不使用 PDO，应该走 Mailbox。

## 2. 基本概念

```text
PDODecl
  描述一个 PDO 槽的版本、ID、方向和 Payload 大小。

IPDOSlot / CPDOSlot
  一个双缓冲数据槽。

IPDOHub / CPDOHub
  一组 PDO 槽的集合，负责按方向统一交换缓冲。
```

## 3. PDO ID

`PDOID` 是 `uint64_t`。

上层通过 `MakePDOID(typeName, instanceName)` 生成稳定 ID。

```cpp
auto id = iCAX::PDO::MakePDOID("Renderer.Camera", "Main");
```

同样的类型名和实例名必须得到同样的 ID，不同类型或不同实例必须尽量区分。

## 4. PDODecl

```cpp
struct PDODecl
{
    uint32_t nVersion;
    PDOID nID;
    PDODirection eDirection;
    int nPayloadSize;
};
```

- `nVersion`：Payload 协议版本。
- `nID`：PDO 槽 ID。
- `eDirection`：数据交换方向。
- `nPayloadSize`：Payload 字节数。

## 5. 数据方向

```cpp
enum PDODirection
{
    kDirectionNil = 0,
    kDirection2Inner = 1,
    kDirection2External = 2,
    kDirectionBoth = 3
};
```

- `kDirection2Inner`：外部写入，内部读取。通常在 backend 帧开始时交换。
- `kDirection2External`：内部写入，外部读取。通常在 backend 帧结束时交换。
- `kDirectionBoth`：双向槽，入向和出向交换都会处理。

## 6. Slot 使用规则

写入方通过 `GetWriteData()` 获取写缓冲区，写完后调用 `MarkWriteReady()`。

读取方通过 `GetReadData()` 获取当前读缓冲区。

只有调用 `SwapBuffersIfReady()` 后，写缓冲区中已经标记 ready 的数据才会变成读数据。

```cpp
struct ViewState
{
    float zoom;
    float panX;
    float panY;
};

auto id = iCAX::PDO::MakePDOID("View.State", "Main");
iCAX::PDO::CPDOSlot slot({ 1, id, iCAX::PDO::kDirectionBoth, sizeof(ViewState) });

ViewState state{ 1.5f, 10.0f, 20.0f };
std::memcpy(slot.GetWriteData(), &state, sizeof(ViewState));
slot.MarkWriteReady();

slot.SwapBuffersIfReady();

ViewState read{};
std::memcpy(&read, slot.GetReadData(), sizeof(ViewState));
```

## 7. Hub 使用规则

`GeneratePDOHub()` 根据一组 `PDODecl` 创建 Hub。

上层通过 `GetSlot(id)` 访问指定槽。

```cpp
auto hub = iCAX::PDO::GeneratePDOHub({
    { 1, cameraId, iCAX::PDO::kDirection2Inner, sizeof(CameraInput) },
    { 1, previewId, iCAX::PDO::kDirection2External, sizeof(PreviewState) }
});

auto& cameraSlot = hub->GetSlot(cameraId);
```

`SwapInSlot()` 只交换 `kDirection2Inner` 和 `kDirectionBoth` 的槽。

`SwapOutSlot()` 只交换 `kDirection2External` 和 `kDirectionBoth` 的槽。

## 8. 生命周期

`CPDOSlot` 拥有两个 Payload 缓冲区。

`CPDOHub` 拥有所有槽。

调用 `CPDOHub::Clear()` 后，已创建的槽会被释放，后续再通过旧 ID 获取槽会抛出异常。

## 9. 线程语义

`CPDOSlot` 使用原子状态维护读写缓冲索引和 ready 状态。

当前规格只保证单个槽的读写交换状态不会被普通数据竞争破坏。Payload 内容本身由调用方负责按约定写入和读取。

`CPDOHub` 的槽集合管理不是并发容器。初始化、清理和按 ID 访问应由上层生命周期保证。

## 10. 错误处理

`CPDOHub::GetSlot()` 查不到 ID 时抛出 `std::runtime_error`。

## 11. 单元测试

测试目录：

```text
src/tests/icax/framework/PDO/PDOTest/
```

覆盖内容：

- `MakePDOID()` 的稳定性和区分性。
- `CPDOSlot` 只有在 ready 后才交换读写缓冲。
- `CPDOHub` 根据方向交换入向和出向槽。
- 未知槽和 `Clear()` 后访问槽的异常行为。
