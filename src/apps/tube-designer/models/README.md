# 配件模型与模板资源契约

配件是可重复引用的真实实体，不是图片、空占位节点或伪装成管材的超扁截面。用户库导入 STEP/STP/BREP；模板和系统模型包也可提供完整的 `icax.neutral-model` JSON。以下示例属于几何和数据契约，不是承载、安装或材料安全认证。

## 来源与坐标

| 引用 | 来源 | 说明 |
| --- | --- | --- |
| `system:post-cap` | 产品安装目录 `models/post-cap/` | 随产品提供的固定尺寸模型 |
| `library:<记录ID>` | 用户配件库 | 导入后保存的 BRep 和元数据，不引用原文件路径 |
| `template:post-cap` | 当前模板包的声明资源 | 必须先在描述符中声明包内路径 |

单位为 mm。系统/模板资源约定 XY 居中、Z 底面为 0；用户导入会按包络作同样的平移归一，保留原朝向。模板只作刚体平移、旋转，不得非均匀缩放模型来凑接口。BREP 文件本身的实际单位、玻璃夹槽宽、柱帽覆盖尺寸等需要核对。

当前系统资源：

| ID | 实体 | 基本尺寸及用途 |
| --- | --- | --- |
| `post-cap` | 80 方柱平盖 | 84 × 84 × 6，底面放在柱顶，平底焊接型 |
| `post-cap-40` | 40 方柱平盖 | 44 × 44 × 6，平底焊接型 |
| `spear-tip` | 20 管平底枪尖 | 底座 20 × 20 × 4，总高 70，尖体横向宽 28 |
| `glass-clamp` | C 形玻璃夹 | 30 × 20 × 30，槽宽 8，开口朝局部 +X |
| `connector-block` | 双孔连接块 | 40 × 30 × 20，两个直径 8 的通孔，孔距 20 |

这些模型为原创固定尺寸示例，并非某厂家配件的采购或承载认证。枪尖、柱帽高出主防护高度，选型时应计入最终产品包络。

## 系统模型包

目录为 `models/<id>/model.json` 和描述符指定的实体文件。例如：

```json
{
  "schema": "icax.component-model",
  "schemaVersion": 1,
  "id": "post-cap",
  "name": "80方柱平盖",
  "category": "护栏/柱帽",
  "sourcing": "purchased",
  "material": "Q235B",
  "description": "固定尺寸平底柱帽；安装前核对连接界面。",
  "geometryFile": "model.icaxmodel.json"
}
```

`geometryFile` 可以指向真实 STEP/STP/BREP 或完整中性几何 JSON。JSON 必须声明几何节点、item 和输出；资源加载只取输出明确引用的根实体，不把未使用的刀具体或构造辅助体并入配件。不得把普通文本保存为 `.step` 冒充模型。

## 模板资源声明

模板描述符 `template.json` 使用 `extensions.modelResources`：

```json
{
  "extensions": {
    "modelResources": {
      "post-cap": {
        "path": "resources/post-cap.icaxmodel.json",
        "name": "模板柱帽",
        "category": "护栏/柱帽",
        "sourcing": "purchased",
        "material": "Q235B",
        "description": "底面 Z=0，固定尺寸柱帽。"
      }
    }
  }
}
```

`path` 必须是模板包内相对路径，使用 `/`；不允许绝对路径、盘符、`..` 或通过链接跳出包目录。未声明的 `template:` 引用必须失败，不能临时猜测文件位置。示例文件位于 `templates/modular_guardrail/resources/post-cap.icaxmodel.json`。

可让用户选择模型的参数使用字符串和模型编辑器：

```json
{
  "key": "postCapModelReference",
  "displayName": { "zh-CN": "柱帽模型" },
  "valueType": "string",
  "defaultValue": "system:post-cap",
  "presentation": { "editor": "component-model" }
}
```

## 中性模型中的引用与放置

模板声明的是稳定资源名，不是磁盘路径：

```json
{
  "key": "cap.resource",
  "operator": "resource",
  "inputs": [],
  "arguments": { "reference": "template:post-cap" }
}
```

随后用 `transform` 引用该节点，给出位置和右手正交单位坐标轴：

```json
{
  "key": "cap.placed",
  "operator": "transform",
  "inputs": ["cap.resource"],
  "arguments": {
    "placement": {
      "origin": [1000, 0, 1200],
      "xAxis": [1, 0, 0],
      "yAxis": [0, 1, 0],
      "zAxis": [0, 0, 1]
    }
  }
}
```

`_shared/component_models.py` 的 `ComponentModelGeometry` 可以在一次生成内共享同一资源节点，同时为各 item 保留独立刚体位置；它校验来源和正交单位轴，拒绝变形缩放。真正的文件加载、实体校验及快照冻结在产品层完成。通用引擎不读取 `reference` 对应的文件，它只接受产品层注入的真实 `brep`。

同一产品生成时保存几何和元数据快照。拆单必须使用已提交的快照，不因后来编辑/删除库记录而更换零件。资源的名称、材料、供料方式和尺寸以解析快照为准，不能用模板中的占位文本替换真实来源。

## 制造与清单边界

普通配件 item 至少携带：

```json
{
  "manufacturing.partKind": "accessory",
  "manufacturing.materialCategory": "accessory",
  "manufacturing.modelReference": "system:post-cap",
  "manufacturing.sourcing": "purchased",
  "manufacturing.process": "purchased",
  "manufacturing.categoryKey": "accessory.post_cap",
  "manufacturing.categoryName": "柱帽",
  "quantity": 1
}
```

- `made` 表示自制，`purchased` 表示外购；这与能否进行管材排样是两个不同判断。
- 自制机加工连接块仍是 `accessory`，不添加 `tubeDesigner.profile`，不进入管材排样。
- X/菱形直管花格是 `tube`、`made`，保留真实截面、斜切/相贯几何及加工信息，可以进入管材流程。
- 金属板是 `plate`，玻璃是 `glass`。玻璃为独立外购项，保留宽、高、总厚度，当前尺寸数据沿用 `manufacturing.plate` 字段，但材料类型必须是 `glass`，不得混为金属板或管材。
- 板件、玻璃及普通配件参与产品装配、制造清单和导出，但不参与一维管材排样。当前不实现板材二维排样、玻璃排版或复杂配件 CAM。
- 导入弯花、弯管或装饰 STEP 的外形不构成加工工艺。缺少明确毛坯、弯曲参数、展开及设备工艺时，不得自动把它判为可排样直管，更不能宣称可直接加工。

玻璃材质名称、总厚度和夹具示意不是安全认证；夹层组成、钢化要求、孔边/板边处理、承载和锚固仍需按项目确认。未使用的配件模型不能被误计入产品清单。
