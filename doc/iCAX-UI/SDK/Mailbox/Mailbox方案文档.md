# Mailbox 方案文档

## 1. 当前实现

代码位置：

```text
src/iCAX-UI/SDK/Mailbox/
```

文件：

- `commandRoute.mjs`
- `mailboxClient.mjs`
- `variantSerializer.mjs`

## 2. Promise 请求

`MailboxClient.request()` 创建 mail，投递给 bridge，并在 response `originId` 匹配时 resolve。

超时或 backend 返回错误时 reject。

## 3. 命令编码

`commandRoute.mjs` 使用和 C++ 一致的 FNV-1a 32 位算法：

```text
typeCode = fnv(main) << 32 | fnv(sub)
```

JS 侧以十进制字符串保存 64 位值，避免超过安全整数范围。

## 4. Variant 编解码

`variantSerializer.mjs` 在普通 JS 对象和 backend Variant 文本之间转换。

## 5. Pending Map 方案

`MailboxClient` 维护 pending map：

```text
request id -> { resolve, reject, timer }
```

收到 response 后：

```text
originId
  -> find pending
  -> clear timer
  -> deserialize payloadText
  -> resolve or reject
```

找不到 pending 的 response 视为迟到或外部事件，不影响已有请求。

## 6. 订阅方案

`MailboxClient` 通过 bridge 的 `subscribeMail()` 接收指定 mailbox 的消息。

同一个 mailbox 第一次请求时建立订阅，之后复用订阅。

这种方式避免每个 request 都重复注册 host callback。

## 7. Event 分发方案

`MailboxClient.receive()` 根据 `originId` 区分 response 和 event：

```text
originId != 0 -> response -> pending Promise
originId == 0 -> event -> event handler
```

事件 handler 按两种 key 存储：

```text
channelId:typeCode
channelId:*
```

收到 event mail 后：

```text
typeCode
  -> typed handlers
  -> wildcard handlers
  -> deserialize payloadText
  -> call handler(event)
```

事件没有 pending Promise，也不参与 request timeout。

## 8. 编码一致性测试

测试必须覆盖：

- `makeCommandTypeCode("App", "GetState")`
- `makeCommandTypeCodeFromCommand(AppCommands.getState)`
- `makePDOID(typeName, instanceName)`

这些值必须和 C++ 侧算法一致。

## 9. 验收标准

- request 能成功 resolve。
- timeout 能 reject。
- response `originId` 匹配正确。
- backend 主动 event mail 能触发 `subscribe()`。
- backend 主动 event mail 能触发 `subscribeAll()`。
- 取消订阅后不再触发 handler。
- Variant 文本往返后结构一致。
- 64 位 typeCode 在 JS 侧不发生精度损失。

