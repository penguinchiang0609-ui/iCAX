# GeometryAdapter 方案文档

## 1. 总体方案

`GeometryAdapter` 是一层接口，不是具体内核实现。

```text
GeometryAlgo
    |
    v
IGeometryKernelAdapter
    |
    +-- OccGeometryAdapter
    +-- CgalGeometryAdapter
    +-- OtherGeometryAdapter
```

Foundation 层只提供 `IGeometryKernelAdapter`。

具体 OCC/CGAL 实现应放在插件或文件模块中，不放进 Foundation。

## 2. 为什么需要 Adapter

`GeometryData` 是中立数据契约，但复杂几何计算需要成熟内核。

例如：

- B 样条/NURBS 精确求值。
- 曲线曲面离散。
- STEP/IGES BRep 转换。
- face triangulation。
- 投影、相交、布尔、offset、sweep。

如果让 `GeometryAlgo` 直接依赖 OCC 或 CGAL，Foundation 会被第三方内核绑定。

所以需要 Adapter：

- `GeometryAlgo` 只知道接口。
- 具体产品或文件模块决定加载哪个实现。
- 多内核可以并存。

## 3. OCC Adapter 思路

OCC adapter 负责双向转换：

```text
GeometryData::BRepModel
        <---->
TopoDS_Shape
```

### 3.1 GeometryData 到 OCC

转换步骤：

1. 转换基础几何：Point、Vector、Direction、Placement。
2. 转换 Curve2/Curve3 到 `Geom2d_Curve` / `Geom_Curve`。
3. 转换 Surface3 到 `Geom_Surface`。
4. 创建 Vertex、Edge、Wire、Face、Shell、Solid。
5. 应用 orientation 和 location。
6. 对退化边、p-curve、same-parameter、same-range 做 OCC 对应设置。

### 3.2 OCC 到 GeometryData

转换步骤：

1. 遍历 `TopoDS_Shape`。
2. 将共享 geometry 放入几何表。
3. 将 vertex/edge/wire/face/shell/solid/compound 放入拓扑表。
4. 保存 orientation/location 到 `BRepShapeRef`。
5. 保存 p-curve 到 `Curve2Record`。
6. 保存 face triangulation 到 `Triangulation3Record`。
7. 保存名称、source id 等到 `EntityMetadata`。

## 4. CGAL Adapter 思路

CGAL adapter 主要面对 mesh 场景：

```text
GeometryData::Triangulation3
        <---->
CGAL Surface_mesh / Polygon_mesh
```

适合能力：

- polygon mesh 构建。
- mesh repair。
- AABB tree。
- mesh distance。
- mesh intersection。
- mesh simplification。

CGAL 对精确 BRep 的承接能力不应作为主路线。精确 CAD BRep 优先通过 OCC adapter。

## 5. Adapter 生命周期

`GeometryAdapter` 不规定 adapter 的创建与持有方式。

推荐策略：

- 产品或文件模块持有具体 adapter。
- 需要计算时传给 `GeometryAlgo`。
- 不在 `GeometryData` 中保存 adapter。
- 不使用全局单例绑定具体内核。

## 6. 错误处理

adapter 接口不抛业务错误，使用：

```cpp
bool TryXxx(..., std::string& error);
```

具体实现内部如果遇到内核异常，可以捕获后转换成 `error`。

严重编程错误可以在实现内部断言或让错误暴露在第一现场。

## 7. 扩展方向

后续可以增加接口：

- `TryValidate(BRepModel)`
- `TryProject(Point3, Surface3)`
- `TryIntersect(Curve3, Surface3)`
- `TryOffset(Curve3)`
- `TryBoolean(BRepModel, BRepModel)`
- `TryImportStep`
- `TryExportStep`

这些接口不应一次性塞入当前最小接口，应该随产品真实需求逐步加入。

