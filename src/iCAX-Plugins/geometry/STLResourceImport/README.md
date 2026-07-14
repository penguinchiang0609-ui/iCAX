# STLResourceImport

`STLResourceImport` 是通用几何资源导入插件，负责把 `.stl` 文件导入为 `GeometryData::CTriangleMeshResource`。

## 目录结构

- `STLResourceImporter.cpp`：STL importer 实现，支持 binary STL 和 ASCII STL。
- `framework.h`、`pch.h`、`pch.cpp`、`dllmain.cpp`：Visual C++ 动态库工程基础文件。

## 设计边界

- 插件只解析 STL 文件并写入 `Scene.Resources`，不依赖 CAM 产品代码。
- 输出资源类型为 `geometry.triangle_mesh`，格式 ID 为 `geometry.stl`。
- 导入时同时保存原始 `CBinaryResource` 和中立三角网格资源。
- STL 顶点不做去重，保持 STL facet 的硬边语义；几何清理、焊接、简化等算法应放到 `GeometryAlgo`。
