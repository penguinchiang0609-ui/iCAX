# 雕花装饰门参数声明附录

归档模板 `decorative-door` / `1.0.0`，共 45 项。
以下从 reference/template.json 原声明生成；默认值不是推荐工程值。未声明约束不表示没有脚本几何约束。
显隐、禁用和选择条件的结构原样保留，供共享 parameterConditions.mjs 解释，不另造条件解释器。

| 键 | 名称 | 类型 | 默认值 | 分组 | 单位 |
| --- | --- | --- | --- | --- | --- |
| productCode | 产品编号 | string | "MH-001" | door |  |
| doorType | 门体结构 | enum | "double" | door |  |
| width | 门扇组合总宽 | number | 1800 | door | mm |
| height | 门扇高度 | number | 2200 | door | mm |
| thickness | 门板厚度 | number | 40 | material | mm |
| leafGap | 扇间门缝 | number | 4 | door | mm |
| leafCount | 门扇数 | integer | 4 | door |  |
| motherRatio | 主扇宽度比例 | number | 0.65 | door |  |
| motherSide | 主扇位置 | enum | "left" | door |  |
| firstHingeSide | 首扇铰链侧 | enum | "left" | door |  |
| pattern | 装饰模块 | enum | "lines" | design |  |
| composition | 多扇构图 | enum | "mirror" | design |  |
| layout | 门面版式 | enum | "full" | design |  |
| bandRatio | 竖带宽度比例 | number | 0.4 | design |  |
| bandOffset | 竖带位置比例 | number | 0.25 | design |  |
| arrayMode | 线条排列 | enum | "count" | array |  |
| lineCount | 线条数 | integer | 8 | array |  |
| linePitch | 线条中心距 | number | 70 | array | mm |
| lineAngle | 线条方向角 | number | 90 | array | deg |
| columns | 花格／池板列数 | integer | 2 | array |  |
| rows | 花格／池板行数 | integer | 3 | array |  |
| webWidth | 单元间保留宽度 | number | 40 | array | mm |
| lineTool | 铣线截面 | enum | "flat" | process |  |
| regionProcess | 花格处理 | enum | "through" | process |  |
| machiningSide | 加工面 | enum | "front" | process |  |
| cutDepth | 单面加工深度 | number | 6 | process | mm |
| grooveWidth | 平底槽宽 | number | 8 | process | mm |
| vAngle | V刀夹角 | number | 90 | process | deg |
| minSkin | 最小剩余板厚 | number | 8 | safe | mm |
| trimEnabled | 池板独立扣线 | boolean | true | process |  |
| trimWidth | 扣线宽度 | number | 10 | process | mm |
| trimHeight | 扣线凸出高度 | number | 5 | process | mm |
| glassThickness | 背衬玻璃厚度 | number | 5 | material | mm |
| glassGap | 玻璃与门板间距 | number | 2 | process | mm |
| border | 四周保留宽度 | number | 100 | safe | mm |
| meetingBorder | 连续构图拼缝侧保留 | number | 35 | safe | mm |
| protectHardware | 保留锁／把手和铰链区 | boolean | true | safe |  |
| lockHeight | 锁区中心高度 | number | 1000 | safe | mm |
| lockWidth | 锁／把手保留区宽 | number | 100 | safe | mm |
| lockLength | 锁／把手保留区高 | number | 300 | safe | mm |
| hingeWidth | 铰链保留区宽 | number | 80 | safe | mm |
| hingeLength | 单个铰链保留区高 | number | 140 | safe | mm |
| material | 门板材料 | string | "铝板／复合门板（按工程选型）" | material |  |
| finish | 表面处理备注 | string | "按工程指定" | material |  |
| glassType | 背衬玻璃类型 | string | "按工程选型" | material |  |

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
  "defaultValue": "MH-001",
  "group": "door"
}
```

### doorType — 门体结构

```json
{
  "key": "doorType",
  "displayName": {
    "zh-CN": "门体结构"
  },
  "valueType": "enum",
  "defaultValue": "double",
  "group": "door",
  "choices": [
    {
      "value": "single",
      "displayName": {
        "zh-CN": "单开"
      }
    },
    {
      "value": "double",
      "displayName": {
        "zh-CN": "双开"
      }
    },
    {
      "value": "mother",
      "displayName": {
        "zh-CN": "子母"
      }
    },
    {
      "value": "multi",
      "displayName": {
        "zh-CN": "多扇"
      }
    }
  ]
}
```

### width — 门扇组合总宽

```json
{
  "key": "width",
  "displayName": {
    "zh-CN": "门扇组合总宽"
  },
  "valueType": "number",
  "defaultValue": 1800,
  "group": "door",
  "constraints": {
    "minimum": 300,
    "maximum": 10000
  },
  "quantity": "length",
  "unit": "mm"
}
```

### height — 门扇高度

```json
{
  "key": "height",
  "displayName": {
    "zh-CN": "门扇高度"
  },
  "valueType": "number",
  "defaultValue": 2200,
  "group": "door",
  "constraints": {
    "minimum": 300,
    "maximum": 4000
  },
  "quantity": "length",
  "unit": "mm"
}
```

### thickness — 门板厚度

```json
{
  "key": "thickness",
  "displayName": {
    "zh-CN": "门板厚度"
  },
  "valueType": "number",
  "defaultValue": 40,
  "group": "material",
  "constraints": {
    "minimum": 2,
    "maximum": 100
  },
  "quantity": "length",
  "unit": "mm"
}
```

### leafGap — 扇间门缝

```json
{
  "key": "leafGap",
  "displayName": {
    "zh-CN": "扇间门缝"
  },
  "valueType": "number",
  "defaultValue": 4,
  "group": "door",
  "constraints": {
    "minimum": 1,
    "maximum": 30
  },
  "quantity": "length",
  "unit": "mm",
  "visibleWhen": {
    "op": "ne",
    "parameter": "doorType",
    "value": "single"
  }
}
```

### leafCount — 门扇数

```json
{
  "key": "leafCount",
  "displayName": {
    "zh-CN": "门扇数"
  },
  "valueType": "integer",
  "defaultValue": 4,
  "group": "door",
  "constraints": {
    "minimum": 2,
    "maximum": 6
  },
  "visibleWhen": {
    "op": "eq",
    "parameter": "doorType",
    "value": "multi"
  }
}
```

### motherRatio — 主扇宽度比例

```json
{
  "key": "motherRatio",
  "displayName": {
    "zh-CN": "主扇宽度比例"
  },
  "valueType": "number",
  "defaultValue": 0.65,
  "group": "door",
  "constraints": {
    "minimum": 0.51,
    "maximum": 0.85
  },
  "visibleWhen": {
    "op": "eq",
    "parameter": "doorType",
    "value": "mother"
  }
}
```

### motherSide — 主扇位置

```json
{
  "key": "motherSide",
  "displayName": {
    "zh-CN": "主扇位置"
  },
  "valueType": "enum",
  "defaultValue": "left",
  "group": "door",
  "choices": [
    {
      "value": "left",
      "displayName": {
        "zh-CN": "左"
      }
    },
    {
      "value": "right",
      "displayName": {
        "zh-CN": "右"
      }
    }
  ],
  "visibleWhen": {
    "op": "eq",
    "parameter": "doorType",
    "value": "mother"
  }
}
```

### firstHingeSide — 首扇铰链侧

```json
{
  "key": "firstHingeSide",
  "displayName": {
    "zh-CN": "首扇铰链侧"
  },
  "valueType": "enum",
  "defaultValue": "left",
  "group": "door",
  "choices": [
    {
      "value": "left",
      "displayName": {
        "zh-CN": "左"
      }
    },
    {
      "value": "right",
      "displayName": {
        "zh-CN": "右"
      }
    }
  ]
}
```

### pattern — 装饰模块

```json
{
  "key": "pattern",
  "displayName": {
    "zh-CN": "装饰模块"
  },
  "valueType": "enum",
  "defaultValue": "lines",
  "group": "design",
  "choices": [
    {
      "value": "lines",
      "displayName": {
        "zh-CN": "铣线／格栅"
      }
    },
    {
      "value": "diamond",
      "displayName": {
        "zh-CN": "菱形花格"
      }
    },
    {
      "value": "octagon",
      "displayName": {
        "zh-CN": "八角花格"
      }
    },
    {
      "value": "round_scene",
      "displayName": {
        "zh-CN": "几何圆景"
      }
    },
    {
      "value": "panels",
      "displayName": {
        "zh-CN": "直壁池板／扣线"
      }
    },
    {
      "value": "glass_lattice",
      "displayName": {
        "zh-CN": "玻璃门花"
      }
    }
  ]
}
```

### composition — 多扇构图

```json
{
  "key": "composition",
  "displayName": {
    "zh-CN": "多扇构图"
  },
  "valueType": "enum",
  "defaultValue": "mirror",
  "group": "design",
  "choices": [
    {
      "value": "repeat",
      "displayName": {
        "zh-CN": "各扇独立重复"
      }
    },
    {
      "value": "mirror",
      "displayName": {
        "zh-CN": "左右交替镜像"
      }
    },
    {
      "value": "continuous",
      "displayName": {
        "zh-CN": "整幅连续构图"
      }
    }
  ],
  "visibleWhen": {
    "op": "ne",
    "parameter": "doorType",
    "value": "single"
  }
}
```

### layout — 门面版式

```json
{
  "key": "layout",
  "displayName": {
    "zh-CN": "门面版式"
  },
  "valueType": "enum",
  "defaultValue": "full",
  "group": "design",
  "choices": [
    {
      "value": "full",
      "displayName": {
        "zh-CN": "满堂"
      }
    },
    {
      "value": "center_band",
      "displayName": {
        "zh-CN": "中轴竖带"
      }
    },
    {
      "value": "offset_band",
      "displayName": {
        "zh-CN": "偏轴竖带"
      }
    },
    {
      "value": "upper",
      "displayName": {
        "zh-CN": "上花下板"
      }
    },
    {
      "value": "lower",
      "displayName": {
        "zh-CN": "上板下花"
      }
    },
    {
      "value": "two_blocks",
      "displayName": {
        "zh-CN": "上下双区"
      }
    }
  ]
}
```

### bandRatio — 竖带宽度比例

```json
{
  "key": "bandRatio",
  "displayName": {
    "zh-CN": "竖带宽度比例"
  },
  "valueType": "number",
  "defaultValue": 0.4,
  "group": "design",
  "constraints": {
    "minimum": 0.1,
    "maximum": 0.9
  },
  "visibleWhen": {
    "op": "any",
    "conditions": [
      {
        "op": "eq",
        "parameter": "layout",
        "value": "center_band"
      },
      {
        "op": "eq",
        "parameter": "layout",
        "value": "offset_band"
      }
    ]
  }
}
```

### bandOffset — 竖带位置比例

```json
{
  "key": "bandOffset",
  "displayName": {
    "zh-CN": "竖带位置比例"
  },
  "valueType": "number",
  "defaultValue": 0.25,
  "group": "design",
  "constraints": {
    "minimum": 0,
    "maximum": 1
  },
  "visibleWhen": {
    "op": "eq",
    "parameter": "layout",
    "value": "offset_band"
  }
}
```

### arrayMode — 线条排列

```json
{
  "key": "arrayMode",
  "displayName": {
    "zh-CN": "线条排列"
  },
  "valueType": "enum",
  "defaultValue": "count",
  "group": "array",
  "choices": [
    {
      "value": "count",
      "displayName": {
        "zh-CN": "指定数量"
      }
    },
    {
      "value": "pitch",
      "displayName": {
        "zh-CN": "指定中心距"
      }
    }
  ],
  "visibleWhen": {
    "op": "any",
    "conditions": [
      {
        "op": "eq",
        "parameter": "pattern",
        "value": "lines"
      }
    ]
  }
}
```

### lineCount — 线条数

```json
{
  "key": "lineCount",
  "displayName": {
    "zh-CN": "线条数"
  },
  "valueType": "integer",
  "defaultValue": 8,
  "group": "array",
  "constraints": {
    "minimum": 1,
    "maximum": 100
  },
  "visibleWhen": {
    "op": "all",
    "conditions": [
      {
        "op": "any",
        "conditions": [
          {
            "op": "eq",
            "parameter": "pattern",
            "value": "lines"
          }
        ]
      },
      {
        "op": "eq",
        "parameter": "arrayMode",
        "value": "count"
      }
    ]
  }
}
```

### linePitch — 线条中心距

```json
{
  "key": "linePitch",
  "displayName": {
    "zh-CN": "线条中心距"
  },
  "valueType": "number",
  "defaultValue": 70,
  "group": "array",
  "constraints": {
    "minimum": 5,
    "maximum": 1000
  },
  "quantity": "length",
  "unit": "mm",
  "visibleWhen": {
    "op": "all",
    "conditions": [
      {
        "op": "any",
        "conditions": [
          {
            "op": "eq",
            "parameter": "pattern",
            "value": "lines"
          }
        ]
      },
      {
        "op": "eq",
        "parameter": "arrayMode",
        "value": "pitch"
      }
    ]
  }
}
```

### lineAngle — 线条方向角

```json
{
  "key": "lineAngle",
  "displayName": {
    "zh-CN": "线条方向角"
  },
  "valueType": "number",
  "defaultValue": 90,
  "group": "array",
  "constraints": {
    "minimum": -90,
    "maximum": 90
  },
  "quantity": "angle",
  "unit": "deg",
  "visibleWhen": {
    "op": "any",
    "conditions": [
      {
        "op": "eq",
        "parameter": "pattern",
        "value": "lines"
      }
    ]
  }
}
```

### columns — 花格／池板列数

```json
{
  "key": "columns",
  "displayName": {
    "zh-CN": "花格／池板列数"
  },
  "valueType": "integer",
  "defaultValue": 2,
  "group": "array",
  "constraints": {
    "minimum": 1,
    "maximum": 12
  },
  "visibleWhen": {
    "op": "any",
    "conditions": [
      {
        "op": "eq",
        "parameter": "pattern",
        "value": "diamond"
      },
      {
        "op": "eq",
        "parameter": "pattern",
        "value": "octagon"
      },
      {
        "op": "eq",
        "parameter": "pattern",
        "value": "panels"
      },
      {
        "op": "eq",
        "parameter": "pattern",
        "value": "glass_lattice"
      }
    ]
  }
}
```

### rows — 花格／池板行数

```json
{
  "key": "rows",
  "displayName": {
    "zh-CN": "花格／池板行数"
  },
  "valueType": "integer",
  "defaultValue": 3,
  "group": "array",
  "constraints": {
    "minimum": 1,
    "maximum": 16
  },
  "visibleWhen": {
    "op": "any",
    "conditions": [
      {
        "op": "eq",
        "parameter": "pattern",
        "value": "diamond"
      },
      {
        "op": "eq",
        "parameter": "pattern",
        "value": "octagon"
      },
      {
        "op": "eq",
        "parameter": "pattern",
        "value": "panels"
      },
      {
        "op": "eq",
        "parameter": "pattern",
        "value": "glass_lattice"
      }
    ]
  }
}
```

### webWidth — 单元间保留宽度

```json
{
  "key": "webWidth",
  "displayName": {
    "zh-CN": "单元间保留宽度"
  },
  "valueType": "number",
  "defaultValue": 40,
  "group": "array",
  "constraints": {
    "minimum": 5,
    "maximum": 200
  },
  "quantity": "length",
  "unit": "mm",
  "visibleWhen": {
    "op": "any",
    "conditions": [
      {
        "op": "eq",
        "parameter": "pattern",
        "value": "diamond"
      },
      {
        "op": "eq",
        "parameter": "pattern",
        "value": "octagon"
      },
      {
        "op": "eq",
        "parameter": "pattern",
        "value": "panels"
      },
      {
        "op": "eq",
        "parameter": "pattern",
        "value": "glass_lattice"
      }
    ]
  }
}
```

### lineTool — 铣线截面

```json
{
  "key": "lineTool",
  "displayName": {
    "zh-CN": "铣线截面"
  },
  "valueType": "enum",
  "defaultValue": "flat",
  "group": "process",
  "choices": [
    {
      "value": "flat",
      "displayName": {
        "zh-CN": "平底槽"
      }
    },
    {
      "value": "v",
      "displayName": {
        "zh-CN": "V形槽"
      }
    }
  ],
  "visibleWhen": {
    "op": "any",
    "conditions": [
      {
        "op": "eq",
        "parameter": "pattern",
        "value": "lines"
      }
    ]
  }
}
```

### regionProcess — 花格处理

```json
{
  "key": "regionProcess",
  "displayName": {
    "zh-CN": "花格处理"
  },
  "valueType": "enum",
  "defaultValue": "through",
  "group": "process",
  "choices": [
    {
      "value": "through",
      "displayName": {
        "zh-CN": "镂空切穿"
      }
    },
    {
      "value": "recess",
      "displayName": {
        "zh-CN": "定深凹雕"
      }
    }
  ],
  "visibleWhen": {
    "op": "any",
    "conditions": [
      {
        "op": "eq",
        "parameter": "pattern",
        "value": "diamond"
      },
      {
        "op": "eq",
        "parameter": "pattern",
        "value": "octagon"
      }
    ]
  }
}
```

### machiningSide — 加工面

```json
{
  "key": "machiningSide",
  "displayName": {
    "zh-CN": "加工面"
  },
  "valueType": "enum",
  "defaultValue": "front",
  "group": "process",
  "choices": [
    {
      "value": "front",
      "displayName": {
        "zh-CN": "正面"
      }
    },
    {
      "value": "back",
      "displayName": {
        "zh-CN": "背面"
      }
    },
    {
      "value": "both",
      "displayName": {
        "zh-CN": "双面"
      }
    }
  ],
  "visibleWhen": {
    "op": "any",
    "conditions": [
      {
        "op": "any",
        "conditions": [
          {
            "op": "eq",
            "parameter": "pattern",
            "value": "lines"
          },
          {
            "op": "eq",
            "parameter": "pattern",
            "value": "round_scene"
          },
          {
            "op": "eq",
            "parameter": "pattern",
            "value": "panels"
          }
        ]
      },
      {
        "op": "all",
        "conditions": [
          {
            "op": "any",
            "conditions": [
              {
                "op": "eq",
                "parameter": "pattern",
                "value": "diamond"
              },
              {
                "op": "eq",
                "parameter": "pattern",
                "value": "octagon"
              }
            ]
          },
          {
            "op": "eq",
            "parameter": "regionProcess",
            "value": "recess"
          }
        ]
      }
    ]
  }
}
```

### cutDepth — 单面加工深度

```json
{
  "key": "cutDepth",
  "displayName": {
    "zh-CN": "单面加工深度"
  },
  "valueType": "number",
  "defaultValue": 6,
  "group": "process",
  "constraints": {
    "minimum": 0.2,
    "maximum": 50
  },
  "quantity": "length",
  "unit": "mm",
  "visibleWhen": {
    "op": "any",
    "conditions": [
      {
        "op": "any",
        "conditions": [
          {
            "op": "eq",
            "parameter": "pattern",
            "value": "lines"
          },
          {
            "op": "eq",
            "parameter": "pattern",
            "value": "round_scene"
          },
          {
            "op": "eq",
            "parameter": "pattern",
            "value": "panels"
          }
        ]
      },
      {
        "op": "all",
        "conditions": [
          {
            "op": "any",
            "conditions": [
              {
                "op": "eq",
                "parameter": "pattern",
                "value": "diamond"
              },
              {
                "op": "eq",
                "parameter": "pattern",
                "value": "octagon"
              }
            ]
          },
          {
            "op": "eq",
            "parameter": "regionProcess",
            "value": "recess"
          }
        ]
      }
    ]
  }
}
```

### grooveWidth — 平底槽宽

```json
{
  "key": "grooveWidth",
  "displayName": {
    "zh-CN": "平底槽宽"
  },
  "valueType": "number",
  "defaultValue": 8,
  "group": "process",
  "constraints": {
    "minimum": 1,
    "maximum": 100
  },
  "quantity": "length",
  "unit": "mm",
  "visibleWhen": {
    "op": "any",
    "conditions": [
      {
        "op": "any",
        "conditions": [
          {
            "op": "eq",
            "parameter": "pattern",
            "value": "round_scene"
          }
        ]
      },
      {
        "op": "all",
        "conditions": [
          {
            "op": "any",
            "conditions": [
              {
                "op": "eq",
                "parameter": "pattern",
                "value": "lines"
              }
            ]
          },
          {
            "op": "eq",
            "parameter": "lineTool",
            "value": "flat"
          }
        ]
      }
    ]
  }
}
```

### vAngle — V刀夹角

```json
{
  "key": "vAngle",
  "displayName": {
    "zh-CN": "V刀夹角"
  },
  "valueType": "number",
  "defaultValue": 90,
  "group": "process",
  "constraints": {
    "minimum": 30,
    "maximum": 120
  },
  "quantity": "angle",
  "unit": "deg",
  "visibleWhen": {
    "op": "all",
    "conditions": [
      {
        "op": "any",
        "conditions": [
          {
            "op": "eq",
            "parameter": "pattern",
            "value": "lines"
          }
        ]
      },
      {
        "op": "eq",
        "parameter": "lineTool",
        "value": "v"
      }
    ]
  }
}
```

### minSkin — 最小剩余板厚

```json
{
  "key": "minSkin",
  "displayName": {
    "zh-CN": "最小剩余板厚"
  },
  "valueType": "number",
  "defaultValue": 8,
  "group": "safe",
  "constraints": {
    "minimum": 1,
    "maximum": 80
  },
  "quantity": "length",
  "unit": "mm",
  "visibleWhen": {
    "op": "any",
    "conditions": [
      {
        "op": "any",
        "conditions": [
          {
            "op": "eq",
            "parameter": "pattern",
            "value": "lines"
          },
          {
            "op": "eq",
            "parameter": "pattern",
            "value": "round_scene"
          },
          {
            "op": "eq",
            "parameter": "pattern",
            "value": "panels"
          }
        ]
      },
      {
        "op": "all",
        "conditions": [
          {
            "op": "any",
            "conditions": [
              {
                "op": "eq",
                "parameter": "pattern",
                "value": "diamond"
              },
              {
                "op": "eq",
                "parameter": "pattern",
                "value": "octagon"
              }
            ]
          },
          {
            "op": "eq",
            "parameter": "regionProcess",
            "value": "recess"
          }
        ]
      }
    ]
  }
}
```

### trimEnabled — 池板独立扣线

```json
{
  "key": "trimEnabled",
  "displayName": {
    "zh-CN": "池板独立扣线"
  },
  "valueType": "boolean",
  "defaultValue": true,
  "group": "process",
  "visibleWhen": {
    "op": "any",
    "conditions": [
      {
        "op": "eq",
        "parameter": "pattern",
        "value": "panels"
      }
    ]
  }
}
```

### trimWidth — 扣线宽度

```json
{
  "key": "trimWidth",
  "displayName": {
    "zh-CN": "扣线宽度"
  },
  "valueType": "number",
  "defaultValue": 10,
  "group": "process",
  "constraints": {
    "minimum": 2,
    "maximum": 50
  },
  "quantity": "length",
  "unit": "mm",
  "visibleWhen": {
    "op": "all",
    "conditions": [
      {
        "op": "any",
        "conditions": [
          {
            "op": "eq",
            "parameter": "pattern",
            "value": "panels"
          }
        ]
      },
      {
        "op": "eq",
        "parameter": "trimEnabled",
        "value": true
      }
    ]
  }
}
```

### trimHeight — 扣线凸出高度

```json
{
  "key": "trimHeight",
  "displayName": {
    "zh-CN": "扣线凸出高度"
  },
  "valueType": "number",
  "defaultValue": 5,
  "group": "process",
  "constraints": {
    "minimum": 1,
    "maximum": 30
  },
  "quantity": "length",
  "unit": "mm",
  "visibleWhen": {
    "op": "all",
    "conditions": [
      {
        "op": "any",
        "conditions": [
          {
            "op": "eq",
            "parameter": "pattern",
            "value": "panels"
          }
        ]
      },
      {
        "op": "eq",
        "parameter": "trimEnabled",
        "value": true
      }
    ]
  }
}
```

### glassThickness — 背衬玻璃厚度

```json
{
  "key": "glassThickness",
  "displayName": {
    "zh-CN": "背衬玻璃厚度"
  },
  "valueType": "number",
  "defaultValue": 5,
  "group": "material",
  "constraints": {
    "minimum": 2,
    "maximum": 20
  },
  "quantity": "length",
  "unit": "mm",
  "visibleWhen": {
    "op": "any",
    "conditions": [
      {
        "op": "eq",
        "parameter": "pattern",
        "value": "glass_lattice"
      }
    ]
  }
}
```

### glassGap — 玻璃与门板间距

```json
{
  "key": "glassGap",
  "displayName": {
    "zh-CN": "玻璃与门板间距"
  },
  "valueType": "number",
  "defaultValue": 2,
  "group": "process",
  "constraints": {
    "minimum": 0.5,
    "maximum": 20
  },
  "quantity": "length",
  "unit": "mm",
  "visibleWhen": {
    "op": "any",
    "conditions": [
      {
        "op": "eq",
        "parameter": "pattern",
        "value": "glass_lattice"
      }
    ]
  }
}
```

### border — 四周保留宽度

```json
{
  "key": "border",
  "displayName": {
    "zh-CN": "四周保留宽度"
  },
  "valueType": "number",
  "defaultValue": 100,
  "group": "safe",
  "constraints": {
    "minimum": 10,
    "maximum": 400
  },
  "quantity": "length",
  "unit": "mm"
}
```

### meetingBorder — 连续构图拼缝侧保留

```json
{
  "key": "meetingBorder",
  "displayName": {
    "zh-CN": "连续构图拼缝侧保留"
  },
  "valueType": "number",
  "defaultValue": 35,
  "group": "safe",
  "constraints": {
    "minimum": 10,
    "maximum": 200
  },
  "quantity": "length",
  "unit": "mm",
  "visibleWhen": {
    "op": "all",
    "conditions": [
      {
        "op": "ne",
        "parameter": "doorType",
        "value": "single"
      },
      {
        "op": "eq",
        "parameter": "composition",
        "value": "continuous"
      }
    ]
  }
}
```

### protectHardware — 保留锁／把手和铰链区

```json
{
  "key": "protectHardware",
  "displayName": {
    "zh-CN": "保留锁／把手和铰链区"
  },
  "valueType": "boolean",
  "defaultValue": true,
  "group": "safe"
}
```

### lockHeight — 锁区中心高度

```json
{
  "key": "lockHeight",
  "displayName": {
    "zh-CN": "锁区中心高度"
  },
  "valueType": "number",
  "defaultValue": 1000,
  "group": "safe",
  "constraints": {
    "minimum": 100,
    "maximum": 3500
  },
  "quantity": "length",
  "unit": "mm",
  "visibleWhen": {
    "op": "eq",
    "parameter": "protectHardware",
    "value": true
  }
}
```

### lockWidth — 锁／把手保留区宽

```json
{
  "key": "lockWidth",
  "displayName": {
    "zh-CN": "锁／把手保留区宽"
  },
  "valueType": "number",
  "defaultValue": 100,
  "group": "safe",
  "constraints": {
    "minimum": 20,
    "maximum": 300
  },
  "quantity": "length",
  "unit": "mm",
  "visibleWhen": {
    "op": "eq",
    "parameter": "protectHardware",
    "value": true
  }
}
```

### lockLength — 锁／把手保留区高

```json
{
  "key": "lockLength",
  "displayName": {
    "zh-CN": "锁／把手保留区高"
  },
  "valueType": "number",
  "defaultValue": 300,
  "group": "safe",
  "constraints": {
    "minimum": 50,
    "maximum": 700
  },
  "quantity": "length",
  "unit": "mm",
  "visibleWhen": {
    "op": "eq",
    "parameter": "protectHardware",
    "value": true
  }
}
```

### hingeWidth — 铰链保留区宽

```json
{
  "key": "hingeWidth",
  "displayName": {
    "zh-CN": "铰链保留区宽"
  },
  "valueType": "number",
  "defaultValue": 80,
  "group": "safe",
  "constraints": {
    "minimum": 20,
    "maximum": 300
  },
  "quantity": "length",
  "unit": "mm",
  "visibleWhen": {
    "op": "eq",
    "parameter": "protectHardware",
    "value": true
  }
}
```

### hingeLength — 单个铰链保留区高

```json
{
  "key": "hingeLength",
  "displayName": {
    "zh-CN": "单个铰链保留区高"
  },
  "valueType": "number",
  "defaultValue": 140,
  "group": "safe",
  "constraints": {
    "minimum": 40,
    "maximum": 300
  },
  "quantity": "length",
  "unit": "mm",
  "visibleWhen": {
    "op": "eq",
    "parameter": "protectHardware",
    "value": true
  }
}
```

### material — 门板材料

```json
{
  "key": "material",
  "displayName": {
    "zh-CN": "门板材料"
  },
  "valueType": "string",
  "defaultValue": "铝板／复合门板（按工程选型）",
  "group": "material"
}
```

### finish — 表面处理备注

```json
{
  "key": "finish",
  "displayName": {
    "zh-CN": "表面处理备注"
  },
  "valueType": "string",
  "defaultValue": "按工程指定",
  "group": "material"
}
```

### glassType — 背衬玻璃类型

```json
{
  "key": "glassType",
  "displayName": {
    "zh-CN": "背衬玻璃类型"
  },
  "valueType": "string",
  "defaultValue": "按工程选型",
  "group": "material",
  "visibleWhen": {
    "op": "any",
    "conditions": [
      {
        "op": "eq",
        "parameter": "pattern",
        "value": "glass_lattice"
      }
    ]
  }
}
```
