# 防盗窗全参数自动化验收

入口：`src/tests/iCAX-UI/TubeDesignerSecurityWindowParameters.native.mjs`。

针对当前统一防盗窗模板 `single-face-security-window`，读取实际描述符生成覆盖表。当前版本有 115 项公开参数，用例数量由当前描述符与场景生成并记录在报告中；新增参数没有对应合法场景、遗漏枚举选项、重复用例标识时，测试立即失败。

## 验收内容

- 结构及排杆参数：单／双／三／五面、三种格栅、开启口开关和位置、边界、重算留距、杆件相碰及数量限制。
- 管材参数：完整应用规格预设、7 个管材角色、全部截面选项、壁厚和圆角、接收管内腔约束。
- 工艺及标识参数：制造方式、拼角和包边、实际模具资源、入榫和间隙的临界值、独立装配的适用范围、编号长度。
- 宿主验证：类型、上下限、未知枚举、默认值和 null、固定只读字段。原生 SDO 在规范化前筛选已声明字段；额外草稿不能混入返回参数或几何。
- 隐藏联动：保留停用草稿，包括超出编辑边界或无法用于实体的管材尺寸。停用的草稿应不影响实际几何，也不能阻断生成；可激活字段在适用分支校验编辑边界。
- 实际接收端：检查非方管接收方向、浮点临界和窗扇自身接收内腔，以及五面顶底双向格栅仍需校验两种管材。窗扇竖杆插入自身框截面中部；主格栅入榫参数不能改变窗扇实体。

每个不同的合法配置都调用原生 `GenerateProductTemplatePreview`，核对全部宿主规范化参数、有效的几何资源、有限尺寸和变换、框件数量。带比较的用例还调用 Python 中性模型，验证输入不变、返回参数恒等，并比较实际管件引用到的完整几何声明图；只改变编号不能算几何变化。

拆单用例额外调用原生 `GetProductManufacturingPlan`，比较制造几何和连接属性。此接口仅声明计划；业务拒绝继续调用 `GeneratePreview` 和 `Disassemble`，到实际局部工艺校验后才接受拒绝。配置了 `nativeDisassembly` 的合法用例也执行真实拆单，核对最终单实体有效性、正体积和保存的宿主参数。`nativeSolidEffect` 可比较最终实体体积，`expectedProcessParameters` 核对原生保存的实际工艺选择。其余合法用例仍以原生预览、计划和中性实体图验证；此套测试不覆盖后续下料／加工工作区、排样或导出。

穿杆内腔和开孔间隙属于拆单验证。成品预览保留真实截面，即使该截面不能采用指定插接也允许设计显示。独立装配来源不适用时按描述符 `unavailableChoiceFallback` 使用内置路线，返回参数仍保留原草稿；这两条阶段与条件规则均有原生回归。

条件判断直接复用产品的 `parameterConditions.mjs`，同时检查条件引用及枚举值合法性，不另建条件解释器。业务拒绝必须匹配对应原因，桥接退出、超时及运行环境错误不算正确拒绝。

## 运行

先按仓库原生测试构建流程准备：

- `tmp/security-window-frames-native/SecurityWindowFramesBridge.exe`（复用现有框件验收桥接程序）。
- `src/x64/Debug` 中当前构建的 DLL。
- Node.js 和 Python 3。Python 默认使用本机 Codex 配套运行时，可用 `ICAX_PYTHON` 指定其他可执行文件。

在仓库根目录运行：

```powershell
node src/tests/iCAX-UI/TubeDesignerSecurityWindowParameters.native.mjs
```

支持以下环境变量：

| 变量 | 用途 |
| --- | --- |
| `ICAX_PYTHON` | Python 可执行文件的绝对路径 |
| `ICAX_SECURITY_WINDOW_BRIDGE` | 已构建桥接程序的绝对路径 |
| `ICAX_SECURITY_WINDOW_RUNTIME` | 显式隔离运行目录；桥接程序必须位于该目录，报告检查实际载入模块路径及哈希 |
| `ICAX_SECURITY_PARAMETER_JOBS` | 独立原生工作进程数，默认 2，允许 1..4 |
| `ICAX_SECURITY_PARAMETER_REPORT` | 报告输出目录 |
| `ICAX_SECURITY_PARAMETER_FILTER` | 用例标识的正则表达式，用于复现；报告明确标注非完整运行 |

例如，仅复现多面窗隐藏管材：

```powershell
$env:ICAX_SECURITY_PARAMETER_FILTER = '^materials\..*inactive-(two|three)-'
$env:ICAX_SECURITY_PARAMETER_REPORT = 'output/tests/security-window-reproduction'
node src/tests/iCAX-UI/TubeDesignerSecurityWindowParameters.native.mjs
```

## 报告与失败处理

默认输出在 `output/tests/security-window-parameters`：

- `report.html`：逐参数覆盖、失败解释和复现配置、全部用例结果。
- `results.json`：完整结果与 24 组显示／启用条件矩阵。
- `failures.json`：失败用例的输入、比较基线、预期和实际原因。
- `native-descriptor.json`：本次真实原生描述符。

失败时进程返回非零状态，仍保存已完成结果。当前已发现的产品问题保留正常业务期望，不改成“预期失败”来获得全绿结果。只修正与实际接口契约冲突的测试假设；发现生产缺陷后应修复生产逻辑并原样重跑。
