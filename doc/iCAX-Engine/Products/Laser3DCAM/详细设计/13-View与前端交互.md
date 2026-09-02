# 13 View 与前端交互详细设计

## 1. 目标

前端是可交互的 CAD 工作台，不是后端渲染结果的播放器。Scene 保存正式业务模型，View 提供动态读模型，资源池提供版本化几何和材质。

## 2. 数据通道

- `View.GetOrCreate/Release` 通过 Scene 邮件通道调用，View 不建立独立 Context 或邮件通道。
- 一个 View 可以组合多个 EntityView Source；同一实体在快照中只出现一次。
- View 快照只保存投影属性和资源 URL，前端按 URL 从 Scene ResourceLibrary 读取数据并自行渲染。
- Entity 新增或移除满足条件的 Component 时，EntityView membership 自动变化，View 发布新版本。

## 3. 交互通道

- 相机旋转、平移、缩放、悬停、框选和拖拽过程完全在前端执行。
- 前端允许维护临时相机、选择、ghost mesh 和 transform preview，这些状态不写入项目主数据。
- 正式修改必须提交语义化 SDO，例如 `Machine.SetElementTransform` 或 `CADIntent.SetParameters`。
- 后端校验并修改 Component/CSG 后，View 新版本返回正式结果，替换前端预览。
- 一次拖拽只在结束时形成一次正式提交和一条 Undo 记录，不传输鼠标轨迹。

## 4. PDO 边界

InputPDO、TransformPDO 和后端相机导航不属于产品交互链路。PDOHub 仍可选地承载 Collider PDO 等可丢帧、高吞吐的计算结果，但不得承载场景渲染主数据，也不得绕过 View。

## 5. 失败处理

- SDO 被后端拒绝时，前端撤销临时预览并显示校验错误。
- View 或资源暂不可用时保留上一正式版本，不把临时预览提升为正式状态。
- 新旧请求竞态使用本地 sequence/revision 丢弃过期结果。
