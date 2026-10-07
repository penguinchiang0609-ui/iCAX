# 百叶窗参数声明附录

归档模板 `louver-window` / `2.1.0`，共 35 项。
以下从 reference/template.json 原声明生成；默认值不是推荐工程值。未声明约束不表示没有脚本几何约束。
显隐、禁用和选择条件的结构原样保留，供共享 parameterConditions.mjs 解释，不另造条件解释器。

| 键 | 名称 | 类型 | 默认值 | 分组 | 单位 |
| --- | --- | --- | --- | --- | --- |
| productCode | 产品编号 | string | "BY-001" | overall |  |
| width | 外宽 W | number | 1200 | overall | mm |
| height | 外高 H | number | 1500 | overall | mm |
| frameJoint | 边框拼接 | enum | "side_wrap" | frame_process |  |
| assemblyPlanningMode | 装配工艺来源 | enum | "builtin_rules" | frame_process |  |
| frameProfileType | 截面类型 | enum | "rect" | frame_profile |  |
| frameWidth | 边框面宽 | number | 40 | frame_profile | mm |
| frameDepth | 边框深度 | number | 100 | frame_profile | mm |
| frameCornerRadius | 外圆角 R | number | 2 | frame_profile | mm |
| frameWallThickness | 壁厚 | number | 1.5 | frame_profile | mm |
| frameMaterial | 材质 | string | "铝合金" | frame_profile |  |
| bladeProfileType | 截面类型 | enum | "rect" | blade_profile |  |
| bladeWidth | 截面宽 B | number | 80 | blade_profile | mm |
| bladeDepth | 截面深 T | number | 20 | blade_profile | mm |
| bladeCornerRadius | 外圆角 R | number | 2 | blade_profile | mm |
| bladeWallThickness | 壁厚 | number | 1.5 | blade_profile | mm |
| bladeMaterial | 材质 | string | "铝合金" | blade_profile |  |
| middlePostCount | 中间立柱数 | integer | 0 | frame |  |
| middleBeamCount | 中间横梁排数 | integer | 0 | frame |  |
| supportMode | 中间支撑连接 | enum | "split" | frame_process |  |
| bladeDirectionAngle | 排列方向角 φ | number | 0 | blades | deg |
| bladeRollAngle | 截面滚转角 θ | number | 45 | blades | deg |
| arrayMode | 排列方式 | enum | "pitch" | array |  |
| bladePitch | 中心距 P | number | 85 | array | mm |
| bladeGap | 投影净缝 G | number | 15 | array | mm |
| bladeOverlap | 投影搭接 O | number | 5 | array | mm |
| bladeCount | 每格叶片数 | integer | 10 | array |  |
| startOffset | 阵列起端最小边距 | number | 3 | array | mm |
| endOffset | 阵列末端最小边距 | number | 3 | array | mm |
| equalEdgeMargin | 余量两端均分 | boolean | true | array |  |
| bladeConnection | 叶片端部连接 | enum | "face_weld" | connection |  |
| endClearance | 顶接端面间隙 | number | 0 | connection | mm |
| insertDepth | 插入深度（垂直框壁） | number | 10 | connection | mm |
| throughExtension | 外露长度（垂直框壁） | number | 0 | connection | mm |
| slotClearance | 开槽单边安装间隙 | number | 0.3 | connection | mm |

## 完整声明

每段包含原参数的全部字段：choices 的值和名称、constraints、visibleWhen、enabledWhen、readOnly、presentation、description 等（以实际存在字段为准）。

### productCode — 产品编号

```json
{
  "key": "productCode",
  "displayName": {
    "zh-CN": "产品编号"
  },
  "valueType": "string",
  "defaultValue": "BY-001",
  "group": "overall"
}
```

### width — 外宽 W

```json
{
  "key": "width",
  "displayName": {
    "zh-CN": "外宽 W"
  },
  "valueType": "number",
  "defaultValue": 1200,
  "group": "overall",
  "unit": "mm",
  "quantity": "length",
  "constraints": {
    "minimum": 100,
    "maximum": 10000
  }
}
```

### height — 外高 H

