# RenderData 规格文档

## 定位

`RenderData` 是后端向渲染系统表达“要渲染什么”的中立数据契约。它不绑定 PDO、Three.js、WPF、Qt、OpenGL 或具体产品。

## 数据范围

- `SRenderMeshData`：三角网格、线段、点集等 mesh 类几何。
- `SRenderPolylineData`：折线和分段显示信息。
- `SRenderToolpathData`：刀路点、刀轴、进给和移动段。
- `SRenderInstanceData`：对象实例、几何引用、变换、可见性和选择状态。
- `SRenderViewStateData`：视口尺寸、相机语义和矩阵。
- `SRenderStyleData`：轻量颜色、线宽和显示标记。

## 版本要求

`RenderDataVersion` 使用 `uint64_t`。产品层应优先使用 Component/Resource 的版本号作为 RenderData 版本，用于 PDO 或 GPU buffer 层跳过未变化数据。

## 使用样例

```cpp
iCAX::Render::SRenderMeshData mesh;
mesh.nGeometryID = 1001;
mesh.nDataVersion = component.Version();
mesh.Positions = { {0, 0, 0}, {1, 0, 0}, {0, 1, 0} };

iCAX::Render::SRenderInstanceData instance;
instance.nObjectID = entity.ID();
instance.nGeometryID = mesh.nGeometryID;
instance.eGeometryKind = iCAX::Render::ERenderGeometryKind::Mesh;
```
