# PDORenderService 方案文档

## 内部结构

```text
CPDORenderService
    ProjectID
        RenderSceneID
            RenderData snapshot
            PDO output state
```

服务可以是 application 级服务实例，但数据必须按 `ProjectID + SceneID` 隔离。

`RenderData snapshot` 保存渲染输入：

- `Meshes`
- `Polylines`
- `Toolpaths`
- `Instances`
- `Styles`
- `Views`

`PDO output state` 只保存运行期 slot 身份和容量：

- `MeshSlots[GeometryID] -> { PDOID, PayloadCapacity }`
- `PolylineSlots[GeometryID] -> { PDOID, PayloadCapacity }`
- `ToolpathSlots[GeometryID] -> { PDOID, PayloadCapacity }`
- `ObjectSlots[ObjectID] -> { PDOID, PayloadCapacity }`
- `ViewSlot -> { PDOID, PayloadCapacity }`

它不保存 `PDOHub`、`IPDOSlot`、共享内存地址或 arena offset。项目关闭后这些状态随 project 清理。

## 每帧输出流程

`Update(application, product, project, deltaTime, totalTime)` 的流程：

```text
1. 从 ProjectContext 读取 ProjectID。
2. 复制该 ProjectID 下的 scene snapshot 和 PDO output state。
3. 从 ProjectContext 取得当前项目 PDOHub。
4. 对已经不存在的 scene 释放全部已分配 slot，并发 SlotFreed。
5. 对仍存在的 scene：
   5.1 释放已删除的 geometry/object/view slot，并发 SlotFreed。
   5.2 为新增的 geometry/object/view 分配 slot；分配时把 PDOHub 的碎片整理回调接到项目 mailbox。
   5.3 分配成功后发送 SlotAllocated。
   5.4 如果 payload 变大且旧容量不足，保持 PDOID 不变，释放旧 slot 后重新分配容量，并发 SlotMoved。
   5.5 将 RenderData 序列化写入对应 slot。
6. 把新的 PDO output state 写回服务。
```

`Update` 不长期持有项目 PDOHub，也不把 slot 引用缓存到服务中。它只缓存 `PDOID` 和容量。

## 对象级 slot

几何 slot 只描述几何数据本身；对象 slot 描述这个对象如何显示。

```text
Mesh slot
    GeometryID = 88
Object slot
    ObjectID = 100
    GeometryID = 88
    Transform = ...
```

因此同一个 mesh 可以被多个 object 复用，每个 object 都有独立 transform。默认情况下，一个 object 一个 PDO slot，不做隐式打包。

## 碎片整理模型

PDO arena 的碎片整理由 PDOHub 在 `AllocateSlot()` 内部自动触发。`PDORenderService` 不检查 free list，也不主动调用碎片整理；它只在分配 slot 时传入两个回调：

- `OnDefragmentBegin`：发送 `DefragBegin`。
- `OnDefragmentEnd(moves)`：按 `moves` 发送 `SlotMoved(PDOID)`，最后发送 `DefragEnd`。

邮件顺序为：

```text
DefragBegin
SlotMoved(PDOID)
SlotMoved(PDOID)
DefragEnd
```

前端收到 `DefragBegin` 后只保留 `PDOID`，不要继续使用旧 offset。收到 `DefragEnd` 后重新扫描或查询 shared memory arena，按 `PDOID` 找到最新 slot header 和 payload offset。

这个约定让 compaction 不需要和前端做复杂锁协商。前端渲染可以在整理期间跳过若干帧，PDO 本来就接受丢帧。

## 序列化边界

`RenderData` 是中立结构，`RenderPDO` 是二进制通信 layout。`PDORenderService` 负责两者之间的转换和校验。

当前写出内容包括：

- `Mesh`：点、法向、顶点色、索引。
- `Polyline`：点数组和分段 range。
- `Toolpath`：刀位点、刀轴、进给、刀路 span。
- `InstanceList`：对象实例、几何引用、显示类别、样式和变换矩阵。
- `ViewState`：viewport 尺寸、相机语义和矩阵。

序列化时先构造局部 header，再追加数组 payload，最后把 header 拷贝回 payload 起始位置。写入前使用 `RenderPDOValidation` 校验 header 和 payload 边界。

## 删除与关闭

`DestroyScene(ProjectID, SceneID)` 只删除 scene 数据，不直接释放 PDO slot，因为它没有 `ProjectContext`，也就拿不到项目 PDOHub。下一次 `Update` 发现 scene 不存在时释放 slot，并发送 `SlotFreed`。

`DestroyProject(ProjectID)` 用于项目关闭。项目 PDOHub 和前端 channel 会随 project 生命周期销毁，服务只清理自己的 scene 和 output state。
