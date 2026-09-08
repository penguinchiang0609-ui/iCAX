# 程式管型参数示意图

`profile.json` 可以声明可选的 `parameterDiagram`，用于把参数输入框与截面上受影响的位置对应起来。标注引用稳定的参数 `key`，不依赖显示名称。图中轮廓仍由 `profile.py` 生成，示意图不改变几何。

```json
{
  "parameterDiagram": {
    "schemaVersion": 1,
    "annotations": [
      {
        "parameter": "width",
        "kind": "linear",
        "axis": "x",
        "side": "bottom",
        "from": ["-width/2", 0],
        "to": ["width/2", 0],
        "description": "左右外侧之间的截面总宽。"
      },
      {
        "parameter": "wallThickness",
        "kind": "leader",
        "side": "right",
        "point": ["width/2-wallThickness/2", 0],
        "description": { "zh-CN": "内外轮廓之间的壁厚。" }
      }
    ]
  }
}
```

## 标注与坐标

- `annotations` 包含 1–128 个标注，`parameter` 必须引用已声明的参数。
- `linear` 表示线性尺寸，仅用于数值参数。`from`、`to` 是两个二维坐标；`axis: "x"` 对应 `side: "top" | "bottom"`，`axis: "y"` 对应 `side: "left" | "right"`。尺寸是两点沿指定轴的距离，并非斜线长度。
- `leader` 表示引线指向 `point`，适用于壁厚、圆角、边数、星角数、模式、比例等参数。`side` 可以是 `top`、`bottom`、`left` 或 `right`，用于选择标签排布方向。它不是通用长度尺寸：枚举值、整数数量、无量纲比例仍按原参数含义显示。
- 坐标使用 `contours(profile, clearance=0, swap_axes=False)` 的实际 XY 截面坐标，单位为毫米；Y 正向向上。不要使用屏幕像素或预先翻转 Y 轴。
- `description` 可选，支持非空文本或本地化文本对象。应解释参数影响的具体部位、联动和语义区别，例如“同时改变翼缘和腹板厚度”“截面总宽并非单块翼缘宽度”。
- 参数是否可见仍由该参数定义的 `visibleWhen` 决定，前端会相应隐藏其标注；元数据中的所有坐标都必须在有效参数组合下可求值，包括当前隐藏的参数。

## 安全坐标表达式

每个坐标可以直接写有限数值，或写不超过 256 字符的表达式。允许：

- 已声明的 `number`、`integer` 参数名，以及数值常量。
- 括号、一元 `+` / `-` 和二元 `+`、`-`、`*`、`/`。
- `min(...)`、`max(...)`：1–8 个实参。
- `abs(x)`、`sqrt(x)`、`sin(x)`、`cos(x)`：一个实参，三角函数角度单位为弧度。
- `contourX(c, v)`、`contourY(c, v)`：读取第 `c` 个轮廓的第 `v` 个真实顶点。两个索引均从 0 开始，必须是非负、未越界的整数；轮廓必须是含有 `points` 的 `polygon`。圆、椭圆、路径等不能通过这两个函数访问。

例如多边形管的星谷可指向 `["contourX(0,1)", "contourY(0,1)"]`。这样即使边数、星谷比或外宽外高变化，标注仍落在经过缩放和居中后的真实顶点，而不是估算位置。边数、轮廓类型可用 `leader` 指向对应顶点，再通过说明解释整个轮廓的变化。

表达式使用受限语法树解释，不使用 `eval`。禁止属性访问、下标、任意函数调用、列表推导、幂运算以及 `build()` 返回的自定义变量。每条表达式最多 64 个语法树节点、16 层嵌套；所有求值结果和中间值必须有限且绝对值不超过 10 亿。除零、无效平方根和越界轮廓索引都会返回明确错误。

## 求值快照与兼容性

运行时每次生成截面后，用同一组规范化参数和真实轮廓求值 `parameterDiagram`。返回的截面快照保存 `schemaVersion: 1`、参数引用、说明和具体数值坐标，不再包含坐标表达式。参数实际值位于同一快照的 `parameters` 中，参数名称、类型、选项及可见性位于 `parameterDefinitions` 中；前端无需执行表达式。

现有描述文件版本仍为 `icax.tube-profile-descriptor` / `schemaVersion: 2`。没有声明 `parameterDiagram` 的旧包继续正常导入和求值，其快照不包含这个字段；界面保留普通轮廓预览，不猜测复杂自定义参数的尺寸含义。为旧包补充元数据不会改变几何，但描述内容参与包摘要，因此摘要会更新。