```json
{
  "key": "height",
  "displayName": {
    "zh-CN": "外高 H"
  },
  "valueType": "number",
  "defaultValue": 1500,
  "group": "overall",
  "unit": "mm",
  "quantity": "length",
  "constraints": {
    "minimum": 100,
    "maximum": 10000
  }
}
```

### frameJoint — 边框拼接

```json
{
  "key": "frameJoint",
  "displayName": {
    "zh-CN": "边框拼接"
  },
  "valueType": "enum",
  "defaultValue": "side_wrap",
  "group": "frame_process",
  "visibleWhen": {
    "op": "ne",
    "parameter": "assemblyPlanningMode",
    "value": "external_templates"
  },
  "choices": [
    {
      "value": "side_wrap",
      "displayName": {
        "zh-CN": "左右框包上下框"
      }
    },
    {
      "value": "horizontal_wrap",
      "displayName": {
        "zh-CN": "上下框包左右框"
      }
    },
    {
      "value": "miter",
      "displayName": {
        "zh-CN": "45°拼角"
      }
    }
  ]
}
```

### assemblyPlanningMode — 装配工艺来源

```json
{
  "key": "assemblyPlanningMode",
  "displayName": {
    "zh-CN": "装配工艺来源"
  },
  "valueType": "enum",
  "defaultValue": "builtin_rules",
  "group": "frame_process",
  "choices": [
    {
      "value": "builtin_rules",
      "displayName": {
        "zh-CN": "产品内置加工规则"
      }
    },
    {
      "value": "external_templates",
      "displayName": {
        "zh-CN": "各连接节点选择工艺模板"
      }
    }
  ],
  "presentation": {
    "visible": false,
    "choiceConditions": {
      "external_templates": {
        "op": "all",
        "conditions": [
          {
            "op": "eq",
            "parameter": "bladeConnection",
            "value": "face_weld"
          },
          {
            "op": "in",
            "parameter": "bladeDirectionAngle",
            "values": [
              0,
              90,
              -90,
              180,
              -180
            ]
          },
          {
            "op": "any",
            "conditions": [
              {
                "op": "eq",
                "parameter": "supportMode",
                "value": "split"
              },
              {
                "op": "all",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "middlePostCount",
                    "value": 0
                  },
                  {
                    "op": "eq",
                    "parameter": "middleBeamCount",
                    "value": 0
                  }
                ]
              }
            ]
          }
        ]
      }
    },
    "unavailableChoiceFallback": "builtin_rules"
  },
  "description": "仅支持水平或竖直顶接叶片，以及分格分段的中间支撑。提供未加工原管与 L/T 节点；斜向端切、插槽和穿透仍需产品内置加工规则。"
}
```

### frameProfileType — 截面类型

```json
{
  "key": "frameProfileType",
  "displayName": {
    "zh-CN": "截面类型"
  },
  "valueType": "enum",
  "defaultValue": "rect",
  "presentation": {
    "editor": "profile-library",
    "resourceRole": "frame",
    "profileConstraints": {
      "sectionKinds": [
        "rect"
      ]
    }
  },
  "group": "frame_profile",
  "choices": [
    {
      "value": "rect",
      "displayName": {
        "zh-CN": "矩形管（含方管、扁管）"
      }
    }
  ],
  "readOnly": true
}
```

### frameWidth — 边框面宽

```json
{
  "key": "frameWidth",
  "displayName": {
    "zh-CN": "边框面宽"
  },
  "valueType": "number",
  "defaultValue": 40,
  "group": "frame_profile",
  "unit": "mm",
  "quantity": "length",
  "constraints": {
    "minimum": 5,
    "maximum": 500
  }
}
```

### frameDepth — 边框深度

```json
{
  "key": "frameDepth",
  "displayName": {
    "zh-CN": "边框深度"
  },
  "valueType": "number",
  "defaultValue": 100,
  "group": "frame_profile",
  "unit": "mm",
  "quantity": "length",
  "constraints": {
    "minimum": 5,
    "maximum": 500
  }
}
```

### frameCornerRadius — 外圆角 R

```json
{
  "key": "frameCornerRadius",
  "displayName": {
    "zh-CN": "外圆角 R"
  },
  "valueType": "number",
  "defaultValue": 2,
  "group": "frame_profile",
  "unit": "mm",
  "quantity": "length",
  "constraints": {
    "minimum": 0,
    "maximum": 100
  }
}
```

