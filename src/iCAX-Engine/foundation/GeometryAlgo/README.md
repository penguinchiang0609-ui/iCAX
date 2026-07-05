# GeometryAlgo

`GeometryAlgo` 承载几何函数。`GeometryData` 只保存数据，所有求值、校验、离散、相交、距离、反转、变换等行为都应放在本项目或本项目调用的适配器中。

## 目录结构

- `GeometryAlgo.h`、`GeometryAlgo.cpp`：基础几何与数学函数。
- `framework.h`、`pch.h`、`pch.cpp`、`dllmain.cpp`：Visual C++ 动态库工程基础文件。

## 设计边界

- 简单函数在本项目直接实现，例如有限性检查、向量点积/叉积、归一化、距离、变换、四元数、平面投影、包围盒、直线/线段/圆/圆弧求值。
- 复杂内核能力通过 `GeometryAdapter::IGeometryKernelAdapter` 下沉到 OCC、CGAL 等外部实现。
- 本项目不拥有几何数据，只操作 `GeometryData`。
- 旧 `Math` 中有价值的函数能力并入本项目；旧的带方法数据类型不再保留在新 foundation 主线中。
