# PDO 方案文档

## 1. 目标

PDO 解决 backend 与 frontend 之间高频过程状态同步的问题。

它不提供消息队列、不提供命令分发、不保证每次写入都被消费。它的设计重点是低成本读取最新状态。

## 2. 所属层级

PDO 位于 Framework：

```text
src/icax/framework/PDO/
```

原因是 PDO 服务于 iCAX 的 backend/frontend 框架通信模型，不再作为完全独立的 Foundation 基础类型存在。

PDO 依赖 Foundation/Data 的稳定 ID 能力。

## 3. 文件结构

```text
PDO.vcxproj
PDO.h
PDODecl.h / PDODecl.cpp
IPDOSlot.h
PDOSlot.h / PDOSlot.cpp
IPDOHub.h / IPDOHub.cpp
PDOHub.h / PDOHub.cpp
```

## 4. ID 设计

`MakePDOID(typeName, instanceName)` 复用 `iCAX::Data::MakeStableId()`。

这样 PDO 本身不维护哈希算法，也不引入新的 ID 体系。

## 5. Slot 设计

`CPDOSlot` 内部维护两个缓冲区：

```cpp
uint8_t* m_pDataArray[2];
std::atomic<uint8_t> m_nWriteIndex;
std::atomic<uint8_t> m_nReadIndex;
```

写入方始终写当前 `writeIndex`。

读取方始终读当前 `readIndex`。

写入完成后，调用方必须调用 `MarkWriteReady()`。这会把当前写缓冲标记为 ready。

交换时，如果当前写缓冲为 ready，则交换读写索引，并把新的写缓冲状态重置为空。

## 6. Hub 设计

`CPDOHub` 使用 `unordered_map<PDOID, shared_ptr<CPDOSlot>>` 管理槽。

初始化时根据 `PDODecl` 创建所有槽。

`SwapInSlot()` 遍历槽集合，只交换入向槽。

`SwapOutSlot()` 遍历槽集合，只交换出向槽。

这个设计让 ApplicationHost 可以在固定帧点统一处理：

```text
Frame Begin
  -> SwapInSlot()
  -> backend update
  -> SwapOutSlot()
Frame End
```

## 7. 与 Mailbox 的关系

Mailbox 负责低频、必须被处理的消息。

PDO 负责高频、只关心最新值的状态。

二者可以同时存在，但互不调用。

## 8. 与 Data 的关系

PDO 只依赖 Data 的稳定 ID。

`PDOID` 的语义仍归 PDO 管理，具体 ID 生成算法由 Data 提供。

## 9. 并发边界

`CPDOSlot` 使用原子变量保护缓冲状态。

`CPDOHub` 不把 map 操作设计为并发写场景。上层应先完成 Hub 初始化，再进入运行期读写和交换。

## 10. 后续可演进点

- 补充 Payload size 的输入校验。
- 补充重复 PDOID 的策略说明。
- 补充版本协商或版本兼容检查。
- 统一修正历史枚举命名中的拼写问题，并保留兼容别名。

## 11. 测试

测试工程：

```text
src/tests/icax/framework/PDO/PDOTest/PDOTest.vcxproj
```

测试运行后会生成 `PDOTest.exe`，用于验证 ID、Slot、Hub 和异常行为。