### frameWallThickness — 壁厚

```json
{
  "key": "frameWallThickness",
  "displayName": {
    "zh-CN": "壁厚"
  },
  "valueType": "number",
  "defaultValue": 1.5,
  "group": "frame_profile",
  "unit": "mm",
  "quantity": "length",
  "constraints": {
    "minimum": 0.1,
    "maximum": 20
  }
}
```

### frameMaterial — 材质

```json
{
  "key": "frameMaterial",
  "displayName": {
    "zh-CN": "材质"
  },
  "valueType": "string",
  "defaultValue": "铝合金",
  "group": "frame_profile"
}
```

### bladeProfileType — 截面类型

```json
{
  "key": "bladeProfileType",
  "displayName": {
    "zh-CN": "截面类型"
  },
  "valueType": "enum",
  "defaultValue": "rect",
  "presentation": {
    "editor": "profile-library",
    "resourceRole": "blade",
    "profileConstraints": {
      "sectionKinds": [
        "rect"
      ]
    }
  },
  "group": "blade_profile",
  "choices": [
    {
      "value": "rect",
      "displayName": {
        "zh-CN": "矩形管（含方管、扁管）"
      }
    }
  ],
  "readOnly": true
}
```

### bladeWidth — 截面宽 B

```json
{
  "key": "bladeWidth",
  "displayName": {
    "zh-CN": "截面宽 B"
  },
  "valueType": "number",
  "defaultValue": 80,
  "group": "blade_profile",
  "unit": "mm",
  "quantity": "length",
  "constraints": {
    "minimum": 5,
    "maximum": 500
  }
}
```

### bladeDepth — 截面深 T

```json
{
  "key": "bladeDepth",
  "displayName": {
    "zh-CN": "截面深 T"
  },
  "valueType": "number",
  "defaultValue": 20,
  "group": "blade_profile",
  "unit": "mm",
  "quantity": "length",
  "constraints": {
    "minimum": 5,
    "maximum": 500
  }
}
```

### bladeCornerRadius — 外圆角 R

```json
{
  "key": "bladeCornerRadius",
  "displayName": {
    "zh-CN": "外圆角 R"
  },
  "valueType": "number",
  "defaultValue": 2,
  "group": "blade_profile",
  "unit": "mm",
  "quantity": "length",
  "constraints": {
    "minimum": 0,
    "maximum": 100
  }
}
```

### bladeWallThickness — 壁厚

```json
{
  "key": "bladeWallThickness",
  "displayName": {
    "zh-CN": "壁厚"
  },
  "valueType": "number",
  "defaultValue": 1.5,
  "group": "blade_profile",
  "unit": "mm",
  "quantity": "length",
  "constraints": {
    "minimum": 0.1,
    "maximum": 20
  }
}
```

### bladeMaterial — 材质

```json
{
  "key": "bladeMaterial",
  "displayName": {
    "zh-CN": "材质"
  },
  "valueType": "string",
  "defaultValue": "铝合金",
  "group": "blade_profile"
}
```

### middlePostCount — 中间立柱数

```json
{
  "key": "middlePostCount",
  "displayName": {
    "zh-CN": "中间立柱数"
  },
  "valueType": "integer",
  "defaultValue": 0,
  "group": "frame",
  "constraints": {
    "minimum": 0,
    "maximum": 10
  }
}
```

### middleBeamCount — 中间横梁排数

```json
{
  "key": "middleBeamCount",
  "displayName": {
    "zh-CN": "中间横梁排数"
  },
  "valueType": "integer",
  "defaultValue": 0,
  "group": "frame",
  "constraints": {
    "minimum": 0,
    "maximum": 10
  }
}
```

### supportMode — 中间支撑连接

```json
{
  "key": "supportMode",
  "displayName": {
    "zh-CN": "中间支撑连接"
  },
  "valueType": "enum",
  "defaultValue": "split",
  "group": "frame_process",
  "choices": [
    {
      "value": "split",
      "displayName": {
        "zh-CN": "按格分段"
      }
    },
    {
      "value": "through",
      "displayName": {
        "zh-CN": "整根穿过支撑"
      }
    }
  ],
  "description": "中间横梁在立柱处分段；穿过支撑时自动切出真实截面槽。",
  "visibleWhen": {
    "op": "all",
    "conditions": [
      {
        "op": "ne",
        "parameter": "bladeConnection",
        "value": "through_insert"
      },
      {
        "op": "any",
        "conditions": [
          {
            "op": "ne",
            "parameter": "middlePostCount",
            "value": 0
          },
          {
            "op": "ne",
            "parameter": "middleBeamCount",
            "value": 0
          }
        ]
      }
    ]
  }
}
```

