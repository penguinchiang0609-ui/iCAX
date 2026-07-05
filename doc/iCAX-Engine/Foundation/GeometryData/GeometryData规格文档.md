# GeometryData 规格文档

## 1. 定位

`GeometryData` 是 iCAX Engine Foundation 层的几何数据契约库。

它只定义数据结构，不提供几何计算、校验、求值、离散、相交、布尔、投影、拓扑修复或内核调用能力。

上层业务、Component、Resource、文件导入导出、渲染数据生成、碰撞数据生成都可以直接依赖 `GeometryData` 表达几何数据。

## 2. 设计目标

- 提供与几何内核无关的中立数据结构。
- 支持二维/三维点、向量、方向、坐标系、矩阵、变换、四元数。
- 支持常见解析曲线和自由曲线。
- 支持常见解析曲面和自由曲面。
- 支持中立 BRep 表达，能承接 STEP/IGES 导入后的精确几何与拓扑结构。
- 支持三角网格表达，能承接 STL、CGAL polygon mesh、OCC triangulation 或前端渲染网格。
- 不依赖 OCC、CGAL 或其他外部几何内核。
- 数据结构不包含方法，所有行为放到 `GeometryAlgo` 或 `GeometryAdapter`。

## 3. 非目标

- 不提供 `TopoDS_Shape`、`TopoDS_Face` 等 OCC 类型。
- 不提供 CGAL mesh 类型。
- 不提供几何对象继承体系。
- 不提供虚函数、多态行为、Clone、Evaluate、Reverse、Length 等方法。
- 不保证数据本身一定几何合法；合法性检查由 `GeometryAlgo` 或外部内核完成。
- 不直接处理文件格式、持久化、schema migration 或资源池。

## 4. 基础数据

### 4.1 标量容器

- `Point2` / `Point3`：二维/三维点。
- `Vector2` / `Vector3`：二维/三维向量。
- `Direction2` / `Direction3`：二维/三维方向，调用者负责保证单位化。
- `TextureCoordinate2`：二维纹理坐标。
- `Matrix3x3` / `Matrix4x4`：行主序矩阵。
- `Transform2` / `Transform3`：二维/三维齐次变换。
- `Quaternion`：四元数数据。

### 4.2 坐标与空间表达

- `Axis2` / `Axis3`：位置 + 方向。
- `Placement2` / `Placement3`：局部坐标系。
- `Plane3`：平面位置、法向、参考 X 方向。
- `BoundingBox2` / `BoundingBox3`：轴对齐包围盒。
- `OrientedBox2` / `OrientedBox3`：有向包围盒。

`Transform2` 使用 3x3 行主序矩阵，`Transform3` 使用 4x4 行主序矩阵。点按列向量应用，平移分量位于最后一列。

## 5. 曲线数据

支持的二维/三维曲线包括：

- `Line2` / `Line3`
- `Ray2` / `Ray3`
- `Segment2` / `Segment3`
- `Circle2` / `Circle3`
- `Arc2` / `Arc3`
- `Ellipse2` / `Ellipse3`
- `EllipseArc2` / `EllipseArc3`
- `Polyline2` / `Polyline3`
- `Bezier2` / `Bezier3`
- `BSpline2` / `BSpline3`
- `NURBS2` / `NURBS3`
- `Clothoid2` / `Clothoid3`

`Curve2` 与 `Curve3` 使用 `std::variant` 聚合具体曲线类型。

`BSpline` 与 `NURBS` 采用接近 OCC 的 pole、knot、multiplicity、weight 表达方式。

## 6. 曲面数据

支持的三维曲面包括：

- `PlaneSurface3`
- `CylindricalSurface3`
- `ConicalSurface3`
- `SphericalSurface3`
- `ToroidalSurface3`
- `BSplineSurface3`

`Surface3` 使用 `std::variant` 聚合具体曲面类型。

`BSplineSurface3` 保存 U/V 两个方向的 degree、pole grid、weight、knot、multiplicity 和 periodic 标记。

