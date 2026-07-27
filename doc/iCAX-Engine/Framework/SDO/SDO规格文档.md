# iCAX SDO 对外规格

## 1. 文档地位

本文档定义 iCAX Service Data Object（SDO）的公开契约。产品、插件、前端 SDK、原生 UI 宿主和外部集成方只要生产或消费 SDO，就必须遵守本文档。

SDO 借用 EtherCAT 中“SDO 负责非周期服务、PDO 负责周期数据”的分层思路，但 iCAX SDO 是软件运行时协议，不是 EtherCAT、CANopen over EtherCAT（CoE）或其对象字典的线协议实现。两者不具备报文、索引或传输层兼容性。

公开契约由下列内容共同构成：

- 稳定方法名 `SDOName.MethodName`；
- 请求、汇报、响应和事件语义；
- payload schema、file identifier 与版本规则；
- 调用范围、状态码和错误语义；
- 本文档明确标记为公开的 C++、JavaScript 与 manifest 入口。

`CSDOFrame`、Endpoint、Channel、Queue 和方法码属于当前传输实现。除本文档明确规定的字段语义外，外部调用方不得依赖它们的内存布局。

## 2. SDO、PDO 与 Resource 的边界

| 通道 | 适用数据 | 交付语义 | 典型示例 |
| --- | --- | --- | --- |
| SDO | 低频控制、查询、事务性操作、进度汇报和离散事件 | 每个 Request 最终得到一个 Response；可有 Report | `Machine.Jog`、`Project.Undo` |
| PDO | 高频、连续、允许覆盖或丢帧的运行状态 | 发布最新值，不提供逐次请求响应 | 位姿、渲染快照、输入状态 |
| Resource | 可能较大的可寻址内容及其版本 | `HEAD/GET/PUT/DELETE/OPTIONS`、ETag 和条件请求 | 几何资源、工程内二进制对象 |

不得用 SDO 连续推送帧级状态，不得用 PDO 表达必须确认成功或失败的命令，也不得把大对象分片塞进 SDO 来替代 Resource。

## 3. 公开身份

### 3.1 方法名称

一个 SDO 方法的公开身份固定为：

```text
SDOName.MethodName
```

两段名称都必须匹配 `[A-Z][A-Za-z0-9_]*`，完整名称必须且只能包含一个点。

```text
合法：Machine.Jog
合法：WorkpieceModel.Import
合法：Project.Undo
非法：machine.Jog
非法：Cam.Machine.Import
非法：MachineID#AxisNo#Jog
```

SDO 名称描述稳定能力，不描述运行时实例。实例 ID、轴号、Entity ID 等必须位于参数中：

```text
Machine.Jog({ machineId, axis, delta })
```

### 3.2 稳定方法码

传输层分别对 SDO 名称和方法名称计算 32 位 FNV-1a：

```text
sdoCode    = FNV1a32(UTF8(SDOName))
methodCode = FNV1a32(UTF8(MethodName))
code64     = (sdoCode << 32) | methodCode
```

高 32 位是 `sdoCode`，低 32 位是 `methodCode`。C++ 使用 `uint64_t`；JavaScript 使用十进制字符串，禁止转换为 `Number`。

名称是协议身份，64 位方法码只是紧凑表示。注册时发现同名重复必须拒绝；发现不同名称产生相同编码必须报错，禁止静默覆盖。

## 4. 调用模型

### 4.1 帧类型

SDO 定义四类逻辑帧：

| Kind | 方向 | CallID | 含义 |
| --- | --- | --- | --- |
| `Request` | 调用方到提供方 | 非零 | 发起一次方法调用 |
| `Report` | 提供方到调用方 | 与 Request 相同 | 零到多次中间进度或状态汇报 |
| `Response` | 提供方到调用方 | 与 Request 相同 | 唯一的最终结果 |
| `Event` | 提供方到订阅方 | 不参与请求关联 | 离散通知 |

一次调用必须满足：

```text
Request -> Report* -> Response
```

同一次 Request、Report 和 Response 始终使用相同的 `CallID` 与方法码。每个 Request 必须最多产生一个 Response；Response 到达后该调用结束，之后到达的 Report 或 Response 必须被忽略或作为协议错误记录。

### 4.2 异步要求

跨端调用不得同步阻塞等待：

- C++：`CSDOInvoker::CallRemote()` 返回 `Task<CInvocationResult>`；
- JavaScript：`SDOClient.invoke()` 返回 `Promise`；
- 原生 UI 绑定应提供等价的 Task/async 接口。

调用方必须设置有限超时。Report 可以刷新调用的活跃超时，但不能完成调用。投递失败、超时、非 `Ok` Response 或 payload 解码失败都必须以失败完成 Task/Promise。

### 4.3 双向调用

前端可以调用后端公开的 SDO，后端也可以调用前端通过 `expose()` 公开的 SDO。调用方向不改变名称、payload、CallID、Report 或 Response 语义。

## 5. Payload

### 5.1 规范格式

跨模块或对外发布的结构化 SDO payload 应使用普通 Google FlatBuffer：

- 不增加 size prefix；
- 不增加 iCAX 私有 envelope；
- schema 必须声明四字符 `file_identifier`；
- root table 必须包含 `schema_version`；
- 接收端访问 root 前必须使用生成类型和 file identifier 执行 `Verifier`；
- 校验失败必须返回 `InvalidInvocation`，不得继续读取。

每个方法应分别说明 Request、Report、Response 和 Event 使用的 root 类型。没有字段的 payload 也应使用明确的空 table，不以空字节猜测语义。

`SDOPayload` 拥有完整字节序列。由 `TryGetFlatBufferSDORoot()` 返回的只读视图不得超过对应 `SDOPayload` 的生命周期。

