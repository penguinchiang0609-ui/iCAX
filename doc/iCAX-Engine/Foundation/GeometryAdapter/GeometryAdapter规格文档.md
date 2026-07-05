# GeometryAdapter 规格文档

## 1. 定位

`GeometryAdapter` 是 iCAX Engine Foundation 层的外部几何内核适配接口库。

它定义稳定接口，用于把 `GeometryData` 转换到 OCC、CGAL 或其他几何内核，再把计算结果转换回 `GeometryData`。

## 2. 设计目标

- 隔离外部几何内核。
- 不让 `GeometryData` 直接依赖 OCC/CGAL。
- 为 `GeometryAlgo` 提供复杂几何能力下沉入口。
- 支持后续多个 adapter 并存。

## 3. 非目标

- 不实现 OCC adapter。
- 不实现 CGAL adapter。
- 不保存几何数据。
- 不管理资源池。
- 不处理文件导入导出。
- 不决定产品使用哪个内核。

## 4. 核心接口

核心接口为：

```cpp
iCAX::GeometryAdapter::IGeometryKernelAdapter
```

当前接口包含：

- `TryEvaluate(BSpline3, parameter, Point3&, error)`
- `TryEvaluate(NURBS3, parameter, Point3&, error)`
- `TryTessellate(Curve3, tolerance, vector<Point3>&, error)`
- `TryTessellate(Surface3, tolerance, Triangulation3&, error)`
- `TryTessellate(BRepModel, tolerance, Triangulation3&, error)`

## 5. 接口语义

### 5.1 TryEvaluate

用于复杂曲线求值。

输入：

- `BSpline3` 或 `NURBS3`
- 参数值

输出：

- `Point3`
- 错误信息

### 5.2 TryTessellate(Curve3)

用于曲线离散。

输入：

- `Curve3`
- 容差

输出：

- 三维采样点列表

### 5.3 TryTessellate(Surface3)

用于曲面离散。

输入：

- `Surface3`
- 容差

输出：

- `Triangulation3`

### 5.4 TryTessellate(BRepModel)

用于 BRep 模型离散。

输入：

- `BRepModel`
- 容差

输出：

- `Triangulation3`

输出 mesh 可携带 `TriangleFaceIds`，用于保留三角面与 BRep face 的对应关系。

## 6. 错误返回

所有接口返回：

```cpp
bool
```

失败时：

- 返回 `false`。
- `error` 写入原因。

成功时：

- 返回 `true`。
- 输出参数写入结果。

## 7. 使用样例

```cpp
class COccGeometryAdapter final
    : public iCAX::GeometryAdapter::IGeometryKernelAdapter
{
public:
    bool TryEvaluate(
        const iCAX::GeometryData::BSpline3& curve,
        double parameter,
        iCAX::GeometryData::Point3& point,
        std::string& error) const override;

    bool TryEvaluate(
        const iCAX::GeometryData::NURBS3& curve,
        double parameter,
        iCAX::GeometryData::Point3& point,
        std::string& error) const override;

    bool TryTessellate(
        const iCAX::GeometryData::Curve3& curve,
        double tolerance,
        std::vector<iCAX::GeometryData::Point3>& points,
        std::string& error) const override;

    bool TryTessellate(
        const iCAX::GeometryData::Surface3& surface,
        double tolerance,
        iCAX::GeometryData::Triangulation3& triangulation,
        std::string& error) const override;

    bool TryTessellate(
        const iCAX::GeometryData::BRepModel& model,
        double tolerance,
        iCAX::GeometryData::Triangulation3& triangulation,
        std::string& error) const override;
};
```

