# GeometryAdapter

`GeometryAdapter` 定义外部几何内核适配接口，用于把 `GeometryData` 转换到 OCC、CGAL 或其他内核，再把计算结果转换回 `GeometryData`。

## 目录结构

- `GeometryAdapter.h`：外部几何内核适配接口。
- `framework.h`、`pch.h`、`pch.cpp`、`dllmain.cpp`：Visual C++ 动态库工程基础文件。

## 设计边界

- 本项目不保存几何数据，数据结构统一来自 `GeometryData`。
- 本项目不强制绑定某一个几何内核，只定义稳定的适配接口。
- OCC、CGAL 等具体实现后续应独立放在扩展项目中，例如 `OccGeometryAdapter`。
- 上层几何算法优先放在 `GeometryAlgo`，只有复杂内核能力才通过本接口下沉。

