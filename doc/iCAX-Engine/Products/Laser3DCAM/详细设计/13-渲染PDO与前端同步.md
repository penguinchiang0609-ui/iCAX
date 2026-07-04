# 13 渲染 PDO 与前端同步详细设计

## 1. 模块定位

本模块负责把后端 CAM 数据转换为前端可渲染的数据，并通过 PDO 和 Mailbox 同步。

原则：

- 大数据走 PDO。
- slot 分配、释放和迁移通知走 Mailbox。
- 前端只渲染，不维护项目主数据。

## 2. 参与模块

后端：

- CamRenderSyncService。
- RenderData。
- RenderPDO。
- PDORenderService。
- PDOHub。
- MailChannel。

前端：

- ProjectProxy。
- PDO client。
- H5 WebGL/Three.js 渲染层。

## 3. 显示对象

一个可独立变换的显示对象对应一个 PDO slot。

典型对象：

- 工件显示网格。
- 切割头子部件。
- 切割头碰撞包络。
- 刀路曲线。
- 姿态场。
- 空移路径。
- 蛙跳路径。
- 安全检查标记。
- 仿真当前状态。

## 4. Slot 生命周期

```text
Allocate
  -> Notify frontend slot created
  -> Write data
  -> Frontend creates render object
Update
  -> Write data version
  -> Frontend reads changed data
Release
  -> Notify frontend slot released
  -> Frontend destroys render object
Compact
  -> Notify compact begin
  -> PDO moves slot
  -> Notify compact end with slot mapping
```

## 5. PDO 数据版本

每个 slot 应包含：

```text
SlotID
PayloadType
DataVersion
PayloadSize
PayloadOffset
```

前端只在 `DataVersion` 变化时重新读取。

## 6. Mail 通知

```text
Render.SlotCreated
  SlotID
  ObjectID
  ObjectKind
  PayloadType
```

```text
Render.SlotReleased
  SlotID
  ObjectID
```

```text
Render.PDOCompactionBegin
```

```text
Render.PDOCompactionEnd
  SlotMapping[]
```

## 7. 后端同步流程

```text
1. Observer 或 Service 标记对象 dirty
2. CamRenderSyncService 收集 dirty 对象
3. 查询 Database 和 Resources
4. 转换为 RenderData
5. 分配或复用 PDO slot
6. 写入 PDO
7. 必要时 mail 通知前端
```

## 8. 前端读取流程

```text
1. ProjectProxy 订阅 render mail
2. 收到 SlotCreated
3. PDO client 绑定 slot
4. 创建 WebGL object
5. 每帧检查 DataVersion
6. 读取变化并更新渲染对象
7. 收到 SlotReleased 后释放对象
```

## 9. H5 与未来 UI 的关系

当前 H5 使用 WebGL/Three.js。

未来 WPF/QT 前端可以复用：

- Mail 命令。
- PDO slot 通知。
- RenderData 契约。

只需要替换渲染实现。

## 10. 测试点

- 导入模型后前端收到工件 slot。
- 删除对象后前端收到 release。
- DataVersion 不变时前端不重复解析大数据。
- PDO 整理碎片后前端 slot 映射正确。
- 仿真时只更新高频状态 slot，不重复传大 mesh。