### bladeDirectionAngle — 排列方向角 φ

```json
{
  "key": "bladeDirectionAngle",
  "displayName": {
    "zh-CN": "排列方向角 φ"
  },
  "valueType": "number",
  "defaultValue": 0,
  "group": "blades",
  "unit": "deg",
  "quantity": "angle",
  "constraints": {
    "minimum": -180,
    "maximum": 180
  },
  "description": "正视图内叶片轴线：0°横向、90°竖向，其他为斜向。"
}
```

### bladeRollAngle — 截面滚转角 θ

```json
{
  "key": "bladeRollAngle",
  "displayName": {
    "zh-CN": "截面滚转角 θ"
  },
  "valueType": "number",
  "defaultValue": 45,
  "group": "blades",
  "unit": "deg",
  "quantity": "angle",
  "constraints": {
    "minimum": -90,
    "maximum": 90
  },
  "description": "0°时截面宽 B 沿窗深方向；90°时 B 平行窗面。与排列方向角独立。"
}
```

### arrayMode — 排列方式

```json
{
  "key": "arrayMode",
  "displayName": {
    "zh-CN": "排列方式"
  },
  "valueType": "enum",
  "defaultValue": "pitch",
  "group": "array",
  "choices": [
    {
      "value": "pitch",
      "displayName": {
        "zh-CN": "指定中心距"
      }
    },
    {
      "value": "gap",
      "displayName": {
        "zh-CN": "指定投影净缝"
      }
    },
    {
      "value": "overlap",
      "displayName": {
        "zh-CN": "指定投影搭接"
      }
    },
    {
      "value": "count",
      "displayName": {
        "zh-CN": "指定叶片数量"
      }
    }
  ]
}
```

### bladePitch — 中心距 P

```json
{
  "key": "bladePitch",
  "displayName": {
    "zh-CN": "中心距 P"
  },
  "valueType": "number",
  "defaultValue": 85,
  "group": "array",
  "unit": "mm",
  "quantity": "length",
  "constraints": {
    "minimum": 0.01,
    "maximum": 2000
  },
  "visibleWhen": {
    "op": "eq",
    "parameter": "arrayMode",
    "value": "pitch"
  }
}
```

### bladeGap — 投影净缝 G

```json
{
  "key": "bladeGap",
  "displayName": {
    "zh-CN": "投影净缝 G"
  },
  "valueType": "number",
  "defaultValue": 15,
  "group": "array",
  "unit": "mm",
  "quantity": "length",
  "constraints": {
    "minimum": 0,
    "maximum": 2000
  },
  "visibleWhen": {
    "op": "eq",
    "parameter": "arrayMode",
    "value": "gap"
  }
}
```

### bladeOverlap — 投影搭接 O

```json
{
  "key": "bladeOverlap",
  "displayName": {
    "zh-CN": "投影搭接 O"
  },
  "valueType": "number",
  "defaultValue": 5,
  "group": "array",
  "unit": "mm",
  "quantity": "length",
  "constraints": {
    "minimum": 0,
    "maximum": 2000
  },
  "visibleWhen": {
    "op": "eq",
    "parameter": "arrayMode",
    "value": "overlap"
  }
}
```

### bladeCount — 每格叶片数

```json
{
  "key": "bladeCount",
  "displayName": {
    "zh-CN": "每格叶片数"
  },
  "valueType": "integer",
  "defaultValue": 10,
  "group": "array",
  "constraints": {
    "minimum": 1,
    "maximum": 200
  },
  "visibleWhen": {
    "op": "eq",
    "parameter": "arrayMode",
    "value": "count"
  }
}
```

### startOffset — 阵列起端最小边距

