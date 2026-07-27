# FlatBuffers 规格文档

## 1. 定位

iCAX 的结构化平坦二进制数据直接使用 Google FlatBuffers，不定义第二套 Header、Section、Offset、Builder、View 或 Validator。

固定依赖版本为 `v25.12.19`。源码与 MSBuild 接入位于：

```text
src/third_party/flatbuffers/
```

本规范约束 iCAX 如何使用 FlatBuffers，适用于：

- ResourcePool 中由 iCAX 定义的结构化资源。
- PDO 中需要跨端读取的结构化快照。
- SDO/Mailbox 的结构化消息数据部分。
- 后端与前端共同读取的 BRep、Mesh 和其他中性表达。

FlatBuffers 不负责：

- Resource URL、Pool、作用域、持久化属性和内容版本。
- PDO 的 Slot、双缓冲、发布序号和租约。
- SDO 的路由、CorrelationID、投递状态和邮件生命周期。
- HTTP/file/resource URL 的获取、缓存和权限。
- BRep 拓扑、Mesh 索引等业务语义校验。
- 外部原生格式的解码，例如 PNG、STEP、GLB。

## 2. 唯一格式原则

不得再建立与 FlatBuffers 并行的通用平坦布局格式。下列自定义通用设施不再需要：

- `CFlatBlob`
- `CFlatBufferBuilder`
- `CFlatDocumentBuilder`
- `CFlatView`
- `CFlatValidator`
- iCAX 自定义通用 Blob Header 和 Section Directory

上层直接持有连续字节范围及其所有权，并使用官方生成 API 和 `flatbuffers::Verifier`。

业务确实需要外层元数据时，由所属系统表达：

```text
ResourceRecord
  URL
  ResourceVersion:uint64
  ContentHash
  MediaType / SchemaType
  Payload bytes

PDO Slot
  PDOID
  DataVersion:uint64
  PayloadSize（若交换层保存精确长度）
  Payload bytes

Mail Envelope
  MessageType
  CorrelationID
  Sender / Receiver
  Payload bytes
```

这些外层结构不能被重新包装为一个“通用 BinaryLayout 协议”。

## 3. Schema 所有权

`.fbs` 是协议源文件，也是持久化和跨端 ABI 的定义。

- Resource schema 由对应 Resource Type/Codec 模块拥有。
- PDO schema 由对应 PDO 类型的生产者和消费者共同拥有。
- SDO schema 由对应消息协议模块拥有。
- BRep 中性表达 schema 由 Geometry/BRep 协议模块拥有。
- 通用目录不得成为不受边界约束的全局 schema 仓库。

推荐目录：

```text
<owner-module>/
  Schema/
    Xxx.fbs
  Generated/
    cpp/
    ts/
```

生成代码必须由 `flatc` 产生，不得手工修改。生成代码与 `.fbs` 一起提交，使普通 C++ 编译不依赖本机预装 `flatc`。

## 4. Schema 设计规则

### 4.1 Table 与 Struct

需要持久化、跨版本读取或独立演进的对象必须使用 `table`。

`struct` 只用于满足以下全部条件的稳定值类型：

- 字段集合已确定，不需要新增字段。
- 字段宽度和顺序不会改变。
- 不需要字符串、vector 或其他非标量引用。
- 定长内联布局带来的性能收益是明确的。

已经发布的 `struct` 不得增加、删除、重排或改变字段类型。BRep 顶点、边、面等会持续演进的记录默认使用 `table`；仅 `Vec3d`、`Quaternion` 等真正稳定的数学值可考虑 `struct`。

### 4.2 字段演进

已经发布的 table 必须遵守：

- 不得改变已有字段的序号、类型、含义或默认值。
- 不得删除字段；废弃字段使用 `deprecated`。
- 新字段追加在末尾；若使用显式 `id`，该 table 的所有字段都必须一致使用显式 `id`。
- 新字段必须有旧读取器可忽略、旧数据可缺省的语义。
- 避免 `required`；无法提供兼容默认语义时才允许使用。
- 需要区分“未提供”和标量默认值时，使用 optional scalar。
- 不复用已废弃字段或枚举值的编号表达新含义。

枚举和 union：

- 新枚举值和 union variant 只追加，不重排、不复用旧编号。
- 读取端必须能处理未知枚举值或未知 union variant。
- 改变 union 中既有类型的含义属于不兼容修改。

