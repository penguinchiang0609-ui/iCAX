# GeometryAlgo 规格文档

## 1. 定位

`GeometryAlgo` 是 iCAX Engine Foundation 层的几何与数学函数库。

它不定义数据结构，不拥有几何数据，只对 `GeometryData` 中的数据结构提供计算、校验和轻量处理函数。

## 2. 设计目标

- 承接旧 `Math` 中有价值的函数能力。
- 对 `GeometryData` 提供基础计算函数。
- 对 BRep 数据提供轻量引用完整性校验。
- 对简单曲线提供直接求值。
- 对复杂曲线、曲面、BRep 的高阶算法通过 `GeometryAdapter` 下沉到外部几何内核。

## 3. 非目标

- 不定义 `Point3`、`Vector3`、`Transform3` 等数据类型。
- 不保存几何对象状态。
- 不提供 OCC/CGAL 具体实现。
- 不提供完整 CAD 内核能力。
- 不直接处理文件格式。

## 4. 公开能力

### 4.1 基础有效性检查

- `IsValid(Point2)`
- `IsValid(Point3)`
- `IsValid(Vector2)`
- `IsValid(Vector3)`
- `IsValid(Quaternion)`

当前有效性检查主要判断数值是否为 finite。

### 4.2 向量函数

- `ToVector`
- `Translate`
- `Dot`
- `Cross`
- `LengthSquared`
- `Length`
- `TryNormalize`
- `Reversed`

### 4.3 距离函数

- `DistanceSquared(Point2, Point2)`
- `DistanceSquared(Point3, Point3)`
- `Distance(Point2, Point2)`
- `Distance(Point3, Point3)`

### 4.4 变换函数

- `MakeTranslation2`
- `MakeTranslation3`
- `MakeScale2`
- `MakeScale3`
- `MakeRotation2`
- `TryMakeRotation3`
- `Multiply(Transform2, Transform2)`
- `Multiply(Transform3, Transform3)`
- `Apply(Transform2, Point2)`
- `Apply(Transform3, Point3)`
- `Apply(Transform2, Vector2)`
- `Apply(Transform3, Vector3)`
- `Apply(Transform2, Direction2)`
- `Apply(Transform3, Direction3)`

`Transform2` 与 `Transform3` 的矩阵约定与 `GeometryData` 保持一致。

### 4.5 四元数函数

- `MakeQuaternionFromAxisAngle`
- `MakeQuaternionFromEulerZYX`
- `Multiply(Quaternion, Quaternion)`
- `Conjugate`
- `MagnitudeSquared`
- `Magnitude`
- `TryNormalize`
- `TryInverse`
- `Apply(Quaternion, Vector3)`
- `Apply(Quaternion, Direction3)`
- `Slerp`

### 4.6 平面函数

- `SignedDistance`
- `Project`
- `IsOn`

### 4.7 包围盒函数

- `Center`
- `Contains`
- `Extend`
- `Union`

### 4.8 曲线求值

直接支持：

- `Line2` / `Line3`
- `Ray2` / `Ray3`
- `Segment2` / `Segment3`
- `Circle2` / `Circle3`
- `Arc2` / `Arc3`
- `Polyline3` 的轻量采样

复杂曲线如 `BSpline3`、`NURBS3` 需要传入 `GeometryAdapter::IGeometryKernelAdapter`。

### 4.9 数据校验

- `Validate(BSpline2)`
- `Validate(BSpline3)`
- `Validate(NURBS2)`
- `Validate(NURBS3)`
- `Validate(BSplineSurface3)`
- `Validate(BRepModel)`

`Validate(BRepModel)` 当前做引用完整性和基本结构校验，不做几何内核级合法性判断。

## 5. 错误返回

涉及失败场景的函数使用：

```cpp
bool result = Function(..., std::string& error);
```

失败时：

- 返回 `false`。
- `error` 写入失败原因。

成功时：

- 返回 `true`。
- `error.clear()`。

## 6. 使用样例

### 6.1 计算距离

```cpp
iCAX::GeometryData::Point3 a { 0.0, 0.0, 0.0 };
iCAX::GeometryData::Point3 b { 3.0, 4.0, 0.0 };

double distance = iCAX::GeometryAlgo::Distance(a, b);
```

### 6.2 归一化方向

```cpp
iCAX::GeometryData::Direction3 direction;
std::string error;

bool ok = iCAX::GeometryAlgo::TryNormalize(
    iCAX::GeometryData::Vector3 { 0.0, 0.0, 10.0 },
    direction,
    error);
```

### 6.3 校验 BRep 引用

```cpp
iCAX::GeometryData::BRepModel model;
std::string error;

bool ok = iCAX::GeometryAlgo::Validate(model, error);
```

### 6.4 复杂曲线求值

```cpp
iCAX::GeometryData::Curve3 curve = iCAX::GeometryData::BSpline3 {};
iCAX::GeometryData::Point3 point;
std::string error;

bool ok = iCAX::GeometryAlgo::TryEvaluate(
    curve,
    0.5,
    point,
    error,
    geometryKernelAdapter);
```