```json
{
  "key": "startOffset",
  "displayName": {
    "zh-CN": "阵列起端最小边距"
  },
  "valueType": "number",
  "defaultValue": 3,
  "group": "array",
  "unit": "mm",
  "quantity": "length",
  "constraints": {
    "minimum": 0,
    "maximum": 2000
  }
}
```

### endOffset — 阵列末端最小边距

```json
{
  "key": "endOffset",
  "displayName": {
    "zh-CN": "阵列末端最小边距"
  },
  "valueType": "number",
  "defaultValue": 3,
  "group": "array",
  "unit": "mm",
  "quantity": "length",
  "constraints": {
    "minimum": 0,
    "maximum": 2000
  }
}
```

### equalEdgeMargin — 余量两端均分

```json
{
  "key": "equalEdgeMargin",
  "displayName": {
    "zh-CN": "余量两端均分"
  },
  "valueType": "boolean",
  "defaultValue": true,
  "group": "array",
  "description": "在起末最小边距之外均分余量；关闭时从起端排列。单根居中。"
}
```

### bladeConnection — 叶片端部连接

```json
{
  "key": "bladeConnection",
  "displayName": {
    "zh-CN": "叶片端部连接"
  },
  "valueType": "enum",
  "defaultValue": "face_weld",
  "group": "connection",
  "choices": [
    {
      "value": "face_weld",
      "displayName": {
        "zh-CN": "顶接焊接"
      }
    },
    {
      "value": "slot_insert",
      "displayName": {
        "zh-CN": "插槽定位"
      }
    },
    {
      "value": "through_insert",
      "displayName": {
        "zh-CN": "穿透边框"
      }
    }
  ]
}
```

### endClearance — 顶接端面间隙

```json
{
  "key": "endClearance",
  "displayName": {
    "zh-CN": "顶接端面间隙"
  },
  "valueType": "number",
  "defaultValue": 0,
  "group": "connection",
  "unit": "mm",
  "quantity": "length",
  "constraints": {
    "minimum": 0,
    "maximum": 10
  },
  "visibleWhen": {
    "op": "all",
    "conditions": [
      {
        "op": "eq",
        "parameter": "bladeConnection",
        "value": "face_weld"
      },
      {
        "op": "ne",
        "parameter": "assemblyPlanningMode",
        "value": "external_templates"
      }
    ]
  }
}
```

### insertDepth — 插入深度（垂直框壁）

```json
{
  "key": "insertDepth",
  "displayName": {
    "zh-CN": "插入深度（垂直框壁）"
  },
  "valueType": "number",
  "defaultValue": 10,
  "group": "connection",
  "unit": "mm",
  "quantity": "length",
  "constraints": {
    "minimum": 0.1,
    "maximum": 200
  },
  "visibleWhen": {
    "op": "eq",
    "parameter": "bladeConnection",
    "value": "slot_insert"
  }
}
```

### throughExtension — 外露长度（垂直框壁）

```json
{
  "key": "throughExtension",
  "displayName": {
    "zh-CN": "外露长度（垂直框壁）"
  },
  "valueType": "number",
  "defaultValue": 0,
  "group": "connection",
  "unit": "mm",
  "quantity": "length",
  "constraints": {
    "minimum": 0,
    "maximum": 200
  },
  "visibleWhen": {
    "op": "eq",
    "parameter": "bladeConnection",
    "value": "through_insert"
  }
}
```

### slotClearance — 开槽单边安装间隙

```json
{
  "key": "slotClearance",
  "displayName": {
    "zh-CN": "开槽单边安装间隙"
  },
  "valueType": "number",
  "defaultValue": 0.3,
  "group": "connection",
  "unit": "mm",
  "quantity": "length",
  "constraints": {
    "minimum": 0,
    "maximum": 5
  },
  "visibleWhen": {
    "op": "any",
    "conditions": [
      {
        "op": "ne",
        "parameter": "bladeConnection",
        "value": "face_weld"
      },
      {
        "op": "all",
        "conditions": [
          {
            "op": "eq",
            "parameter": "supportMode",
            "value": "through"
          },
          {
            "op": "any",
            "conditions": [
              {
                "op": "ne",
                "parameter": "middlePostCount",
                "value": 0
              },
              {
                "op": "ne",
                "parameter": "middleBeamCount",
                "value": 0
              }
            ]
          }
        ]
      }
    ]
  }
}
```
