# PDO 方案文档

## 1. 当前实现

代码位置：

```text
src/iCAX-UI/SDK/PDO/
```

文件：

- `pdoClient.mjs`

## 2. ID 生成

`PDOClient.makeId(typeName, instanceName)` 复用 `Mailbox` 的 hash 算法，和 C++ `PDOID` 保持一致。

## 3. 读取流程

```text
ProjectProxy
  -> PDOClient
    -> findDeclaration()
      -> bridge.pdo.withRead()
```

## 4. Shared Memory 映射方案

真实 CEF/宿主适配器下需要把 OS shared memory 映射为 JS 可读视图，并明确 lease 释放时机。

映射策略：

- C++ 侧创建 shared memory arena。
- WebPageHost 根据 project PDO descriptor 暴露 arena 名称和 declaration。
- JS 侧通过 bridge 申请 read lease。
- bridge 在 lease 有效期内提供 `ArrayBuffer` 或 typed array view。
- reader 返回后释放 lease。

## 5. 双缓冲/版本方案

PDO 接受丢帧，因此前端读取最新稳定数据。

写侧必须避免覆盖读侧正在读取的数据。当前前端只表达 read lease，不直接决定缓冲策略；缓冲策略由 backend PDO arena 实现。

前端使用 `dataVersion` 判断数据是否变化：

```text
if declaration.dataVersion > lastReadVersion:
  read and parse
else:
  skip
```

## 6. 与产品协议关系

`PDO` 不知道 mesh、刀路或仿真帧格式。

产品应在：

```text
src/apps/<product-id>/protocol/
```

定义 PDO 的名称、二进制布局、版本和解释方式。

## 7. 验收标准

- `PDOClient.makeId()` 与 C++ PDOID 一致。
- `findDeclaration()` 可以按类型和实例查找。
- PDO 未启用时 `withRead()` 返回空或明确错误。
- bridge 不支持 PDO 时抛出明确错误。

