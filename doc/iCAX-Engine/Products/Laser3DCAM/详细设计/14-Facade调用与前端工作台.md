# 14 Facade 调用与前端工作台详细设计

## 1. 定位

产品前端通过 SceneProxy 调用 Laser3DCAM 对外 Facade。Facade 表达低频调用、结果和事件，PDO 承载高频显示和交互状态。

## 2. 方法身份

每个方法固定使用 `FacadeName.MethodName`：

```text
MachineDefinition.Import
Machine.Instantiate
Machine.Jog
WorkpieceModel.Import
Workpiece.Instantiate
IntentToolpath.CreateFromSelection
Project.Undo
```

不再使用统一 `Cam` 主命令，也不提供 `Cam.Machine.Import` 三段名称。Facade 名称表达稳定业务接口，方法名称表达行为。

实例信息必须位于参数：

```js
sceneProxy.invoke("Machine.Jog", {
  machineId,
  axis,
  delta,
});
```

不得生成 `MachineID#AxisNo#Jog` 一类实例地址。

## 3. 调用 envelope

JS SDK 把 `Machine.Jog` 的两段名称编码为内部 frame 的 `methodCode`，把参数序列化到 `payloadText`。同一次 Request、Report 和 Response 使用同一个 `callId`；业务 payload 不重复携带方法名或 requestId。

成功结果完成 Promise，失败 stamp 或错误 payload 拒绝 Promise。

## 4. Facade 与领域能力

```text
MachineDefinition.Import       -> 机床定义托管与产品设置
Machine.Instantiate            -> 机床描述加载与 Entity 展开
Machine.Jog                    -> 实例/轴校验与运行态更新
WorkpieceModel.Import          -> Resource import
Workpiece.Instantiate          -> Workpiece Entity 创建
IntentToolpath.CreateFromSelection -> IntentToolpath 领域模型
Toolpath.*                     -> 当前加工意图/刀路入口
Project.Undo / Project.Redo    -> Repository undo/redo
```

Facade 负责参数校验、上下文选择、事务边界和领域编排。可复用算法继续放在 Service，持久化数据继续放在 Component/Resource。

## 5. 前端调用

```js
const imported = await sceneProxy.invoke(
  "WorkpieceModel.Import",
  { sourcePath },
  { timeoutMs: 120000 },
);

await sceneProxy.invoke(
  "Workpiece.Instantiate",
  imported,
  { timeoutMs: 60000 },
);
```

前端不直接构造 Facade，不直接计算 methodCode，也不访问后端 Entity/Component。

## 6. 事件

主动事件同样使用两个名称段，例如 `PDORender.SlotAllocated`、`Machine.Changed`。Event frame 的 `callId` 为 0，通过 `SceneProxy.subscribe()` 接收。事件不替代方法结果。

## 7. 本地与后端状态

前端可本地维护面板展开、tab、筛选、loading 和相机交互临时状态。

工件、意图刀路、工艺、ProcessPath、机床实例、作业和安全检查结果属于后端主数据，前端只能通过 Facade、PDO 和 Resource 访问。

## 8. 验收

- `Machine.Jog` 可通过参数操作任意机床实例和轴；
- 不存在实例级 Facade 注册；
- 不接受三段方法名；
- Promise 可由结果 Facade 正确完成或拒绝；
- 跨 Facade 范围调用返回失败结果，不使 Scene 线程故障；
- C++ 与 JS 对两段名称生成一致 methodCode。