## 7. BRep 数据

`BRepModel` 使用几何表和拓扑表分离的方式表达边界表示模型。

几何表：

- `Curve2Record`
- `Curve3Record`
- `Surface3Record`
- `Triangulation3Record`

拓扑表：

- `BRepVertex`
- `BRepEdge`
- `BRepCoedge`
- `BRepWire`
- `BRepFace`
- `BRepShell`
- `BRepSolid`
- `BRepCompSolid`
- `BRepCompound`

### 7.1 BRepFace

`BRepFace` 由以下信息组成：

- `Surface3Id`：精确曲面引用。
- `WireIds`：边界 wire 引用。
- `Domain`：曲面参数域范围。
- `Orientation`：拓扑方向。
- `Triangulation3Id`：可选三角剖分引用。
- `Tolerance`：容差。
- `NaturalRestriction`：是否自然边界限制。

### 7.2 BRepCoedge

`BRepCoedge` 表示 wire 内的一条有向边，包含：

- `EdgeId`：三维边引用。
- `Curve2Id`：面参数域 p-curve 引用，可选。
- `StartVertexId` / `EndVertexId`：coedge 方向下的起止顶点，可选。
- `Range`：参数范围。
- `Orientation`：拓扑方向。

退化边可以没有三维曲线，但对应 coedge 必须提供 p-curve。

### 7.3 BRepShapeRef

`BRepShapeRef` 表达形体引用：

- `Kind`：Vertex、Edge、Wire、Face、Shell、Solid、CompSolid、Compound。
- `Id`：目标形体 Id。
- `Orientation`：引用方向。
- `Location`：形体级变换。

该结构用于对应 OCC `TopoDS_Shape` 的 orientation/location，也用于表达 STEP 装配实例或 compound 层级。

## 8. 三角网格数据

`Triangulation3` 支持：

- `Vertices`
- `Normals`
- `TextureCoordinates`
- `Triangles`
- `TriangleFaceIds`

它既可表达渲染网格，也可表达 CGAL polygon mesh 或 OCC face triangulation 的轻量结果。

## 9. 元数据

`EntityMetadata` 用于保存：

- `Name`
- `SourceId`
- `Tags`

它用于承接 STEP/IGES 文件中的名称、原始标识、分类标签等非几何信息。

## 10. 使用样例

### 10.1 定义三维线段

```cpp
iCAX::GeometryData::Segment3 segment;
segment.Start = { 0.0, 0.0, 0.0 };
segment.End = { 10.0, 0.0, 0.0 };
```

### 10.2 定义三阶 B 样条

```cpp
iCAX::GeometryData::BSpline3 curve;
curve.Degree = 3;
curve.Poles = {
    { 0.0, 0.0, 0.0 },
    { 10.0, 0.0, 0.0 },
    { 20.0, 10.0, 0.0 },
    { 30.0, 10.0, 0.0 }
};
curve.Knots = { 0.0, 1.0 };
curve.Multiplicities = { 4, 4 };
curve.Periodic = false;
```

### 10.3 定义一个平面 Face

```cpp
iCAX::GeometryData::BRepModel model;

model.Surfaces3.push_back({
    1,
    iCAX::GeometryData::PlaneSurface3 {},
    { "top face", "step-face-42", {} }
});

model.Faces.push_back({
    .Id = 10,
    .Surface3Id = 1,
    .WireIds = {},
    .Orientation = iCAX::GeometryData::ETopologyOrientation::Forward
});

model.RootShapes.push_back({
    iCAX::GeometryData::EBRepShapeKind::Face,
    10,
    iCAX::GeometryData::ETopologyOrientation::Forward,
    {}
});
```

## 11. 与其他模块关系

- `GeometryAlgo`：使用本项目数据进行计算、校验、求值。
- `GeometryAdapter`：将本项目数据转换到 OCC、CGAL 等外部内核，并转换回本项目数据。
- 产品插件：直接依赖本项目表达曲线、曲面、BRep、mesh。

