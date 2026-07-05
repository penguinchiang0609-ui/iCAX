# foundation

`foundation` 放置 icax 后台平台最底层的基础能力。这里的代码不应该依赖 `framework`、`services`、业务应用扩展或前端代码。

## 目录结构

- `Data/`：基础数据结构、变体、数组、通用函数等。
- `GeometryData/`：点、向量、坐标系、变换、曲线、曲面、区域等纯几何数据结构。
- `GeometryAdapter/`：外部几何内核适配接口，用于桥接 OCC、CGAL 等库。
- `GeometryAlgo/`：几何与数学计算函数，输入输出都使用 `GeometryData`。
- `Task/`：接近 C# `Task` 调用体验的 C++ 异步任务基础能力。
