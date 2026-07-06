# foundation

`foundation` 放置 icax 后台平台最底层的基础能力。这里的代码不应该依赖 `framework`、`services`、业务应用扩展或前端代码。

## 目录结构

- `Data/`：基础数据结构、变体、数组、通用函数等。
- `FontData/`：字体面、字形、字形度量、文本 run、shaping 输出等纯字体数据结构。
- `FontAdapter/`：外部字体内核适配接口，用于桥接 FreeType、HarfBuzz、DirectWrite 等库。
- `FontAlgo/`：字体算法函数，输入输出都使用 `FontData`。
- `GeometryData/`：点、向量、坐标系、变换、曲线、曲面、区域等纯几何数据结构。
- `GeometryAdapter/`：外部几何内核适配接口，用于桥接 OCC、CGAL 等库。
- `GeometryAlgo/`：几何与数学计算函数，输入输出都使用 `GeometryData`。
- `ImageData/`：位图、RGBA 图片、矢量图、颜色空间、alpha 语义等纯图片数据结构。
- `ImageAdapter/`：外部图像算法库适配接口，用于桥接 OpenCV、Skia、FreeImage、libvips 等库。
- `ImageAlgo/`：图片算法函数，输入输出都使用 `ImageData`。
- `Task/`：接近 C# `Task` 调用体验的 C++ 异步任务基础能力。
