# FlatBuffers 方案文档

## 1. 方案结论

删除自研 BinaryLayout Framework 工程，直接引入并固定 Google FlatBuffers。

```text
Google FlatBuffers
  flatc              构建期 schema 编译器
  C++ headers        运行期构建、View、Verifier
  generated code     各业务协议的类型安全 API
```

iCAX 只增加依赖接入、schema 管理规范和兼容性门禁，不在官方 API 外再包一层通用 Blob/Builder/View。

## 2. 目录

```text
src/third_party/flatbuffers/
  flatbuffers-25.12.19/   官方源码
  FlatBuffers.props       C++ 头文件接入
  build-flatc.ps1         从固定源码构建 flatc
  README.md               版本与来源

doc/iCAX-Engine/Framework/FlatBuffers/
  FlatBuffers规格文档.md
  FlatBuffers方案文档.md

src/tests/icax-engine/framework/FlatBuffers/FlatBuffersTest/
  Schema/
  Generated/
  generate.ps1
  ...
```

`FlatBuffersTest` 是第三方接入和项目使用规则的验证工程，不代表存在 `framework/FlatBuffers` 运行库。

## 3. 依赖方向

```text
                     Google FlatBuffers headers
                          ^       ^       ^
                          |       |       |
                     Resource    PDO     SDO
                          ^
                          |
                   BRep/Mesh codecs
```

业务模块只依赖：

- 官方头文件。
- 自己拥有的生成头文件。
- 必要的同层协议模块。

业务模块不得依赖测试 schema，也不得从 `X Other/legacy` 包含 FlatBuffers 头文件。

## 4. MSBuild 接入

C++ 工程导入：

```xml
<Import Project="...\third_party\flatbuffers\FlatBuffers.props" />
```

该 props 只设置：

```text
ICAXFlatBuffersVersion
ICAXFlatBuffersRoot
ICAXFlatBuffersIncludeDir
AdditionalIncludeDirectories
```

FlatBuffers C++ 是头文件实现，不增加链接库或运行时 DLL。

## 5. 代码生成

修改 schema 后的标准流程：

```powershell
# 构建固定版本 flatc
src/third_party/flatbuffers/build-flatc.ps1

# 由协议模块脚本生成 C++/TypeScript
flatc --cpp --scoped-enums -o Generated/cpp Schema/Xxx.fbs
flatc --ts -o Generated/ts Schema/Xxx.fbs

# 与上一发布 schema 比较
flatc --conform Schema/Baseline/Xxx.fbs Schema/Xxx.fbs
```

各协议模块提供自己的生成脚本，明确输入 schema、输出语言、固定参数和兼容基线。CI 中重新生成到临时目录并比较差异，防止提交过期生成代码。

## 6. 最小验证工程

测试包含两份兼容 schema：

```text
V1 TestEnvelope
  schema_version
  name
  values

V2 TestEnvelope
  schema_version
  name
  values
  content_hash       末尾新增字段
```

V2 writer 生成 buffer，V1 reader 在独立翻译单元中读取同一 buffer，从而真实验证旧生成代码忽略未知字段。另有截断 buffer 的 Verifier 测试。

## 7. Resource 接入

### 7.1 Record 定位与 payload

```text
URL
  -> ResourcePool
  -> Pool + RecordKey
  -> ResourceRecord
  -> selected ResourceVersion
  -> byte owner + exact byte range
  -> Codec / generated root View
```

ResourceRecord 负责 URL、版本、类型、哈希、来源和持久化属性。FlatBuffer payload 不重复保存 PoolID、RecordKey 或 ResourceVersion，除非它们本身是资源业务内容。

当前 `CFlatBufferResource` 只是 Resource 层的不可变 byte owner，用于让资源副本共享同一份 `std::vector<uint8_t>`；`ResourceFlatBuffer.h` 直接调用官方 Builder、Verifier 和 generated root API。它不是通用序列化格式，也不定义第二套 Blob Header。

### 7.2 外部格式

资源池统一管理来源，但不强制统一内容编码：

```text
STEP/PNG/GLB bytes  -> 原生 Codec
iCAX BRep/Mesh      -> FlatBuffers generated API
```

内嵌外部文件时保存其原始字节最有利于互操作。需要加速读取时，可另建带来源 hash/version 的派生 FlatBuffer Resource。

### 7.3 大资源

FlatBuffers 使用 32 位 offset 的常规格式适合单个可寻址 buffer。超大 BRep、点云或纹理集合应在 Resource 层拆分：

```text
manifest resource
  metadata
  chunk URLs
  chunk versions/hashes

chunk resource
  one FlatBuffer or native byte range
```

这样可以按 URL 独立下载、缓存、替换和回收，不需要重建一套通用 Section 容器。

## 8. PDO 接入

### 8.1 发布模型

FlatBuffers 从尾向前构建一个连续 buffer。PDO 使用 inactive slot 构建，Finish 后原子发布：

