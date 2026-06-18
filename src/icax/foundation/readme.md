# foundation

`foundation` 放置 icax 后台平台最底层的基础能力。这里的代码不应该依赖 `framework`、`services`、产品扩展或前端代码。

## 目录结构

- `Data/`：基础数据结构、变体、数组、通用函数等。
- `Math/`：向量、点、矩阵、变换、四元数等数学类型。
- `Geometry/`：二维/三维曲线、圆弧、边界曲线等几何类型。
- `Task/`：接近 C# `Task` 调用体验的 C++ 异步任务基础能力。
