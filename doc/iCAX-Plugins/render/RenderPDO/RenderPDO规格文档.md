# RenderPDO 规格文档

## 1. 定位

`RenderPDO` 解决的问题是：后端如何用 PDO 把渲染数据告诉前方。

```text
C++ 后端
  -> PDO shared memory
    -> 前方 viewport / UI 自己渲染
```

它不是 renderer，不设计 shader/material/pipeline，也不抽象多前端渲染实现。

## 2. 核心原则

- 只定义 PDO payload 的固定二进制 layout。
- payload 必须是 standard-layout、trivially-copyable。
- 每个 payload 通过 `SRenderPDOHeader` 携带 magic、layout version、payload kind、payload size 和 data version。
- 变长数据使用同一块 payload 内的 offset 定位。
- `dataVersion` 使用 PDO 槽的数据版本语义，用于前方判断是否需要重新读取。
- 不依赖 `Resources`，不使用 `ResourcePool` key。
- 不包含 material、shader、GPU buffer 或 renderer。

## 3. Payload 类型

当前定义：

- `Mesh`：三角网格或其他拓扑几何。
- `Polyline`：折线、轮廓线、辅助线。
- `Toolpath`：刀路显示点列和段语义。
- `InstanceList`：对象实例列表，表达 geometry、transform、visible/selectable 等显示状态。
- `ViewState`：视口尺寸、相机语义和 view/projection matrix。

## 4. Mesh Payload

`SRenderMeshPDOHeader` 定义：

- `geometryId`
- `bounds`
- `topology`
- `vertexCount`
- `indexCount`
- `positionsOffset`
- `normalsOffset`
- `vertexColorsOffset`
- `indicesOffset`

mesh payload 不表达材质。前方可以根据 instance 的 `displayClass`、`styleId` 或自己的 UI 状态决定颜色和显示方式。

## 5. Polyline Payload

`SRenderPolylinePDOHeader` 定义：

- `geometryId`
- `bounds`
- `pointCount`
- `rangeCount`
- `pointsOffset`
- `rangesOffset`

Polyline 用于轮廓线、辅助线、二维/三维折线和轻量显示曲线。

## 6. Toolpath Payload

`SRenderToolpathPDOHeader` 定义：

- `geometryId`
- `bounds`
- `pointCount`
- `spanCount`
- `pointsOffset`
- `spansOffset`

`SRenderToolpathSpanData` 中包含 `moveKind`，用于区分快速移动、切削、连接、引入、引出等显示语义。

## 7. InstanceList Payload

`SRenderInstanceListPDOHeader` 定义：

- `instanceCount`
- `styleCount`
- `instancesOffset`
- `stylesOffset`

`SRenderInstanceData` 定义：

- `objectId`
- `geometryId`
- `geometryKind`
- `displayClass`
- `styleId`
- `flags`
- `transform`

这里的 `SRenderStyleData` 是轻量渲染提示，不是 material。前方可以使用，也可以忽略。

## 8. ViewState Payload

`SRenderViewStatePDOHeader` 定义：

- `viewCount`
- `activeViewIndex`
- `viewsOffset`

`SRenderViewStateData` 定义：

- `viewId`
- `width`
- `height`
- `dpiScale`
- `flags`
- `eye`
- `target`
- `up`
- `nearPlane`
- `farPlane`
- `verticalFovRadians`
- `orthographicHeight`
- `viewMatrix`
- `projectionMatrix`

`ViewState` 不绑定 H5、WPF、QT 或某个 renderer。它只是前端和后端共享的视图状态数据：

- 前端写给后端：用于拾取、LOD、动态简化、视图相关计算。
- 后端写给前端：用于打开项目后的默认视角、仿真视角同步、回放视角同步。

## 9. 常用 PDO 命名

建议产品使用稳定的 `typeName + instanceName`：

- `render.mesh / MainViewport.Model`
- `render.polyline / MainViewport.Contour`
- `render.toolpath / MainViewport.Toolpath`
- `render.instance_list / MainViewport.Scene`
- `render.view_state / MainViewport.View`

这里的 `MainViewport` 只是实例名约定，不是 framework 概念。

## 10. 边界

不属于 `RenderPDO`：

- material。
- shader。
- pipeline state。
- GPU resource。
- renderer backend。
- picking/selection 算法。
- collision。
- resource loading。

这些应该由其他插件或前端自己实现。

## 11. 验收要求

- 能独立构建 `RenderPDO.vcxproj`。
- 能构建并通过 `RenderPDOTest.vcxproj`。
- layout 结构满足 standard-layout 和 trivially-copyable。
- 能生成 PDO 声明。
- 能校验 payload magic、version、kind、size、dataVersion、offset range 和 view active index。
- 不依赖 `Resources`。
