# GeometryData

`GeometryData` 只定义几何数据结构，不提供几何计算、合法性判断、克隆、反转、求值、离散化等行为。

## 目录结构

- `GeometryData.h`：点、向量、方向、坐标系、矩阵、变换、四元数、平面、包围盒、曲线、曲面、BRep、区域和三角网格等纯数据结构。
- `framework.h`、`pch.h`、`pch.cpp`、`dllmain.cpp`：Visual C++ 动态库工程基础文件。

## 设计边界

- 数据结构参考 OCC 的表达习惯，例如 `Placement`、`Pole`、`Knot`、`Multiplicity`、`Weight` 等。
- 数据结构不直接依赖 OCC、CGAL 或其他几何内核。
- `BRepModel` 使用中立边界表示：`Curve/Surface` 是几何表，`Vertex/Edge/Wire/Face/Shell/Solid` 是拓扑表。它不引入 OCC `TopoDS`，也不承诺某个内核的内部拓扑布局。
- `BRepFace` 由一个 `Surface3` 引用和若干 `Wire` 边界组成；`BRepCoedge` 可携带面参数域上的 `Curve2` 引用，用于表达 p-curve。
- `BRepShapeRef` 表达形体引用、方向和形体级变换，可对应 OCC `TopoDS_Shape` 的 orientation/location，也可用于 STEP 装配实例。
- `BRepEdge` 支持退化边、same-parameter、same-range；退化边可以没有三维曲线，但对应的 `BRepCoedge` 必须有二维 p-curve。
- `Triangulation3` 支持三角形、法线、UV 和 triangle-to-face 映射，可承接 CGAL polygon mesh 或 OCC face triangulation 的轻量结果。
- `Transform2` 使用 3x3 行主序矩阵，`Transform3` 使用 4x4 行主序矩阵；点按列向量应用，平移分量位于最后一列。
- 几何求值、离散、校验、反转、长度、相交等行为放到 `GeometryAlgo`。
- 调用 OCC、CGAL 等外部内核的桥接逻辑放到 `GeometryAdapter`。
- 旧 `Math::Point3`、`Math::Vector3`、`Math::Transform3` 这类对象模型不再作为上层数据契约使用。
