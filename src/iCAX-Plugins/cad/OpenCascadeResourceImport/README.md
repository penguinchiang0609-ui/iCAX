# OpenCascadeResourceImport

本目录提供基于 Open CASCADE Technology 的通用资源导入插件。

## 职责

- 注册 `IResourceImporter`，为当前产品的 `ResourceLibrary` 增加 STEP/STP、IGS/IGES 导入能力。
- 将外部 CAD 文件导入为通用资源：
  - `source`：`iCAX::Resource::CBinaryResource`，保存原始文件内容。
  - `geometry.brep`：`iCAX::GeometryData::BRepModel`，保存中立 BRep 数据。
- 不创建产品 Entity，不写 CAM 刀路或拓扑索引，不暴露 OCC 类型。

## 扩展方式

新增资源格式时，新建插件并实现 `IResourceImporter` 或 `IResourceExporter`，再通过 `ICAX_REGISTER_RESOURCE_IMPORTER_PROVIDER` / `ICAX_REGISTER_RESOURCE_EXPORTER_PROVIDER` 注册稳定 provider ID。本插件 provider ID 为 `occ.opencascade`。

产品 manifest 通过 `backend.resources.handlers` 选择资源类型、格式、扩展名和 provider。例如 STEP 导入到中立 BRep：

```json
{
  "kind": "importer",
  "resourceType": "geometry.brep",
  "formatId": "cad.step",
  "extensions": [".step", ".stp"],
  "module": "../../iCAX-Plugins/cad/OpenCascadeResourceImport/${Platform}/${Configuration}/OpenCascadeResourceImport.dll",
  "provider": "occ.opencascade",
  "priority": 100
}
```

运行期由 `ProductRuntime` 加载 DLL，再按模块路径把注册回放到当前产品和 Scene 的资源注册表。产品代码只需要调用 `scene.Resources().Import<iCAX::GeometryData::BRepModel>(path)` 或传入 `CResourceImportRequest`。

## 中性模型二维路径

`OpenCascadeNeutralModelEvaluator` 的 `profile2d` 接收统一的 `contours` 数组。第一项是外轮廓，后续项都是内孔轮廓；C++ 直接用这些闭环构造带孔平面，`extrude` 一次生成最终拉伸体，不需要用实体布尔运算制造管壁。轮廓表达与产品、模板和管型无关：

```json
{
  "placement": {
    "origin": [0, 0, 0],
    "xAxis": [1, 0, 0],
    "yAxis": [0, 1, 0]
  },
  "contours": [
    {
      "kind": "path",
      "segments": [
        { "kind": "line", "start": [-40, -30], "end": [40, -30] },
        { "kind": "arc", "start": [40, -30], "middle": [50, 0], "end": [40, 30] },
        { "kind": "line", "start": [40, 30], "end": [-40, 30] },
        {
          "kind": "ellipseArc",
          "center": [-40, 0],
          "majorRadius": 30,
          "minorRadius": 10,
          "rotation": 1.5707963267948966,
          "startAngle": 0,
          "endAngle": 3.141592653589793
        }
      ]
    },
    { "kind": "circle", "radius": 10 }
  ]
}
```

所有角度和样条参数均使用弧度。`ellipseArc` 的 `majorRadius` 必须不小于 `minorRadius`，`rotation` 是长轴相对局部 X 轴的旋转角。

贝塞尔曲线段使用二维控制点：

```json
{
  "kind": "bezier",
  "controlPoints": [[0, 0], [10, 20], [30, 20], [40, 0]]
}
```

B 样条和 NURBS 使用相同的次数、控制点、节点及周期结构；NURBS 额外要求每个控制点具有正权重：

```json
{
  "kind": "bspline",
  "degree": 3,
  "periodic": true,
  "controlPoints": [[0, -35], [20, -20], [35, 0], [20, 20], [0, 35]],
  "knots": [0, 1, 2, 3, 4, 5],
  "multiplicities": [1, 1, 1, 1, 1, 1]
}
```

```json
{
  "kind": "nurbs",
  "degree": 2,
  "controlPoints": [[30, 0], [30, 30], [0, 30]],
  "weights": [1, 0.7071067811865476, 1],
  "knots": [0, 1],
  "multiplicities": [3, 3]
}
```

`knots` 可以采用两种形式：提供 `multiplicities` 时表示严格递增的唯一节点；省略 `multiplicities` 时表示允许重复值的完整节点向量。非周期样条可选用成对的 `startParameter`、`endParameter` 截取参数区间。每项轮廓都必须首尾连续并闭合。`contours` 不允许为空，也不再接受旧的单个 `contour` 字段。

## 中性模型共享实例

求解器只复用模板显式引用的节点，不推断两个独立模型是否相同。`EvaluateNeutralModel(model, roots)` 只执行指定根及实际依赖，在这一次调用内每个依赖节点只求解一次。

显示是否需要布尔运算由 Python 模板的 `display` 几何引用决定，而不是求解器中的用途判断。通用 C++ 求解器没有“显示时跳过 Boolean”的规则；如果模板把布尔结果作为显示根，求解器仍执行该布尔及其依赖。