```text
reader -> active immutable slot N
writer -> inactive mutable slot N+1
Finish + Verify
publish dataVersion
reader acquires N+1
old slot waits for lease release
Clear/reuse old builder capacity
```

### 8.2 分配策略

不存在“每次 PDO 必须 new 一个 CFlatBlob”的要求，可按负载选择：

1. 每 slot 一个长期 Builder：最简单，`Clear()` 后复用容量。
2. 每生产线程一个 Builder：Finish 后复制一次到 inactive slot。
3. 自定义 allocator/arena：Builder 扩容和释放由 PDO 管理。
4. 大小上限稳定的 payload：预留接近峰值的初始容量，降低扩容。

发布后不能 Clear 或复用仍被 reader 引用的 Builder。因此单 Builder 复用与真正零拷贝跨帧读取不可同时覆盖同一内存；需要 slot/lease 隔离。

当前适配器直接采用普通 FlatBuffer。shared arena v3 为每个 buffer 保存精确 payload size；`PDOFlatBuffer.h` 把已完成的字节写入 inactive slot 并设置租约实际大小，随后仍由 `CPDOWriteLease::Commit()` 和 swap 发布。读端只校验该精确范围，返回的 generated View 只在 `CPDOReadLease` 生命周期内有效。

### 8.3 版本

```text
schema_version   结构/语义版本，随 schema 演进
dataVersion      某 PDO 内容修订，每次成功发布递增 uint64
```

两者独立。前端以 `PDOID + dataVersion` 判断缓存是否过期，再用生成代码读取 payload。

## 9. SDO 接入

Mail Envelope 持有一个通用 byte owner 和精确范围；FlatBuffers 不要求 `CFlatBlob`：

```cpp
struct MailPayload
{
    shared_owner owner;
    const uint8_t* data;
    size_t size;
};
```

具体 owner 类型由 Mailbox 的内存与并发模型确定。收件者在 Envelope 生命周期内验证和读取；需要异步保留时转移/共享 owner，而不是缓存 generated table 指针。

当前 SDO 以 `SDOPayload = std::vector<uint8_t>` 持有完整普通 FlatBuffer。SDO 已有消息边界，因此不使用 size prefix；`SDOFlatBuffer.h` 直接使用官方 Verifier 和 generated root API。

## 10. BRep 中性表达

### 10.1 数据建模

BRep 不直接序列化后端内核对象和指针。建议根结构按池组织：

```text
BRepDocument
  metadata
  geometryPool
    curves2d
    curves3d
    surfaces
  topologyPool
    vertices
    edges
    coedges
    loops
    faces
    shells
    solids
  attributes
```

引用使用局部 index/ID。curve/surface 的多态性使用 union。为了局部加载，可以把 geometry pool 或大参数数组拆成子资源 URL。

### 10.2 后端与前端

后端负责输出稳定的 BRep 中性表达；前端：

1. 根据 URL/Version 获取 BRep FlatBuffer。
2. Verifier 与 BRep 语义校验。
3. 根据显示精度和设备能力三角剖分。
4. 把结果保存在前端缓存，或写成可丢弃派生 Resource。

服务端仍可为无剖分能力的客户端、批处理或共享缓存生成 Mesh；这是一种可选执行位置，不改变 BRep 是权威资源的原则。

## 11. 前端接入

同一 `.fbs` 生成 TypeScript API。前端 loader registry 根据 Resource Type/identifier 选择解析器：

```text
URL + Version
  -> fetch through Resource client
  -> ArrayBuffer/Uint8Array
  -> identifier + generated accessor
  -> domain object/View
  -> render/tessellate
```

前端不拼 SQL，不直接访问 Database，也不根据 URL 构造数据库查询。SDO 只把 URL/Version 或有限的业务查询结果传给前端。

## 12. 安全与预算

Verifier 前先应用 transport/resource 的总大小上限；Verifier 后再应用业务数量上限。对于 BRep 等复杂资源，业务 Validator 接收预算：

```text
max bytes
max table/vector elements
max topology entities
max string length
max traversal depth
max tessellation output
```

网络和 file URL 得到的内容即使缓存进 ResourcePool，第一次解析时仍视为不可信。

## 13. 迁移

基础依赖迁移先移除了尚未被上层采用的自研 BinaryLayout 工程和测试。在此基础上，framework 已为 Resource、PDO、SDO 落下 Google FlatBuffers 字节所有权、精确边界、`Verifier` 和生成 root 读取适配器；具体业务 schema 仍由各业务模块逐类型迁移。

后续按类型迁移：

1. 为一个 payload 建立 `.fbs` 和兼容基线。
2. 生成 C++/TypeScript。
3. 在现有 owner/slot/envelope 上直接放 FlatBuffer bytes。
4. 增加 Verifier 和业务 Validator。
5. 双读旧格式与新格式。
6. 完成数据迁移后移除旧 codec。

不要一次性把所有资源包装成一个巨型 union schema；各协议应保持独立发布和演进。
