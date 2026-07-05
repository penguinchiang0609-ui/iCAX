# GeometryAlgo 方案文档

## 1. 总体方案

`GeometryAlgo` 采用自由函数方案。

它只依赖：

- `GeometryData`
- `GeometryAdapter`
- C++ 标准库

它不保存状态，不定义对象继承体系，不拥有任何几何资源。

```text
上层业务 / Service / Behaviour
        |
        v
GeometryAlgo
        |
        +-- 简单算法：直接实现
        |
        +-- 复杂算法：通过 GeometryAdapter 下沉
```

## 2. 模块职责

### 2.1 简单算法

简单算法在 `GeometryAlgo` 中直接实现：

- 向量点积、叉积、长度、归一化。
- 点距离。
- 矩阵变换。
- 四元数基础运算。
- 平面距离与投影。
- 包围盒合并与包含。
- 直线、线段、圆、圆弧求值。

这些算法不需要外部几何内核，直接实现更轻量。

### 2.2 复杂算法

复杂算法不在 `GeometryAlgo` 中硬写：

- B 样条/NURBS 精确求值。
- 曲线离散。
- 曲面离散。
- BRep triangulation。
- 曲面相交。
- 曲线曲面投影。
- BRep 有效性强校验。
- Boolean、offset、fillet、sweep 等 CAD 内核能力。

这些能力通过 `GeometryAdapter::IGeometryKernelAdapter` 调用 OCC、CGAL 或其他内核。

## 3. 为什么函数并入 GeometryAlgo

旧 `Math` 中的 `Point3.Distance()`、`Vector3.Normalize()`、`Transform3.Apply()` 等方法会让数据结构重新带上行为。

新的方案是：

- `GeometryData` 保存字段。
- `GeometryAlgo` 保存函数。

这样可以避免：

- 数据对象膨胀。
- 多套 Point/Vector 类型并存。
- 上层数据依赖算法实现。
- OCC/CGAL 适配污染基础数据。

## 4. BRep 校验方案

`Validate(BRepModel)` 做轻量校验：

- Id 非 0。
- 同类 Id 不重复。
- edge 引用的 vertex 存在。
- 非退化 edge 必须引用 3D curve。
- 退化 edge 的 coedge 必须引用 p-curve。
- wire 的 coedge 引用 edge 存在。
- face 引用 surface 和 wire 存在。
- shell 引用 face 存在。
- solid 引用 shell 存在。
- compsolid 引用 solid 存在。
- compound/root shape 引用合法。

它不做内核级校验：

- 不判断 wire 是否几何闭合。
- 不判断 p-curve 与 3D curve 是否一致。
- 不判断 face trimming 是否有效。
- 不判断 shell 是否水密。
- 不判断 solid 是否可制造。

这些应由 Adapter 调 OCC/CGAL 完成。

## 5. 错误处理方案

函数错误统一采用：

```cpp
bool TryXxx(..., std::string& error);
```

原因：

- Foundation 层保持简单。
- 便于上层在 ModifyFilter、文件导入、服务调用中携带错误信息。
- 不强制所有几何错误都用异常表达。

确定违反编程前置条件的场景，后续可以在调用侧直接断言或崩溃在第一现场。

## 6. Adapter 调用方案

以 `TryEvaluate(Curve3)` 为例：

1. 判断 variant 中的实际曲线类型。
2. 简单曲线直接求值。
3. `BSpline3` / `NURBS3` 等复杂曲线要求传入 adapter。
4. adapter 返回结果或错误。

这个模式后续可扩展到：

- `TryTessellate(Curve3)`
- `TryTessellate(Surface3)`
- `TryTessellate(BRepModel)`
- `TryIntersect`
- `TryProject`

## 7. 与产品插件关系

产品插件可以：

- 直接依赖 `GeometryData` 表达数据。
- 在需要计算时依赖 `GeometryAlgo`。
- 在需要外部内核时通过产品或文件模块提供具体 adapter。

产品插件不应该重新定义自己的 Point/Vector/BRep 基础结构。