`transform` 使用一个输入节点和 `arguments.placement`。位置由 `origin` 指定，`xAxis`、`yAxis`、`zAxis` 为局部坐标系在目标空间的方向，均为三元素数组。各方向必须有限、单位长度、互相正交且右手；不接受缩放、镜像和错切。实现保留输入 `TShape`，仅为实例增加独立 `TopLoc_Location`，所以多个实例可以共享一次完成的基础拉伸体。

孔槽等加工仍接在各实例自己的布尔节点上。Cut、Fuse、Common 均在 `Build()` 前启用非破坏模式，确保不会修改其他实例引用的基础体。该机制不等同于显示与生产共用最终模型，也不提供跨请求或跨生成批次缓存。

## 受控并行求解

生成与拆单共同使用上述求解器，默认按依赖层级分批并行执行独立的几何节点。
执行器使用 foundation `Task::Run` 和共享的专用 `ThreadPoolTaskScheduler(8)`，不再逐批创建原生线程。
专用池与默认任务池隔离，避免同步求解器从默认池内调用时因等待同池子任务而饥饿。
单次调用最多使用 `min(8, max(1, hardware_concurrency - 1))` 个计算线程；
`EvaluateNeutralModel(model, roots, options)` 可用 `MaximumConcurrency = 1` 回退到串行。
显式线程数也限制在 1–8；0 表示自动选择。

- 先检查所选子图的缺失依赖和循环，再启动计算。Boolean 的 `target/tools` 覆盖普通 `inputs` 时，以实际使用的依赖为准。
- 共享依赖仍只求解一次。并行的拉伸和布尔任务使用在主调线程上预先深拷贝的几何，避免不同任务通过共同的 TShape 修改拓扑、容差或缓存。
- Transform 和 Compound 的共享实例装配保持串行；结果字典只由主调线程写入，批内所有 Task 完成后才传播异常，不返回半成品。即使提交途中失败，也先等待已提交任务再释放其引用的数据。
- 布尔算法明确关闭内部并行，避免外层零件并行与内层线程池叠加。没有改变同一个节点内刀具的应用顺序。
- 减料前使用不依赖显示网格、包含形状容差的包围盒快筛。仅跳过明确分离的包围盒；接触或重叠仍交给精确布尔，合并和求交不使用此快捷路径。`UseBoundingBoxFilter = false` 可关闭以作对照。
- 本层不访问项目、资源库或撤销栈；TubeDesigner 的资源提交和数据库事务仍在原调用线程上完成。

`ComponentResource` 和 `TemplateRuntimeTest.NeutralModel*` 覆盖并行与串行的一致性、
共享实例、依赖选择、异常恢复以及快筛边界。
完整五面防盗窗的耗时／几何对照测试需显式运行：

```text
TubeDesignerTest.exe --gtest_filter=TemplateRuntimeTest.DISABLED_FiveFaceParallelGeometryBenchmark --gtest_also_run_disabled_tests
```

该测试对 88 个零件的显示和制造模型分别比较串行、串行快筛、8 线程快筛；
每次重新求解，没有读取已拆单项目的缓存。检查最终实体有效性、体积、包围盒及实体／面／边数量。
计时仅包含几何求解，不代表包括资源转换、入库及前端刷新在内的整次操作耗时。

## OCC 到中性 BRep 的并行转换

`ConvertOpenCascadeShapesToBRep(inputs, tolerance, maximumConcurrency)` 使用同一个专用 Task 池，
按零件并行生成中性几何、拓扑及三角网格。0 自动选择并发数，1 可用于串行对照，最多 8 路。
输入实体在主调线程分批深拷贝，避免 OCCT 网格化回写共享 TShape；原实例与原型保持不变。
输出顺序与输入一致；返回或抛出异常前等待已提交任务结束，不返回部分转换结果。

TubeDesigner 的生成预览、拆单均分为“准备输入 → 并行转换 → 串行发布”。
资源 URL、零件元数据和坐标规范化仍在原后端调用线程准备；工作线程不访问 Scene、资源池或数据库。
整批转换成功后才在原线程写入 BRep 资源、注册前端网格资源并继续数据库事务。
这里保证转换失败不发布该批 BRep，不表示后续多个资源写入具备整体回滚事务。

回归测试还覆盖共享原型不被网格化修改、圆柱接缝的正反向 pcurve 顺序和曲面坐标系的左右手性。
完整 88 件转换对照可单独运行：

```text
TubeDesignerTest.exe --gtest_filter=TemplateRuntimeTest.DISABLED_FiveFaceBRepConversionBenchmark --gtest_also_run_disabled_tests
```

该计时包含保护性深拷贝、网格化和中性 BRep 转换，不含之前的布尔求解、后续资源发布及界面刷新。
测试同时检查串并行拓扑／网格数量，以及还原实体的有效性、体积和包围盒。

## 目录结构

- `OpenCascadeResourceImporter.cpp`：OCCT 资源导入器实现。
- `OpenCascadeNeutralModelEvaluator.cpp`：中性几何图、二维路径、拉伸和布尔运算适配器。
- `OpenCascadeResourceImport.vcxproj`：插件 DLL 工程。
- `framework.h`、`pch.h`、`pch.cpp`、`dllmain.cpp`：Windows DLL 工程基础文件。