### 5.2 Bridge 文本适配

当前 H5 Bridge 可以用 UTF-8 `payloadText` 承载 `VariantSerializer` 文本。这是 UI Bridge 适配格式，不改变 SDO 的调用语义，也不是新业务协议首选的数据格式。新增跨模块协议应优先定义 FlatBuffer schema。

### 5.3 大小与敏感信息

SDO 不保证适合大对象或流式传输。资源内容应改用 Resource API，高频流应改用 PDO。

错误文本、Report 和日志不得泄露密码、令牌或未授权的本地路径。权限校验属于 SDO 提供方职责，不能只依赖前端隐藏入口。

## 6. 状态与错误

标准调用状态：

| 状态 | 含义 |
| --- | --- |
| `Ok` | 方法成功，Response payload 有效 |
| `SDONotFound` | 当前调用范围没有注册目标 SDO |
| `MethodNotFound` | SDO 存在，但没有目标方法 |
| `InvalidInvocation` | 名称/编码、范围、参数、payload 或执行过程无效 |

失败 Response 应提供面向开发者的简短错误说明，但调用方不得解析错误文本来决定业务流程；业务可分支错误应在方法自己的 Response schema 中定义稳定枚举。

一次失败调用不得击穿 Application、Product 或 Scene 工作线程。协议边界必须把可预期的输入错误转换为失败 Response。

## 7. 运行范围与上下文

SDO Channel 有三个公开运行范围：

| 范围 | 可用上下文 |
| --- | --- |
| Application | `ApplicationContext` |
| Product | `ApplicationContext`、`ProductContext` |
| Scene | `ApplicationContext`、`ProductContext`、`ProjectContext`、`SceneContext` |

Project 是容器，不单独拥有 Channel；项目级命令通过其 Scene 范围调用。方法必须验证所需上下文，范围不匹配时返回失败结果。

应用设置只能由 ApplicationRuntime 工作线程中的 Application 范围 SDO 修改。其他范围不得通过调用参数获得写入应用设置的权限。

## 8. 注册、发现与所有权

SDO 注册只允许新增，不支持运行时覆盖。产品模块使用 `ICAX_REGISTER_SDO` 登记无状态 SDO 类型，`ProductRuntime` 只回放产品 manifest 中 `backend.modules.sdo` 指定模块的注册动作。

具体产品拥有自己的业务 SDO 契约。共享插件可以提供 Component、Resource、Behaviour、Service 和算法，但不得擅自把产品私有业务方法变成全局协议。

`CSDORegistry::GetMethods()` 可导出当前运行范围的方法清单，供诊断、测试和文档生成。清单用于发现能力，不替代版本化的产品规格。

## 9. 并发与线程

SDO 层不创建业务线程，也不替调用方选择 continuation 线程。

- Request 在对应 Application、Product 或 Scene 的运行线程中分发；
- Report handler 在执行 `DispatchAvailableFrames()` 的线程运行；
- C++ `CallRemote()` 创建 Task 时应绑定目标 scheduler；
- Scene 状态 continuation 应绑定 `Universe::GetEngineTaskScheduler()`；
- 前端原生 continuation 应绑定 `FrontendBridge::GetFrontTaskScheduler()`；
- JavaScript continuation 由前端事件循环调度。

Report handler 不得长时间阻塞分发线程。需要切换线程时，调用方必须显式调度。

## 10. 版本演进

已经发布的 `SDOName.MethodName` 不得改变含义或复用为另一项能力。兼容演进遵循：

- 新增可选字段；
- 保留旧字段编号，不重排或复用；
- 接收端忽略未知字段；
- 缺少新增字段时使用明确默认值；
- 通过 `schema_version` 拒绝无法兼容的主版本；
- 不兼容变更发布新方法名或新 SDO 名称。

方法、状态和 schema 的废弃必须先在产品规格中标记，并至少保留一个约定的迁移周期。当前实现不提供旧名称兼容层。

## 11. 公开接入面

### 11.1 C++

核心公开类型：

- `CSDOMethod` / `MakeSDOMethod()`；
- `CInvocation` / `CInvocationResult`；
- `ISDO` / `CSDO`；
- `CSDORegistry` / `CSDOInvoker`；
- `SDOPayload` 与 `SDOFlatBuffer.h` 辅助函数；
- `ICAX_REGISTER_SDO`。

### 11.2 JavaScript

核心公开入口：

```js
import {
  SDOClient,
  AppSDO,
  ProductSDO,
  ProjectSDO,
  makeSDOMethodCodeFromName,
} from "iCAX-UI/SDK/index.mjs";
```

产品页面通常通过 `AppProxy`、`ProductProxy`、`ProjectProxy` 和 `SceneProxy` 使用 SDO；只有协议层代码和白盒测试直接创建 `SDOClient`。

### 11.3 Manifest

产品后端 SDO 模块使用：

```json
{
  "backend": {
    "modules": {
      "sdo": [
        "../../${Platform}/${Configuration}/ProductProtocol.dll"
      ]
    }
  }
}
```

## 12. 发布验收

发布一个 SDO 方法前至少验证：

- 方法名满足两段命名规则，且不含实例 ID；
- C++ 与 JavaScript 生成相同 64 位方法码；
- Request、Report 和 Response 保持相同 CallID 与方法码；
- 正常、方法不存在、非法 payload、范围错误、超时和异常路径都有测试；
- FlatBuffer 的 file identifier、`schema_version` 和 `Verifier` 生效；
- 跨端调用不阻塞，continuation 回到约定 scheduler；
- 大对象和高频数据没有误用 SDO；
- 方法及其 Request/Report/Response schema 已进入产品对外规格。