### 4.3 Vector、索引和引用

- 变长集合使用 vector。
- 对二维或多维数组，使用拥有子 vector 的 table，或采用一维扁平 vector 加 offsets/counts。
- 对 BRep 图结构使用稳定局部 ID 或 vector index 建立引用，不保存裸指针。
- 索引宽度由可能的最大集合规模决定，发布后不得缩窄。
- 可选引用使用缺失 offset 表达，不使用进程内句柄。

### 4.4 根类型与标识

每种可独立存储、传输或按 URL 获取的根 payload 必须：

- 声明唯一且稳定的 `root_type`。
- 声明 4 字节 `file_identifier`。
- 在读取时同时检查 identifier 和 Verifier。

`file_identifier` 用于快速识别根 payload，不取代 Resource Type、MessageType 或 PDO Type。

同一 framing 中连续存放多条 FlatBuffer 时使用 size-prefixed buffer；若 ResourceRecord、PDO Slot 或 Mail Envelope 已提供精确 PayloadSize，不额外增加自定义长度头。

## 5. 版本模型

iCAX 中至少有三种不同版本，不得混用：

| 版本 | 类型 | 所属层 | 作用 |
|---|---:|---|---|
| FlatBuffers 工具版本 | `25.12.19` | 构建系统 | 固定生成器和头文件实现 |
| Schema 版本 | schema 自定义标量或发布基线 | 具体协议 | 标识业务语义演进或迁移基线 |
| Resource/PDO Data Version | `uint64` | ResourcePool/PDO | 标识某个逻辑对象的内容修订 |

FlatBuffers table 自身通过字段存在性实现大部分前后兼容。只有业务语义确实需要迁移分支时，才在根 table 中增加 `schema_version:uint`；不能把它当成每次内容变更都递增的版本。

Resource 的 `ResourceVersion:uint64` 位于 ResourceRecord/version entry，不写入 payload，只要内容字节相同就应允许得到相同内容哈希。

## 6. 校验

来自文件、网络、ResourcePool、共享内存或其他进程的字节，在首次建立业务 View 前必须执行：

1. 检查字节范围和最大允许大小。
2. 检查 `file_identifier`。
3. 使用生成的 `VerifyXxxBuffer(flatbuffers::Verifier&)`。
4. 执行业务语义校验。

业务语义校验至少覆盖适用项：

- vector 数量上限。
- 索引小于目标集合长度。
- BRep 拓扑引用存在且方向合法。
- enum/union 未知值策略。
- 字符串编码、名称长度和值域。
- 递归深度、总节点数和内存预算。

Verifier 保证 FlatBuffer 的结构安全，但不保证 CAD 业务语义正确。不得以“来自本进程”为由跳过发布边界校验；由可信 Builder 创建且保持不可变的 buffer 可以缓存已验证状态。

## 7. 内存与生命周期

FlatBuffers View 不拥有内存。任何由 `GetXxx()`、table accessor、string 或 vector accessor 返回的对象都不得超过底层字节范围的生命周期。

使用者必须保证：

- buffer 在所有读取者释放前不移动、不覆盖、不释放。
- 发布给其他线程或前端后的 buffer 只读。
- PDO 不覆盖仍存在 reader lease 的 slot。
- SDO Envelope 持有 payload owner，直到投递结束。
- 映射文件关闭前释放所有 View。

不要求每次发布都创建新的 iCAX Blob 对象。可以直接使用 `FlatBufferBuilder` 的 buffer、PDO slot owner、共享 byte owner 或只读映射。

## 8. Resource 约束

所有外部资源先经过 ResourcePool：

```text
file:///...
https://...
  -> ResourcePool resolve/fetch
  -> ResourceRecord/version
  -> payload bytes
  -> codec or generated FlatBuffers View

resource://<pool>/<record>
  -> ResourceRecord/version
  -> payload bytes
```

ResourcePool 不根据 URL scheme 猜测 payload 是否为 FlatBuffers。Record 的类型/媒体信息选择正确的 Codec 或生成 View。

