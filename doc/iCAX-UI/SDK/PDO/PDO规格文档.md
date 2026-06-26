# PDO 规格文档

## 1. 定位

`PDO` 是 SDK 内部的前端高频数据访问模块。

PDO 面向 shared memory，不走 mailbox payload。

## 2. 使用模型

项目打开后，project state 中包含 PDO descriptor。

前端通过 descriptor 找到 PDO 声明，再向 host bridge 申请 read lease。

## 3. 数据解释

PDO 不自描述业务结构。

某个 PDO 表达 mesh、刀路、仿真状态还是组件快照，由产品 `protocol/` 约定。

## 4. 边界

本模块只管理 PDO ID、声明查找和 lease 获取，不负责渲染和业务解析。产品页面通过 `ProjectProxy` 暴露的 `project.pdo` 使用本能力，不直接构造 `PDOClient`。

## 5. PDO Descriptor

project state 中的 PDO descriptor 至少包含：

- `enabled`：是否启用 PDO。
- `sharedArenaName`：shared memory arena 名称。
- `declarations`：PDO 声明列表。

每个 declaration 至少包含：

- `id`：PDO ID，使用字符串表达。
- `payloadSize`：payload 字节数。
- `version`：PDO 布局版本。
- `dataVersion`：数据版本。

PDO 不要求运行时 declaration 携带 `typeName` 或 `instanceName`。产品前后端提前约定 `typeName + instanceName -> PDOID`，前端用同一算法生成 ID 后在 declaration 中查找。

## 6. 读取语义

前端读取 PDO 时必须通过 lease：

```js
await project.pdo.withRead(typeName, instanceName, view => {
  // 在这里读取 view
});
```

要求：

- lease 期间数据不可被写侧破坏。
- reader 返回后 lease 结束。
- 前端不得跨异步边界保存底层视图。
- 如果前端只接受丢帧，则读取最新可用数据即可。

## 7. 版本语义

`version` 表达 PDO 布局版本。

`dataVersion` 表达业务数据版本。

前端可以用 `dataVersion` 判断是否需要重新解析大对象，例如 mesh、刀路、仿真帧。

## 8. 错误处理

以下情况应抛出明确错误：

- PDO 未启用。
- bridge 未提供 PDO 能力。
- reader 不是函数。
- 找不到声明。
- arena 不存在。
- payload size 或布局版本不匹配。

## 9. 验收要求

- 能根据 `typeName + instanceName` 生成稳定 PDO ID。
- 能从 descriptor 找到 declaration。
- 能调用 bridge 获取 read lease。
- 对不存在的 PDO 给出明确错误。