- iCAX 原生结构化资源优先使用 FlatBuffers。
- PNG、JPEG、STEP、GLB 等外部原生资源保持原格式二进制，不为统一形式而包一层 FlatBuffers。
- 用户保存工程时是否内嵌外部资源，由 ResourcePool 持久化策略决定。
- 超大资源的 chunk、range、manifest 和缓存由 Resource 层管理，不定义通用 FlatBuffers Section Directory。

## 9. PDO 约束

PDO 的 payload 可以是 FlatBuffer，但 PDO 继续拥有交换协议：

- active/inactive slot。
- data version。
- payload size/capacity。
- reader lease。
- 丢帧和发布策略。

高频 PDO 不要求每帧堆分配：

- 每个可写 slot 持有长期存在的 Builder/buffer，发布前 `Finish()`。
- slot 被回收后调用 `FlatBufferBuilder::Clear()` 复用已分配容量。
- 或为 Builder 提供受 PDO slot/arena 管理的自定义 allocator。
- 若 Builder 不直接拥有 slot 内存，可将完成后的字节一次复制到 inactive slot。

`Clear()` 只能在不存在指向旧 buffer 的读取 View 时执行。双缓冲或多 slot 的意义正是让生产者构建新快照时，消费者仍可读取上一个不可变快照。

PDO shared arena v3 在每个双缓冲区上同时保存 payload capacity 和本次发布的精确 payload size。FlatBuffer PDO 因而使用生成代码的普通 `FinishXxxBuffer()`，不增加 size prefix：writer 把完成后的字节复制到 inactive slot，`Commit()` 发布实际大小；reader 只在 `CPDOReadLease::PayloadSize()` 给出的范围内执行普通 `Verifier`。旧版本 arena 会在打开时因版本不匹配被明确拒绝。

## 10. SDO 约束

SDO 的数据部分使用 FlatBuffer；Mail Envelope 不解释业务 payload。

```text
Mail Envelope
  routing/lifecycle metadata
  PayloadOwner
  PayloadSize
  FlatBuffer bytes
```

收件者根据 MessageType 选择生成的根类型，完成 identifier、Verifier 和业务校验后读取。

## 11. 前端约束

前端从同一份 `.fbs` 生成 TypeScript/JavaScript 访问代码，并根据 Resource URL 取得字节后解析。

前端只感知：

```text
URL + ResourceVersion + payload bytes + schema-generated API
```

前端不得感知后端 Database SQL，也不得把资源 URL 解析能力扩展成数据库查询接口。资源解析与 SDO/Database 交互是两条独立边界。

## 12. BRep 约束

BRep 中性表达可以由 `.fbs` 表达，但必须从“C++ 对象图”转换为“索引化数据模型”：

```text
BRepDocument
  vertices:[Vertex]
  edges:[Edge]
  loops:[Loop]
  faces:[Face]
  shells:[Shell]
  solids:[Solid]
```

对象之间使用 ID/index、方向标记和范围引用。复杂几何参数使用 union 表达不同曲线/曲面类型。schema 只表达结构，流形性、闭合性、方向一致性和几何容差仍由 BRep Validator 负责。

三角剖分可由前端根据 BRep FlatBuffer 生成；若性能需要，也可以作为独立、可丢弃、带来源版本的派生 Resource 缓存。不得把三角网格当成 BRep 的唯一持久化表达。

## 13. 兼容性门禁

每次修改已发布 schema 必须：

- 使用固定版本 `flatc` 重新生成所有目标语言代码。
- 使用 `flatc --conform` 对上一发布基线执行兼容性检查。
- 运行旧 reader 读取新 writer 数据的测试。
- 运行 Verifier 损坏/截断数据测试。
- 对持久化格式保留必要的 golden buffer 或迁移测试。

破坏兼容性的修改必须创建新的根协议/identifier，或提供显式迁移边界，不能静默覆盖旧 schema。

## 14. 验收标准

- 仓库只存在 Google FlatBuffers 这一套通用平坦格式。
- 依赖版本、来源、许可和归档哈希可追踪。
- C++ 工程可通过统一 `.props` 直接包含官方头文件。
- `flatc` 可从固定源码复现构建。
- 示例 schema 可生成代码并通过 `--conform`。
- 自动测试证明 V1 reader 可读取追加字段后的 V2 table。
- 自动测试证明 Verifier 拒绝截断 buffer。
- 不再存在 BinaryLayout DLL 或 `CFlatBlob` 接口。
