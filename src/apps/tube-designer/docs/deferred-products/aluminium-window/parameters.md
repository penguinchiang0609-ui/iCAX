# 普通铝合金窗参数声明附录

归档模板 `aluminium-window` / `1.1.0`，共 35 项。
以下从 reference/template.json 原声明生成；默认值不是推荐工程值。未声明约束不表示没有脚本几何约束。
显隐、禁用和选择条件的结构原样保留，供共享 parameterConditions.mjs 解释，不另造条件解释器。

| 键 | 名称 | 类型 | 默认值 | 分组 | 单位 |
| --- | --- | --- | --- | --- | --- |
| systemSource | 型材系统来源 | enum | "demonstration" | system |  |
| systemFile | 系列文件绝对路径 | string | "" | system |  |
| productCode | 产品编号 | string | "LC-001" | basic |  |
| width | 总宽 W | number | 1500 | basic | mm |
| height | 总高 H | number | 1500 | basic | mm |
| windowType | 窗型 | enum | "sliding" | basic |  |
| assemblyPlanningMode | 装配工艺来源 | enum | "builtin_rules" | basic |  |
| columns | 左右分格数 | integer | 1 | grid |  |
| rows | 上下分格数 | integer | 1 | grid |  |
| mergeTopLight | 顶部合并为通长固定亮 | boolean | false | grid |  |
| columnWeight1 | 第1列净宽比例 | number | 1 | grid |  |
| rowWeight1 | 第1行净高比例 | number | 1 | grid |  |
| columnWeight2 | 第2列净宽比例 | number | 1 | grid |  |
| rowWeight2 | 第2行净高比例 | number | 1 | grid |  |
| columnWeight3 | 第3列净宽比例 | number | 1 | grid |  |
| rowWeight3 | 第3行净高比例 | number | 1 | grid |  |
| cell11 | 第1行第1列 | enum | "sliding" | cells |  |
| cell12 | 第1行第2列 | enum | "fixed" | cells |  |
| cell13 | 第1行第3列 | enum | "fixed" | cells |  |
| cell21 | 第2行第1列 | enum | "fixed" | cells |  |
| cell22 | 第2行第2列 | enum | "fixed" | cells |  |
| cell23 | 第2行第3列 | enum | "fixed" | cells |  |
| cell31 | 第3行第1列 | enum | "fixed" | cells |  |
| cell32 | 第3行第2列 | enum | "fixed" | cells |  |
| cell33 | 第3行第3列 | enum | "fixed" | cells |  |
| slidingCount | 每洞口玻璃扇数 | integer | 2 | sliding |  |
| trackCount | 轨道数（含纱窗轨） | integer | 3 | sliding |  |
| screenEnabled | 配置滑动纱窗 | boolean | true | sliding |  |
| screenCount | 每洞口滑动纱窗数 | integer | 1 | sliding |  |
| hingedCount | 每洞口平开扇数 | integer | 1 | hinged |  |
| openingSide | 单扇合页侧 | enum | "left" | hinged |  |
| openingDirection | 开启方向 | enum | "outward" | hinged |  |
| glassThickness | 玻璃厚度 | number | 5 | fillings | mm |
| glassType | 玻璃类型 | string | "待工程选型" | fillings |  |
| meshType | 纱网类型 | string | "待工程选型" | fillings |  |

## 完整声明

每段包含原参数的全部字段：choices 的值和名称、constraints、visibleWhen、enabledWhen、readOnly、presentation、description 等（以实际存在字段为准）。

### systemSource — 型材系统来源

```json
{
  "key": "systemSource",
  "displayName": {
    "zh-CN": "型材系统来源"
  },
  "valueType": "enum",
  "defaultValue": "demonstration",
  "group": "system",
  "choices": [
    {
      "value": "demonstration",
      "displayName": {
        "zh-CN": "结构演示（禁止生产）"
      }
    },
    {
      "value": "file",
      "displayName": {
        "zh-CN": "厂家系列 JSON 文件"
      }
    }
  ]
}
```

### systemFile — 系列文件绝对路径

```json
{
  "key": "systemFile",
  "displayName": {
    "zh-CN": "系列文件绝对路径"
  },
  "valueType": "string",
  "defaultValue": "",
  "group": "system",
  "visibleWhen": {
    "op": "eq",
    "parameter": "systemSource",
    "value": "file"
  }
}
```

### productCode — 产品编号

```json
{
  "key": "productCode",
  "displayName": {
    "zh-CN": "产品编号"
  },
  "valueType": "string",
  "defaultValue": "LC-001",
  "group": "basic"
}
```

### width — 总宽 W

```json
{
  "key": "width",
  "displayName": {
    "zh-CN": "总宽 W"
  },
  "valueType": "number",
  "defaultValue": 1500,
  "group": "basic",
  "constraints": {
    "minimum": 100,
    "maximum": 10000
  },
  "unit": "mm",
  "quantity": "length"
}
```

### height — 总高 H

```json
{
  "key": "height",
  "displayName": {
    "zh-CN": "总高 H"
  },
  "valueType": "number",
  "defaultValue": 1500,
  "group": "basic",
  "constraints": {
    "minimum": 100,
    "maximum": 10000
  },
  "unit": "mm",
  "quantity": "length"
}
```

### windowType — 窗型

```json
{
  "key": "windowType",
  "displayName": {
    "zh-CN": "窗型"
  },
  "valueType": "enum",
  "defaultValue": "sliding",
  "group": "basic",
  "choices": [
    {
      "value": "fixed",
      "displayName": {
        "zh-CN": "固定玻璃"
      }
    },
    {
      "value": "sliding",
      "displayName": {
        "zh-CN": "推拉窗"
      }
    },
    {
      "value": "hinged",
      "displayName": {
        "zh-CN": "平开窗"
      }
    },
    {
      "value": "mixed",
      "displayName": {
        "zh-CN": "组合窗（逐格配置）"
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
  "presentation": {
    "visible": false
  },
  "valueType": "enum",
  "defaultValue": "builtin_rules",
  "group": "basic",
  "choices": [
    {
      "value": "builtin_rules",
      "displayName": {
        "zh-CN": "型材系列内置加工规则"
      }
    },
    {
      "value": "external_templates",
      "displayName": {
        "zh-CN": "按连接节点选择装配工艺模板"
      }
    }
  ],
  "description": "独立工艺模式保留方头原管并输出框角与中梃、横梃连接节点。节点工艺与整窗适配通过验证前不能生产下料。"
}
```

### columns — 左右分格数

```json
{
  "key": "columns",
  "displayName": {
    "zh-CN": "左右分格数"
  },
  "valueType": "integer",
  "defaultValue": 1,
  "group": "grid",
  "constraints": {
    "minimum": 1,
    "maximum": 3
  }
}
```

### rows — 上下分格数

```json
{
  "key": "rows",
  "displayName": {
    "zh-CN": "上下分格数"
  },
  "valueType": "integer",
  "defaultValue": 1,
  "group": "grid",
  "constraints": {
    "minimum": 1,
    "maximum": 3
  }
}
```

### mergeTopLight — 顶部合并为通长固定亮

```json
{
  "key": "mergeTopLight",
  "displayName": {
    "zh-CN": "顶部合并为通长固定亮"
  },
  "valueType": "boolean",
  "defaultValue": false,
  "group": "grid",
  "visibleWhen": {
    "op": "all",
    "conditions": [
      {
        "op": "any",
        "conditions": [
          {
            "op": "eq",
            "parameter": "columns",
            "value": 2
          },
          {
            "op": "eq",
            "parameter": "columns",
            "value": 3
          }
        ]
      },
      {
        "op": "any",
        "conditions": [
          {
            "op": "eq",
            "parameter": "rows",
            "value": 2
          },
          {
            "op": "eq",
            "parameter": "rows",
            "value": 3
          }
        ]
      }
    ]
  }
}
```

### columnWeight1 — 第1列净宽比例

```json
{
  "key": "columnWeight1",
  "displayName": {
    "zh-CN": "第1列净宽比例"
  },
  "valueType": "number",
  "defaultValue": 1,
  "group": "grid",
  "constraints": {
    "minimum": 0.01,
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
            "parameter": "columns",
            "value": 2
          },
          {
            "op": "eq",
            "parameter": "columns",
            "value": 3
          }
        ]
      },
      {
        "op": "any",
        "conditions": [
          {
            "op": "eq",
            "parameter": "columns",
            "value": 1
          },
          {
            "op": "eq",
            "parameter": "columns",
            "value": 2
          },
          {
            "op": "eq",
            "parameter": "columns",
            "value": 3
          }
        ]
      }
    ]
  }
}
```

### rowWeight1 — 第1行净高比例

```json
{
  "key": "rowWeight1",
  "displayName": {
    "zh-CN": "第1行净高比例"
  },
  "valueType": "number",
  "defaultValue": 1,
  "group": "grid",
  "constraints": {
    "minimum": 0.01,
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
            "parameter": "rows",
            "value": 2
          },
          {
            "op": "eq",
            "parameter": "rows",
            "value": 3
          }
        ]
      },
      {
        "op": "any",
        "conditions": [
          {
            "op": "eq",
            "parameter": "rows",
            "value": 1
          },
          {
            "op": "eq",
            "parameter": "rows",
            "value": 2
          },
          {
            "op": "eq",
            "parameter": "rows",
            "value": 3
          }
        ]
      }
    ]
  }
}
```

### columnWeight2 — 第2列净宽比例

```json
{
  "key": "columnWeight2",
  "displayName": {
    "zh-CN": "第2列净宽比例"
  },
  "valueType": "number",
  "defaultValue": 1,
  "group": "grid",
  "constraints": {
    "minimum": 0.01,
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
            "parameter": "columns",
            "value": 2
          },
          {
            "op": "eq",
            "parameter": "columns",
            "value": 3
          }
        ]
      },
      {
        "op": "any",
        "conditions": [
          {
            "op": "eq",
            "parameter": "columns",
            "value": 2
          },
          {
            "op": "eq",
            "parameter": "columns",
            "value": 3
          }
        ]
      }
    ]
  }
}
```

### rowWeight2 — 第2行净高比例

```json
{
  "key": "rowWeight2",
  "displayName": {
    "zh-CN": "第2行净高比例"
  },
  "valueType": "number",
  "defaultValue": 1,
  "group": "grid",
  "constraints": {
    "minimum": 0.01,
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
            "parameter": "rows",
            "value": 2
          },
          {
            "op": "eq",
            "parameter": "rows",
            "value": 3
          }
        ]
      },
      {
        "op": "any",
        "conditions": [
          {
            "op": "eq",
            "parameter": "rows",
            "value": 2
          },
          {
            "op": "eq",
            "parameter": "rows",
            "value": 3
          }
        ]
      }
    ]
  }
}
```

### columnWeight3 — 第3列净宽比例

```json
{
  "key": "columnWeight3",
  "displayName": {
    "zh-CN": "第3列净宽比例"
  },
  "valueType": "number",
  "defaultValue": 1,
  "group": "grid",
  "constraints": {
    "minimum": 0.01,
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
            "parameter": "columns",
            "value": 2
          },
          {
            "op": "eq",
            "parameter": "columns",
            "value": 3
          }
        ]
      },
      {
        "op": "any",
        "conditions": [
          {
            "op": "eq",
            "parameter": "columns",
            "value": 3
          }
        ]
      }
    ]
  }
}
```

### rowWeight3 — 第3行净高比例

```json
{
  "key": "rowWeight3",
  "displayName": {
    "zh-CN": "第3行净高比例"
  },
  "valueType": "number",
  "defaultValue": 1,
  "group": "grid",
  "constraints": {
    "minimum": 0.01,
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
            "parameter": "rows",
            "value": 2
          },
          {
            "op": "eq",
            "parameter": "rows",
            "value": 3
          }
        ]
      },
      {
        "op": "any",
        "conditions": [
          {
            "op": "eq",
            "parameter": "rows",
            "value": 3
          }
        ]
      }
    ]
  }
}
```

### cell11 — 第1行第1列

```json
{
  "key": "cell11",
  "displayName": {
    "zh-CN": "第1行第1列"
  },
  "valueType": "enum",
  "defaultValue": "sliding",
  "group": "cells",
  "choices": [
    {
      "value": "fixed",
      "displayName": {
        "zh-CN": "固定玻璃"
      }
    },
    {
      "value": "sliding",
      "displayName": {
        "zh-CN": "推拉窗"
      }
    },
    {
      "value": "hinged",
      "displayName": {
        "zh-CN": "平开窗"
      }
    }
  ],
  "visibleWhen": {
    "op": "all",
    "conditions": [
      {
        "op": "all",
        "conditions": [
          {
            "op": "eq",
            "parameter": "windowType",
            "value": "mixed"
          },
          {
            "op": "any",
            "conditions": [
              {
                "op": "eq",
                "parameter": "rows",
                "value": 1
              },
              {
                "op": "eq",
                "parameter": "rows",
                "value": 2
              },
              {
                "op": "eq",
                "parameter": "rows",
                "value": 3
              }
            ]
          },
          {
            "op": "any",
            "conditions": [
              {
                "op": "eq",
                "parameter": "columns",
                "value": 1
              },
              {
                "op": "eq",
                "parameter": "columns",
                "value": 2
              },
              {
                "op": "eq",
                "parameter": "columns",
                "value": 3
              }
            ]
          }
        ]
      },
      {
        "op": "not",
        "condition": {
          "op": "all",
          "conditions": [
            {
              "op": "all",
              "conditions": [
                {
                  "op": "eq",
                  "parameter": "mergeTopLight",
                  "value": true
                },
                {
                  "op": "any",
                  "conditions": [
                    {
                      "op": "eq",
                      "parameter": "columns",
                      "value": 2
                    },
                    {
                      "op": "eq",
                      "parameter": "columns",
                      "value": 3
                    }
                  ]
                },
                {
                  "op": "any",
                  "conditions": [
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 2
                    },
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 3
                    }
                  ]
                }
              ]
            },
            {
              "op": "eq",
              "parameter": "rows",
              "value": 1
            }
          ]
        }
      }
    ]
  }
}
```

### cell12 — 第1行第2列

```json
{
  "key": "cell12",
  "displayName": {
    "zh-CN": "第1行第2列"
  },
  "valueType": "enum",
  "defaultValue": "fixed",
  "group": "cells",
  "choices": [
    {
      "value": "fixed",
      "displayName": {
        "zh-CN": "固定玻璃"
      }
    },
    {
      "value": "sliding",
      "displayName": {
        "zh-CN": "推拉窗"
      }
    },
    {
      "value": "hinged",
      "displayName": {
        "zh-CN": "平开窗"
      }
    }
  ],
  "visibleWhen": {
    "op": "all",
    "conditions": [
      {
        "op": "all",
        "conditions": [
          {
            "op": "eq",
            "parameter": "windowType",
            "value": "mixed"
          },
          {
            "op": "any",
            "conditions": [
              {
                "op": "eq",
                "parameter": "rows",
                "value": 1
              },
              {
                "op": "eq",
                "parameter": "rows",
                "value": 2
              },
              {
                "op": "eq",
                "parameter": "rows",
                "value": 3
              }
            ]
          },
          {
            "op": "any",
            "conditions": [
              {
                "op": "eq",
                "parameter": "columns",
                "value": 2
              },
              {
                "op": "eq",
                "parameter": "columns",
                "value": 3
              }
            ]
          }
        ]
      },
      {
        "op": "not",
        "condition": {
          "op": "all",
          "conditions": [
            {
              "op": "all",
              "conditions": [
                {
                  "op": "eq",
                  "parameter": "mergeTopLight",
                  "value": true
                },
                {
                  "op": "any",
                  "conditions": [
                    {
                      "op": "eq",
                      "parameter": "columns",
                      "value": 2
                    },
                    {
                      "op": "eq",
                      "parameter": "columns",
                      "value": 3
                    }
                  ]
                },
                {
                  "op": "any",
                  "conditions": [
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 2
                    },
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 3
                    }
                  ]
                }
              ]
            },
            {
              "op": "eq",
              "parameter": "rows",
              "value": 1
            }
          ]
        }
      }
    ]
  }
}
```

### cell13 — 第1行第3列

```json
{
  "key": "cell13",
  "displayName": {
    "zh-CN": "第1行第3列"
  },
  "valueType": "enum",
  "defaultValue": "fixed",
  "group": "cells",
  "choices": [
    {
      "value": "fixed",
      "displayName": {
        "zh-CN": "固定玻璃"
      }
    },
    {
      "value": "sliding",
      "displayName": {
        "zh-CN": "推拉窗"
      }
    },
    {
      "value": "hinged",
      "displayName": {
        "zh-CN": "平开窗"
      }
    }
  ],
  "visibleWhen": {
    "op": "all",
    "conditions": [
      {
        "op": "all",
        "conditions": [
          {
            "op": "eq",
            "parameter": "windowType",
            "value": "mixed"
          },
          {
            "op": "any",
            "conditions": [
              {
                "op": "eq",
                "parameter": "rows",
                "value": 1
              },
              {
                "op": "eq",
                "parameter": "rows",
                "value": 2
              },
              {
                "op": "eq",
                "parameter": "rows",
                "value": 3
              }
            ]
          },
          {
            "op": "any",
            "conditions": [
              {
                "op": "eq",
                "parameter": "columns",
                "value": 3
              }
            ]
          }
        ]
      },
      {
        "op": "not",
        "condition": {
          "op": "all",
          "conditions": [
            {
              "op": "all",
              "conditions": [
                {
                  "op": "eq",
                  "parameter": "mergeTopLight",
                  "value": true
                },
                {
                  "op": "any",
                  "conditions": [
                    {
                      "op": "eq",
                      "parameter": "columns",
                      "value": 2
                    },
                    {
                      "op": "eq",
                      "parameter": "columns",
                      "value": 3
                    }
                  ]
                },
                {
                  "op": "any",
                  "conditions": [
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 2
                    },
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 3
                    }
                  ]
                }
              ]
            },
            {
              "op": "eq",
              "parameter": "rows",
              "value": 1
            }
          ]
        }
      }
    ]
  }
}
```

### cell21 — 第2行第1列

```json
{
  "key": "cell21",
  "displayName": {
    "zh-CN": "第2行第1列"
  },
  "valueType": "enum",
  "defaultValue": "fixed",
  "group": "cells",
  "choices": [
    {
      "value": "fixed",
      "displayName": {
        "zh-CN": "固定玻璃"
      }
    },
    {
      "value": "sliding",
      "displayName": {
        "zh-CN": "推拉窗"
      }
    },
    {
      "value": "hinged",
      "displayName": {
        "zh-CN": "平开窗"
      }
    }
  ],
  "visibleWhen": {
    "op": "all",
    "conditions": [
      {
        "op": "all",
        "conditions": [
          {
            "op": "eq",
            "parameter": "windowType",
            "value": "mixed"
          },
          {
            "op": "any",
            "conditions": [
              {
                "op": "eq",
                "parameter": "rows",
                "value": 2
              },
              {
                "op": "eq",
                "parameter": "rows",
                "value": 3
              }
            ]
          },
          {
            "op": "any",
            "conditions": [
              {
                "op": "eq",
                "parameter": "columns",
                "value": 1
              },
              {
                "op": "eq",
                "parameter": "columns",
                "value": 2
              },
              {
                "op": "eq",
                "parameter": "columns",
                "value": 3
              }
            ]
          }
        ]
      },
      {
        "op": "not",
        "condition": {
          "op": "all",
          "conditions": [
            {
              "op": "all",
              "conditions": [
                {
                  "op": "eq",
                  "parameter": "mergeTopLight",
                  "value": true
                },
                {
                  "op": "any",
                  "conditions": [
                    {
                      "op": "eq",
                      "parameter": "columns",
                      "value": 2
                    },
                    {
                      "op": "eq",
                      "parameter": "columns",
                      "value": 3
                    }
                  ]
                },
                {
                  "op": "any",
                  "conditions": [
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 2
                    },
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 3
                    }
                  ]
                }
              ]
            },
            {
              "op": "eq",
              "parameter": "rows",
              "value": 2
            }
          ]
        }
      }
    ]
  }
}
```

### cell22 — 第2行第2列

```json
{
  "key": "cell22",
  "displayName": {
    "zh-CN": "第2行第2列"
  },
  "valueType": "enum",
  "defaultValue": "fixed",
  "group": "cells",
  "choices": [
    {
      "value": "fixed",
      "displayName": {
        "zh-CN": "固定玻璃"
      }
    },
    {
      "value": "sliding",
      "displayName": {
        "zh-CN": "推拉窗"
      }
    },
    {
      "value": "hinged",
      "displayName": {
        "zh-CN": "平开窗"
      }
    }
  ],
  "visibleWhen": {
    "op": "all",
    "conditions": [
      {
        "op": "all",
        "conditions": [
          {
            "op": "eq",
            "parameter": "windowType",
            "value": "mixed"
          },
          {
            "op": "any",
            "conditions": [
              {
                "op": "eq",
                "parameter": "rows",
                "value": 2
              },
              {
                "op": "eq",
                "parameter": "rows",
                "value": 3
              }
            ]
          },
          {
            "op": "any",
            "conditions": [
              {
                "op": "eq",
                "parameter": "columns",
                "value": 2
              },
              {
                "op": "eq",
                "parameter": "columns",
                "value": 3
              }
            ]
          }
        ]
      },
      {
        "op": "not",
        "condition": {
          "op": "all",
          "conditions": [
            {
              "op": "all",
              "conditions": [
                {
                  "op": "eq",
                  "parameter": "mergeTopLight",
                  "value": true
                },
                {
                  "op": "any",
                  "conditions": [
                    {
                      "op": "eq",
                      "parameter": "columns",
                      "value": 2
                    },
                    {
                      "op": "eq",
                      "parameter": "columns",
                      "value": 3
                    }
                  ]
                },
                {
                  "op": "any",
                  "conditions": [
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 2
                    },
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 3
                    }
                  ]
                }
              ]
            },
            {
              "op": "eq",
              "parameter": "rows",
              "value": 2
            }
          ]
        }
      }
    ]
  }
}
```

### cell23 — 第2行第3列

```json
{
  "key": "cell23",
  "displayName": {
    "zh-CN": "第2行第3列"
  },
  "valueType": "enum",
  "defaultValue": "fixed",
  "group": "cells",
  "choices": [
    {
      "value": "fixed",
      "displayName": {
        "zh-CN": "固定玻璃"
      }
    },
    {
      "value": "sliding",
      "displayName": {
        "zh-CN": "推拉窗"
      }
    },
    {
      "value": "hinged",
      "displayName": {
        "zh-CN": "平开窗"
      }
    }
  ],
  "visibleWhen": {
    "op": "all",
    "conditions": [
      {
        "op": "all",
        "conditions": [
          {
            "op": "eq",
            "parameter": "windowType",
            "value": "mixed"
          },
          {
            "op": "any",
            "conditions": [
              {
                "op": "eq",
                "parameter": "rows",
                "value": 2
              },
              {
                "op": "eq",
                "parameter": "rows",
                "value": 3
              }
            ]
          },
          {
            "op": "any",
            "conditions": [
              {
                "op": "eq",
                "parameter": "columns",
                "value": 3
              }
            ]
          }
        ]
      },
      {
        "op": "not",
        "condition": {
          "op": "all",
          "conditions": [
            {
              "op": "all",
              "conditions": [
                {
                  "op": "eq",
                  "parameter": "mergeTopLight",
                  "value": true
                },
                {
                  "op": "any",
                  "conditions": [
                    {
                      "op": "eq",
                      "parameter": "columns",
                      "value": 2
                    },
                    {
                      "op": "eq",
                      "parameter": "columns",
                      "value": 3
                    }
                  ]
                },
                {
                  "op": "any",
                  "conditions": [
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 2
                    },
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 3
                    }
                  ]
                }
              ]
            },
            {
              "op": "eq",
              "parameter": "rows",
              "value": 2
            }
          ]
        }
      }
    ]
  }
}
```

### cell31 — 第3行第1列

```json
{
  "key": "cell31",
  "displayName": {
    "zh-CN": "第3行第1列"
  },
  "valueType": "enum",
  "defaultValue": "fixed",
  "group": "cells",
  "choices": [
    {
      "value": "fixed",
      "displayName": {
        "zh-CN": "固定玻璃"
      }
    },
    {
      "value": "sliding",
      "displayName": {
        "zh-CN": "推拉窗"
      }
    },
    {
      "value": "hinged",
      "displayName": {
        "zh-CN": "平开窗"
      }
    }
  ],
  "visibleWhen": {
    "op": "all",
    "conditions": [
      {
        "op": "all",
        "conditions": [
          {
            "op": "eq",
            "parameter": "windowType",
            "value": "mixed"
          },
          {
            "op": "any",
            "conditions": [
              {
                "op": "eq",
                "parameter": "rows",
                "value": 3
              }
            ]
          },
          {
            "op": "any",
            "conditions": [
              {
                "op": "eq",
                "parameter": "columns",
                "value": 1
              },
              {
                "op": "eq",
                "parameter": "columns",
                "value": 2
              },
              {
                "op": "eq",
                "parameter": "columns",
                "value": 3
              }
            ]
          }
        ]
      },
      {
        "op": "not",
        "condition": {
          "op": "all",
          "conditions": [
            {
              "op": "all",
              "conditions": [
                {
                  "op": "eq",
                  "parameter": "mergeTopLight",
                  "value": true
                },
                {
                  "op": "any",
                  "conditions": [
                    {
                      "op": "eq",
                      "parameter": "columns",
                      "value": 2
                    },
                    {
                      "op": "eq",
                      "parameter": "columns",
                      "value": 3
                    }
                  ]
                },
                {
                  "op": "any",
                  "conditions": [
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 2
                    },
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 3
                    }
                  ]
                }
              ]
            },
            {
              "op": "eq",
              "parameter": "rows",
              "value": 3
            }
          ]
        }
      }
    ]
  }
}
```

### cell32 — 第3行第2列

```json
{
  "key": "cell32",
  "displayName": {
    "zh-CN": "第3行第2列"
  },
  "valueType": "enum",
  "defaultValue": "fixed",
  "group": "cells",
  "choices": [
    {
      "value": "fixed",
      "displayName": {
        "zh-CN": "固定玻璃"
      }
    },
    {
      "value": "sliding",
      "displayName": {
        "zh-CN": "推拉窗"
      }
    },
    {
      "value": "hinged",
      "displayName": {
        "zh-CN": "平开窗"
      }
    }
  ],
  "visibleWhen": {
    "op": "all",
    "conditions": [
      {
        "op": "all",
        "conditions": [
          {
            "op": "eq",
            "parameter": "windowType",
            "value": "mixed"
          },
          {
            "op": "any",
            "conditions": [
              {
                "op": "eq",
                "parameter": "rows",
                "value": 3
              }
            ]
          },
          {
            "op": "any",
            "conditions": [
              {
                "op": "eq",
                "parameter": "columns",
                "value": 2
              },
              {
                "op": "eq",
                "parameter": "columns",
                "value": 3
              }
            ]
          }
        ]
      },
      {
        "op": "not",
        "condition": {
          "op": "all",
          "conditions": [
            {
              "op": "all",
              "conditions": [
                {
                  "op": "eq",
                  "parameter": "mergeTopLight",
                  "value": true
                },
                {
                  "op": "any",
                  "conditions": [
                    {
                      "op": "eq",
                      "parameter": "columns",
                      "value": 2
                    },
                    {
                      "op": "eq",
                      "parameter": "columns",
                      "value": 3
                    }
                  ]
                },
                {
                  "op": "any",
                  "conditions": [
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 2
                    },
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 3
                    }
                  ]
                }
              ]
            },
            {
              "op": "eq",
              "parameter": "rows",
              "value": 3
            }
          ]
        }
      }
    ]
  }
}
```

### cell33 — 第3行第3列

```json
{
  "key": "cell33",
  "displayName": {
    "zh-CN": "第3行第3列"
  },
  "valueType": "enum",
  "defaultValue": "fixed",
  "group": "cells",
  "choices": [
    {
      "value": "fixed",
      "displayName": {
        "zh-CN": "固定玻璃"
      }
    },
    {
      "value": "sliding",
      "displayName": {
        "zh-CN": "推拉窗"
      }
    },
    {
      "value": "hinged",
      "displayName": {
        "zh-CN": "平开窗"
      }
    }
  ],
  "visibleWhen": {
    "op": "all",
    "conditions": [
      {
        "op": "all",
        "conditions": [
          {
            "op": "eq",
            "parameter": "windowType",
            "value": "mixed"
          },
          {
            "op": "any",
            "conditions": [
              {
                "op": "eq",
                "parameter": "rows",
                "value": 3
              }
            ]
          },
          {
            "op": "any",
            "conditions": [
              {
                "op": "eq",
                "parameter": "columns",
                "value": 3
              }
            ]
          }
        ]
      },
      {
        "op": "not",
        "condition": {
          "op": "all",
          "conditions": [
            {
              "op": "all",
              "conditions": [
                {
                  "op": "eq",
                  "parameter": "mergeTopLight",
                  "value": true
                },
                {
                  "op": "any",
                  "conditions": [
                    {
                      "op": "eq",
                      "parameter": "columns",
                      "value": 2
                    },
                    {
                      "op": "eq",
                      "parameter": "columns",
                      "value": 3
                    }
                  ]
                },
                {
                  "op": "any",
                  "conditions": [
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 2
                    },
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 3
                    }
                  ]
                }
              ]
            },
            {
              "op": "eq",
              "parameter": "rows",
              "value": 3
            }
          ]
        }
      }
    ]
  }
}
```

### slidingCount — 每洞口玻璃扇数

```json
{
  "key": "slidingCount",
  "displayName": {
    "zh-CN": "每洞口玻璃扇数"
  },
  "valueType": "integer",
  "defaultValue": 2,
  "group": "sliding",
  "constraints": {
    "minimum": 2,
    "maximum": 4
  },
  "visibleWhen": {
    "op": "any",
    "conditions": [
      {
        "op": "eq",
        "parameter": "windowType",
        "value": "sliding"
      },
      {
        "op": "all",
        "conditions": [
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "windowType",
                "value": "mixed"
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 1
                  },
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 2
                  },
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 3
                  }
                ]
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 1
                  },
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 2
                  },
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 3
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "cell11",
                "value": "sliding"
              },
              {
                "op": "not",
                "condition": {
                  "op": "all",
                  "conditions": [
                    {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "eq",
                          "parameter": "mergeTopLight",
                          "value": true
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 3
                            }
                          ]
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 3
                            }
                          ]
                        }
                      ]
                    },
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 1
                    }
                  ]
                }
              }
            ]
          }
        ]
      },
      {
        "op": "all",
        "conditions": [
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "windowType",
                "value": "mixed"
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 1
                  },
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 2
                  },
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 3
                  }
                ]
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 2
                  },
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 3
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "cell12",
                "value": "sliding"
              },
              {
                "op": "not",
                "condition": {
                  "op": "all",
                  "conditions": [
                    {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "eq",
                          "parameter": "mergeTopLight",
                          "value": true
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 3
                            }
                          ]
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 3
                            }
                          ]
                        }
                      ]
                    },
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 1
                    }
                  ]
                }
              }
            ]
          }
        ]
      },
      {
        "op": "all",
        "conditions": [
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "windowType",
                "value": "mixed"
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 1
                  },
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 2
                  },
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 3
                  }
                ]
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 3
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "cell13",
                "value": "sliding"
              },
              {
                "op": "not",
                "condition": {
                  "op": "all",
                  "conditions": [
                    {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "eq",
                          "parameter": "mergeTopLight",
                          "value": true
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 3
                            }
                          ]
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 3
                            }
                          ]
                        }
                      ]
                    },
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 1
                    }
                  ]
                }
              }
            ]
          }
        ]
      },
      {
        "op": "all",
        "conditions": [
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "windowType",
                "value": "mixed"
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 2
                  },
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 3
                  }
                ]
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 1
                  },
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 2
                  },
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 3
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "cell21",
                "value": "sliding"
              },
              {
                "op": "not",
                "condition": {
                  "op": "all",
                  "conditions": [
                    {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "eq",
                          "parameter": "mergeTopLight",
                          "value": true
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 3
                            }
                          ]
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 3
                            }
                          ]
                        }
                      ]
                    },
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 2
                    }
                  ]
                }
              }
            ]
          }
        ]
      },
      {
        "op": "all",
        "conditions": [
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "windowType",
                "value": "mixed"
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 2
                  },
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 3
                  }
                ]
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 2
                  },
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 3
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "cell22",
                "value": "sliding"
              },
              {
                "op": "not",
                "condition": {
                  "op": "all",
                  "conditions": [
                    {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "eq",
                          "parameter": "mergeTopLight",
                          "value": true
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 3
                            }
                          ]
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 3
                            }
                          ]
                        }
                      ]
                    },
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 2
                    }
                  ]
                }
              }
            ]
          }
        ]
      },
      {
        "op": "all",
        "conditions": [
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "windowType",
                "value": "mixed"
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 2
                  },
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 3
                  }
                ]
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 3
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "cell23",
                "value": "sliding"
              },
              {
                "op": "not",
                "condition": {
                  "op": "all",
                  "conditions": [
                    {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "eq",
                          "parameter": "mergeTopLight",
                          "value": true
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 3
                            }
                          ]
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 3
                            }
                          ]
                        }
                      ]
                    },
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 2
                    }
                  ]
                }
              }
            ]
          }
        ]
      },
      {
        "op": "all",
        "conditions": [
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "windowType",
                "value": "mixed"
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 3
                  }
                ]
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 1
                  },
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 2
                  },
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 3
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "cell31",
                "value": "sliding"
              },
              {
                "op": "not",
                "condition": {
                  "op": "all",
                  "conditions": [
                    {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "eq",
                          "parameter": "mergeTopLight",
                          "value": true
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 3
                            }
                          ]
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 3
                            }
                          ]
                        }
                      ]
                    },
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 3
                    }
                  ]
                }
              }
            ]
          }
        ]
      },
      {
        "op": "all",
        "conditions": [
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "windowType",
                "value": "mixed"
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 3
                  }
                ]
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 2
                  },
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 3
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "cell32",
                "value": "sliding"
              },
              {
                "op": "not",
                "condition": {
                  "op": "all",
                  "conditions": [
                    {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "eq",
                          "parameter": "mergeTopLight",
                          "value": true
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 3
                            }
                          ]
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 3
                            }
                          ]
                        }
                      ]
                    },
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 3
                    }
                  ]
                }
              }
            ]
          }
        ]
      },
      {
        "op": "all",
        "conditions": [
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "windowType",
                "value": "mixed"
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 3
                  }
                ]
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 3
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "cell33",
                "value": "sliding"
              },
              {
                "op": "not",
                "condition": {
                  "op": "all",
                  "conditions": [
                    {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "eq",
                          "parameter": "mergeTopLight",
                          "value": true
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 3
                            }
                          ]
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 3
                            }
                          ]
                        }
                      ]
                    },
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 3
                    }
                  ]
                }
              }
            ]
          }
        ]
      }
    ]
  }
}
```

### trackCount — 轨道数（含纱窗轨）

```json
{
  "key": "trackCount",
  "displayName": {
    "zh-CN": "轨道数（含纱窗轨）"
  },
  "valueType": "integer",
  "defaultValue": 3,
  "group": "sliding",
  "constraints": {
    "minimum": 2,
    "maximum": 4
  },
  "visibleWhen": {
    "op": "any",
    "conditions": [
      {
        "op": "eq",
        "parameter": "windowType",
        "value": "sliding"
      },
      {
        "op": "all",
        "conditions": [
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "windowType",
                "value": "mixed"
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 1
                  },
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 2
                  },
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 3
                  }
                ]
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 1
                  },
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 2
                  },
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 3
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "cell11",
                "value": "sliding"
              },
              {
                "op": "not",
                "condition": {
                  "op": "all",
                  "conditions": [
                    {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "eq",
                          "parameter": "mergeTopLight",
                          "value": true
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 3
                            }
                          ]
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 3
                            }
                          ]
                        }
                      ]
                    },
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 1
                    }
                  ]
                }
              }
            ]
          }
        ]
      },
      {
        "op": "all",
        "conditions": [
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "windowType",
                "value": "mixed"
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 1
                  },
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 2
                  },
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 3
                  }
                ]
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 2
                  },
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 3
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "cell12",
                "value": "sliding"
              },
              {
                "op": "not",
                "condition": {
                  "op": "all",
                  "conditions": [
                    {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "eq",
                          "parameter": "mergeTopLight",
                          "value": true
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 3
                            }
                          ]
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 3
                            }
                          ]
                        }
                      ]
                    },
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 1
                    }
                  ]
                }
              }
            ]
          }
        ]
      },
      {
        "op": "all",
        "conditions": [
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "windowType",
                "value": "mixed"
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 1
                  },
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 2
                  },
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 3
                  }
                ]
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 3
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "cell13",
                "value": "sliding"
              },
              {
                "op": "not",
                "condition": {
                  "op": "all",
                  "conditions": [
                    {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "eq",
                          "parameter": "mergeTopLight",
                          "value": true
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 3
                            }
                          ]
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 3
                            }
                          ]
                        }
                      ]
                    },
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 1
                    }
                  ]
                }
              }
            ]
          }
        ]
      },
      {
        "op": "all",
        "conditions": [
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "windowType",
                "value": "mixed"
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 2
                  },
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 3
                  }
                ]
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 1
                  },
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 2
                  },
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 3
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "cell21",
                "value": "sliding"
              },
              {
                "op": "not",
                "condition": {
                  "op": "all",
                  "conditions": [
                    {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "eq",
                          "parameter": "mergeTopLight",
                          "value": true
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 3
                            }
                          ]
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 3
                            }
                          ]
                        }
                      ]
                    },
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 2
                    }
                  ]
                }
              }
            ]
          }
        ]
      },
      {
        "op": "all",
        "conditions": [
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "windowType",
                "value": "mixed"
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 2
                  },
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 3
                  }
                ]
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 2
                  },
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 3
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "cell22",
                "value": "sliding"
              },
              {
                "op": "not",
                "condition": {
                  "op": "all",
                  "conditions": [
                    {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "eq",
                          "parameter": "mergeTopLight",
                          "value": true
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 3
                            }
                          ]
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 3
                            }
                          ]
                        }
                      ]
                    },
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 2
                    }
                  ]
                }
              }
            ]
          }
        ]
      },
      {
        "op": "all",
        "conditions": [
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "windowType",
                "value": "mixed"
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 2
                  },
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 3
                  }
                ]
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 3
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "cell23",
                "value": "sliding"
              },
              {
                "op": "not",
                "condition": {
                  "op": "all",
                  "conditions": [
                    {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "eq",
                          "parameter": "mergeTopLight",
                          "value": true
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 3
                            }
                          ]
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 3
                            }
                          ]
                        }
                      ]
                    },
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 2
                    }
                  ]
                }
              }
            ]
          }
        ]
      },
      {
        "op": "all",
        "conditions": [
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "windowType",
                "value": "mixed"
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 3
                  }
                ]
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 1
                  },
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 2
                  },
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 3
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "cell31",
                "value": "sliding"
              },
              {
                "op": "not",
                "condition": {
                  "op": "all",
                  "conditions": [
                    {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "eq",
                          "parameter": "mergeTopLight",
                          "value": true
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 3
                            }
                          ]
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 3
                            }
                          ]
                        }
                      ]
                    },
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 3
                    }
                  ]
                }
              }
            ]
          }
        ]
      },
      {
        "op": "all",
        "conditions": [
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "windowType",
                "value": "mixed"
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 3
                  }
                ]
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 2
                  },
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 3
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "cell32",
                "value": "sliding"
              },
              {
                "op": "not",
                "condition": {
                  "op": "all",
                  "conditions": [
                    {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "eq",
                          "parameter": "mergeTopLight",
                          "value": true
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 3
                            }
                          ]
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 3
                            }
                          ]
                        }
                      ]
                    },
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 3
                    }
                  ]
                }
              }
            ]
          }
        ]
      },
      {
        "op": "all",
        "conditions": [
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "windowType",
                "value": "mixed"
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 3
                  }
                ]
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 3
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "cell33",
                "value": "sliding"
              },
              {
                "op": "not",
                "condition": {
                  "op": "all",
                  "conditions": [
                    {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "eq",
                          "parameter": "mergeTopLight",
                          "value": true
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 3
                            }
                          ]
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 3
                            }
                          ]
                        }
                      ]
                    },
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 3
                    }
                  ]
                }
              }
            ]
          }
        ]
      }
    ]
  }
}
```

### screenEnabled — 配置滑动纱窗

```json
{
  "key": "screenEnabled",
  "displayName": {
    "zh-CN": "配置滑动纱窗"
  },
  "valueType": "boolean",
  "defaultValue": true,
  "group": "sliding",
  "visibleWhen": {
    "op": "any",
    "conditions": [
      {
        "op": "eq",
        "parameter": "windowType",
        "value": "sliding"
      },
      {
        "op": "all",
        "conditions": [
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "windowType",
                "value": "mixed"
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 1
                  },
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 2
                  },
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 3
                  }
                ]
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 1
                  },
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 2
                  },
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 3
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "cell11",
                "value": "sliding"
              },
              {
                "op": "not",
                "condition": {
                  "op": "all",
                  "conditions": [
                    {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "eq",
                          "parameter": "mergeTopLight",
                          "value": true
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 3
                            }
                          ]
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 3
                            }
                          ]
                        }
                      ]
                    },
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 1
                    }
                  ]
                }
              }
            ]
          }
        ]
      },
      {
        "op": "all",
        "conditions": [
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "windowType",
                "value": "mixed"
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 1
                  },
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 2
                  },
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 3
                  }
                ]
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 2
                  },
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 3
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "cell12",
                "value": "sliding"
              },
              {
                "op": "not",
                "condition": {
                  "op": "all",
                  "conditions": [
                    {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "eq",
                          "parameter": "mergeTopLight",
                          "value": true
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 3
                            }
                          ]
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 3
                            }
                          ]
                        }
                      ]
                    },
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 1
                    }
                  ]
                }
              }
            ]
          }
        ]
      },
      {
        "op": "all",
        "conditions": [
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "windowType",
                "value": "mixed"
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 1
                  },
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 2
                  },
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 3
                  }
                ]
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 3
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "cell13",
                "value": "sliding"
              },
              {
                "op": "not",
                "condition": {
                  "op": "all",
                  "conditions": [
                    {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "eq",
                          "parameter": "mergeTopLight",
                          "value": true
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 3
                            }
                          ]
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 3
                            }
                          ]
                        }
                      ]
                    },
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 1
                    }
                  ]
                }
              }
            ]
          }
        ]
      },
      {
        "op": "all",
        "conditions": [
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "windowType",
                "value": "mixed"
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 2
                  },
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 3
                  }
                ]
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 1
                  },
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 2
                  },
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 3
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "cell21",
                "value": "sliding"
              },
              {
                "op": "not",
                "condition": {
                  "op": "all",
                  "conditions": [
                    {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "eq",
                          "parameter": "mergeTopLight",
                          "value": true
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 3
                            }
                          ]
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 3
                            }
                          ]
                        }
                      ]
                    },
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 2
                    }
                  ]
                }
              }
            ]
          }
        ]
      },
      {
        "op": "all",
        "conditions": [
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "windowType",
                "value": "mixed"
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 2
                  },
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 3
                  }
                ]
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 2
                  },
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 3
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "cell22",
                "value": "sliding"
              },
              {
                "op": "not",
                "condition": {
                  "op": "all",
                  "conditions": [
                    {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "eq",
                          "parameter": "mergeTopLight",
                          "value": true
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 3
                            }
                          ]
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 3
                            }
                          ]
                        }
                      ]
                    },
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 2
                    }
                  ]
                }
              }
            ]
          }
        ]
      },
      {
        "op": "all",
        "conditions": [
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "windowType",
                "value": "mixed"
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 2
                  },
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 3
                  }
                ]
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 3
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "cell23",
                "value": "sliding"
              },
              {
                "op": "not",
                "condition": {
                  "op": "all",
                  "conditions": [
                    {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "eq",
                          "parameter": "mergeTopLight",
                          "value": true
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 3
                            }
                          ]
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 3
                            }
                          ]
                        }
                      ]
                    },
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 2
                    }
                  ]
                }
              }
            ]
          }
        ]
      },
      {
        "op": "all",
        "conditions": [
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "windowType",
                "value": "mixed"
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 3
                  }
                ]
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 1
                  },
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 2
                  },
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 3
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "cell31",
                "value": "sliding"
              },
              {
                "op": "not",
                "condition": {
                  "op": "all",
                  "conditions": [
                    {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "eq",
                          "parameter": "mergeTopLight",
                          "value": true
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 3
                            }
                          ]
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 3
                            }
                          ]
                        }
                      ]
                    },
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 3
                    }
                  ]
                }
              }
            ]
          }
        ]
      },
      {
        "op": "all",
        "conditions": [
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "windowType",
                "value": "mixed"
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 3
                  }
                ]
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 2
                  },
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 3
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "cell32",
                "value": "sliding"
              },
              {
                "op": "not",
                "condition": {
                  "op": "all",
                  "conditions": [
                    {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "eq",
                          "parameter": "mergeTopLight",
                          "value": true
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 3
                            }
                          ]
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 3
                            }
                          ]
                        }
                      ]
                    },
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 3
                    }
                  ]
                }
              }
            ]
          }
        ]
      },
      {
        "op": "all",
        "conditions": [
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "windowType",
                "value": "mixed"
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 3
                  }
                ]
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 3
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "cell33",
                "value": "sliding"
              },
              {
                "op": "not",
                "condition": {
                  "op": "all",
                  "conditions": [
                    {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "eq",
                          "parameter": "mergeTopLight",
                          "value": true
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 3
                            }
                          ]
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 3
                            }
                          ]
                        }
                      ]
                    },
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 3
                    }
                  ]
                }
              }
            ]
          }
        ]
      }
    ]
  }
}
```

### screenCount — 每洞口滑动纱窗数

```json
{
  "key": "screenCount",
  "displayName": {
    "zh-CN": "每洞口滑动纱窗数"
  },
  "valueType": "integer",
  "defaultValue": 1,
  "group": "sliding",
  "constraints": {
    "minimum": 1,
    "maximum": 2
  },
  "visibleWhen": {
    "op": "all",
    "conditions": [
      {
        "op": "any",
        "conditions": [
          {
            "op": "eq",
            "parameter": "windowType",
            "value": "sliding"
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "all",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "windowType",
                    "value": "mixed"
                  },
                  {
                    "op": "any",
                    "conditions": [
                      {
                        "op": "eq",
                        "parameter": "rows",
                        "value": 1
                      },
                      {
                        "op": "eq",
                        "parameter": "rows",
                        "value": 2
                      },
                      {
                        "op": "eq",
                        "parameter": "rows",
                        "value": 3
                      }
                    ]
                  },
                  {
                    "op": "any",
                    "conditions": [
                      {
                        "op": "eq",
                        "parameter": "columns",
                        "value": 1
                      },
                      {
                        "op": "eq",
                        "parameter": "columns",
                        "value": 2
                      },
                      {
                        "op": "eq",
                        "parameter": "columns",
                        "value": 3
                      }
                    ]
                  }
                ]
              },
              {
                "op": "all",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "cell11",
                    "value": "sliding"
                  },
                  {
                    "op": "not",
                    "condition": {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "all",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "mergeTopLight",
                              "value": true
                            },
                            {
                              "op": "any",
                              "conditions": [
                                {
                                  "op": "eq",
                                  "parameter": "columns",
                                  "value": 2
                                },
                                {
                                  "op": "eq",
                                  "parameter": "columns",
                                  "value": 3
                                }
                              ]
                            },
                            {
                              "op": "any",
                              "conditions": [
                                {
                                  "op": "eq",
                                  "parameter": "rows",
                                  "value": 2
                                },
                                {
                                  "op": "eq",
                                  "parameter": "rows",
                                  "value": 3
                                }
                              ]
                            }
                          ]
                        },
                        {
                          "op": "eq",
                          "parameter": "rows",
                          "value": 1
                        }
                      ]
                    }
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "all",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "windowType",
                    "value": "mixed"
                  },
                  {
                    "op": "any",
                    "conditions": [
                      {
                        "op": "eq",
                        "parameter": "rows",
                        "value": 1
                      },
                      {
                        "op": "eq",
                        "parameter": "rows",
                        "value": 2
                      },
                      {
                        "op": "eq",
                        "parameter": "rows",
                        "value": 3
                      }
                    ]
                  },
                  {
                    "op": "any",
                    "conditions": [
                      {
                        "op": "eq",
                        "parameter": "columns",
                        "value": 2
                      },
                      {
                        "op": "eq",
                        "parameter": "columns",
                        "value": 3
                      }
                    ]
                  }
                ]
              },
              {
                "op": "all",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "cell12",
                    "value": "sliding"
                  },
                  {
                    "op": "not",
                    "condition": {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "all",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "mergeTopLight",
                              "value": true
                            },
                            {
                              "op": "any",
                              "conditions": [
                                {
                                  "op": "eq",
                                  "parameter": "columns",
                                  "value": 2
                                },
                                {
                                  "op": "eq",
                                  "parameter": "columns",
                                  "value": 3
                                }
                              ]
                            },
                            {
                              "op": "any",
                              "conditions": [
                                {
                                  "op": "eq",
                                  "parameter": "rows",
                                  "value": 2
                                },
                                {
                                  "op": "eq",
                                  "parameter": "rows",
                                  "value": 3
                                }
                              ]
                            }
                          ]
                        },
                        {
                          "op": "eq",
                          "parameter": "rows",
                          "value": 1
                        }
                      ]
                    }
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "all",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "windowType",
                    "value": "mixed"
                  },
                  {
                    "op": "any",
                    "conditions": [
                      {
                        "op": "eq",
                        "parameter": "rows",
                        "value": 1
                      },
                      {
                        "op": "eq",
                        "parameter": "rows",
                        "value": 2
                      },
                      {
                        "op": "eq",
                        "parameter": "rows",
                        "value": 3
                      }
                    ]
                  },
                  {
                    "op": "any",
                    "conditions": [
                      {
                        "op": "eq",
                        "parameter": "columns",
                        "value": 3
                      }
                    ]
                  }
                ]
              },
              {
                "op": "all",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "cell13",
                    "value": "sliding"
                  },
                  {
                    "op": "not",
                    "condition": {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "all",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "mergeTopLight",
                              "value": true
                            },
                            {
                              "op": "any",
                              "conditions": [
                                {
                                  "op": "eq",
                                  "parameter": "columns",
                                  "value": 2
                                },
                                {
                                  "op": "eq",
                                  "parameter": "columns",
                                  "value": 3
                                }
                              ]
                            },
                            {
                              "op": "any",
                              "conditions": [
                                {
                                  "op": "eq",
                                  "parameter": "rows",
                                  "value": 2
                                },
                                {
                                  "op": "eq",
                                  "parameter": "rows",
                                  "value": 3
                                }
                              ]
                            }
                          ]
                        },
                        {
                          "op": "eq",
                          "parameter": "rows",
                          "value": 1
                        }
                      ]
                    }
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "all",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "windowType",
                    "value": "mixed"
                  },
                  {
                    "op": "any",
                    "conditions": [
                      {
                        "op": "eq",
                        "parameter": "rows",
                        "value": 2
                      },
                      {
                        "op": "eq",
                        "parameter": "rows",
                        "value": 3
                      }
                    ]
                  },
                  {
                    "op": "any",
                    "conditions": [
                      {
                        "op": "eq",
                        "parameter": "columns",
                        "value": 1
                      },
                      {
                        "op": "eq",
                        "parameter": "columns",
                        "value": 2
                      },
                      {
                        "op": "eq",
                        "parameter": "columns",
                        "value": 3
                      }
                    ]
                  }
                ]
              },
              {
                "op": "all",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "cell21",
                    "value": "sliding"
                  },
                  {
                    "op": "not",
                    "condition": {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "all",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "mergeTopLight",
                              "value": true
                            },
                            {
                              "op": "any",
                              "conditions": [
                                {
                                  "op": "eq",
                                  "parameter": "columns",
                                  "value": 2
                                },
                                {
                                  "op": "eq",
                                  "parameter": "columns",
                                  "value": 3
                                }
                              ]
                            },
                            {
                              "op": "any",
                              "conditions": [
                                {
                                  "op": "eq",
                                  "parameter": "rows",
                                  "value": 2
                                },
                                {
                                  "op": "eq",
                                  "parameter": "rows",
                                  "value": 3
                                }
                              ]
                            }
                          ]
                        },
                        {
                          "op": "eq",
                          "parameter": "rows",
                          "value": 2
                        }
                      ]
                    }
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "all",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "windowType",
                    "value": "mixed"
                  },
                  {
                    "op": "any",
                    "conditions": [
                      {
                        "op": "eq",
                        "parameter": "rows",
                        "value": 2
                      },
                      {
                        "op": "eq",
                        "parameter": "rows",
                        "value": 3
                      }
                    ]
                  },
                  {
                    "op": "any",
                    "conditions": [
                      {
                        "op": "eq",
                        "parameter": "columns",
                        "value": 2
                      },
                      {
                        "op": "eq",
                        "parameter": "columns",
                        "value": 3
                      }
                    ]
                  }
                ]
              },
              {
                "op": "all",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "cell22",
                    "value": "sliding"
                  },
                  {
                    "op": "not",
                    "condition": {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "all",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "mergeTopLight",
                              "value": true
                            },
                            {
                              "op": "any",
                              "conditions": [
                                {
                                  "op": "eq",
                                  "parameter": "columns",
                                  "value": 2
                                },
                                {
                                  "op": "eq",
                                  "parameter": "columns",
                                  "value": 3
                                }
                              ]
                            },
                            {
                              "op": "any",
                              "conditions": [
                                {
                                  "op": "eq",
                                  "parameter": "rows",
                                  "value": 2
                                },
                                {
                                  "op": "eq",
                                  "parameter": "rows",
                                  "value": 3
                                }
                              ]
                            }
                          ]
                        },
                        {
                          "op": "eq",
                          "parameter": "rows",
                          "value": 2
                        }
                      ]
                    }
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "all",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "windowType",
                    "value": "mixed"
                  },
                  {
                    "op": "any",
                    "conditions": [
                      {
                        "op": "eq",
                        "parameter": "rows",
                        "value": 2
                      },
                      {
                        "op": "eq",
                        "parameter": "rows",
                        "value": 3
                      }
                    ]
                  },
                  {
                    "op": "any",
                    "conditions": [
                      {
                        "op": "eq",
                        "parameter": "columns",
                        "value": 3
                      }
                    ]
                  }
                ]
              },
              {
                "op": "all",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "cell23",
                    "value": "sliding"
                  },
                  {
                    "op": "not",
                    "condition": {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "all",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "mergeTopLight",
                              "value": true
                            },
                            {
                              "op": "any",
                              "conditions": [
                                {
                                  "op": "eq",
                                  "parameter": "columns",
                                  "value": 2
                                },
                                {
                                  "op": "eq",
                                  "parameter": "columns",
                                  "value": 3
                                }
                              ]
                            },
                            {
                              "op": "any",
                              "conditions": [
                                {
                                  "op": "eq",
                                  "parameter": "rows",
                                  "value": 2
                                },
                                {
                                  "op": "eq",
                                  "parameter": "rows",
                                  "value": 3
                                }
                              ]
                            }
                          ]
                        },
                        {
                          "op": "eq",
                          "parameter": "rows",
                          "value": 2
                        }
                      ]
                    }
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "all",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "windowType",
                    "value": "mixed"
                  },
                  {
                    "op": "any",
                    "conditions": [
                      {
                        "op": "eq",
                        "parameter": "rows",
                        "value": 3
                      }
                    ]
                  },
                  {
                    "op": "any",
                    "conditions": [
                      {
                        "op": "eq",
                        "parameter": "columns",
                        "value": 1
                      },
                      {
                        "op": "eq",
                        "parameter": "columns",
                        "value": 2
                      },
                      {
                        "op": "eq",
                        "parameter": "columns",
                        "value": 3
                      }
                    ]
                  }
                ]
              },
              {
                "op": "all",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "cell31",
                    "value": "sliding"
                  },
                  {
                    "op": "not",
                    "condition": {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "all",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "mergeTopLight",
                              "value": true
                            },
                            {
                              "op": "any",
                              "conditions": [
                                {
                                  "op": "eq",
                                  "parameter": "columns",
                                  "value": 2
                                },
                                {
                                  "op": "eq",
                                  "parameter": "columns",
                                  "value": 3
                                }
                              ]
                            },
                            {
                              "op": "any",
                              "conditions": [
                                {
                                  "op": "eq",
                                  "parameter": "rows",
                                  "value": 2
                                },
                                {
                                  "op": "eq",
                                  "parameter": "rows",
                                  "value": 3
                                }
                              ]
                            }
                          ]
                        },
                        {
                          "op": "eq",
                          "parameter": "rows",
                          "value": 3
                        }
                      ]
                    }
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "all",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "windowType",
                    "value": "mixed"
                  },
                  {
                    "op": "any",
                    "conditions": [
                      {
                        "op": "eq",
                        "parameter": "rows",
                        "value": 3
                      }
                    ]
                  },
                  {
                    "op": "any",
                    "conditions": [
                      {
                        "op": "eq",
                        "parameter": "columns",
                        "value": 2
                      },
                      {
                        "op": "eq",
                        "parameter": "columns",
                        "value": 3
                      }
                    ]
                  }
                ]
              },
              {
                "op": "all",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "cell32",
                    "value": "sliding"
                  },
                  {
                    "op": "not",
                    "condition": {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "all",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "mergeTopLight",
                              "value": true
                            },
                            {
                              "op": "any",
                              "conditions": [
                                {
                                  "op": "eq",
                                  "parameter": "columns",
                                  "value": 2
                                },
                                {
                                  "op": "eq",
                                  "parameter": "columns",
                                  "value": 3
                                }
                              ]
                            },
                            {
                              "op": "any",
                              "conditions": [
                                {
                                  "op": "eq",
                                  "parameter": "rows",
                                  "value": 2
                                },
                                {
                                  "op": "eq",
                                  "parameter": "rows",
                                  "value": 3
                                }
                              ]
                            }
                          ]
                        },
                        {
                          "op": "eq",
                          "parameter": "rows",
                          "value": 3
                        }
                      ]
                    }
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "all",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "windowType",
                    "value": "mixed"
                  },
                  {
                    "op": "any",
                    "conditions": [
                      {
                        "op": "eq",
                        "parameter": "rows",
                        "value": 3
                      }
                    ]
                  },
                  {
                    "op": "any",
                    "conditions": [
                      {
                        "op": "eq",
                        "parameter": "columns",
                        "value": 3
                      }
                    ]
                  }
                ]
              },
              {
                "op": "all",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "cell33",
                    "value": "sliding"
                  },
                  {
                    "op": "not",
                    "condition": {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "all",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "mergeTopLight",
                              "value": true
                            },
                            {
                              "op": "any",
                              "conditions": [
                                {
                                  "op": "eq",
                                  "parameter": "columns",
                                  "value": 2
                                },
                                {
                                  "op": "eq",
                                  "parameter": "columns",
                                  "value": 3
                                }
                              ]
                            },
                            {
                              "op": "any",
                              "conditions": [
                                {
                                  "op": "eq",
                                  "parameter": "rows",
                                  "value": 2
                                },
                                {
                                  "op": "eq",
                                  "parameter": "rows",
                                  "value": 3
                                }
                              ]
                            }
                          ]
                        },
                        {
                          "op": "eq",
                          "parameter": "rows",
                          "value": 3
                        }
                      ]
                    }
                  }
                ]
              }
            ]
          }
        ]
      },
      {
        "op": "eq",
        "parameter": "screenEnabled",
        "value": true
      }
    ]
  }
}
```

### hingedCount — 每洞口平开扇数

```json
{
  "key": "hingedCount",
  "displayName": {
    "zh-CN": "每洞口平开扇数"
  },
  "valueType": "integer",
  "defaultValue": 1,
  "group": "hinged",
  "constraints": {
    "minimum": 1,
    "maximum": 2
  },
  "visibleWhen": {
    "op": "any",
    "conditions": [
      {
        "op": "eq",
        "parameter": "windowType",
        "value": "hinged"
      },
      {
        "op": "all",
        "conditions": [
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "windowType",
                "value": "mixed"
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 1
                  },
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 2
                  },
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 3
                  }
                ]
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 1
                  },
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 2
                  },
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 3
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "cell11",
                "value": "hinged"
              },
              {
                "op": "not",
                "condition": {
                  "op": "all",
                  "conditions": [
                    {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "eq",
                          "parameter": "mergeTopLight",
                          "value": true
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 3
                            }
                          ]
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 3
                            }
                          ]
                        }
                      ]
                    },
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 1
                    }
                  ]
                }
              }
            ]
          }
        ]
      },
      {
        "op": "all",
        "conditions": [
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "windowType",
                "value": "mixed"
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 1
                  },
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 2
                  },
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 3
                  }
                ]
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 2
                  },
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 3
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "cell12",
                "value": "hinged"
              },
              {
                "op": "not",
                "condition": {
                  "op": "all",
                  "conditions": [
                    {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "eq",
                          "parameter": "mergeTopLight",
                          "value": true
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 3
                            }
                          ]
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 3
                            }
                          ]
                        }
                      ]
                    },
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 1
                    }
                  ]
                }
              }
            ]
          }
        ]
      },
      {
        "op": "all",
        "conditions": [
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "windowType",
                "value": "mixed"
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 1
                  },
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 2
                  },
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 3
                  }
                ]
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 3
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "cell13",
                "value": "hinged"
              },
              {
                "op": "not",
                "condition": {
                  "op": "all",
                  "conditions": [
                    {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "eq",
                          "parameter": "mergeTopLight",
                          "value": true
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 3
                            }
                          ]
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 3
                            }
                          ]
                        }
                      ]
                    },
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 1
                    }
                  ]
                }
              }
            ]
          }
        ]
      },
      {
        "op": "all",
        "conditions": [
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "windowType",
                "value": "mixed"
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 2
                  },
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 3
                  }
                ]
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 1
                  },
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 2
                  },
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 3
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "cell21",
                "value": "hinged"
              },
              {
                "op": "not",
                "condition": {
                  "op": "all",
                  "conditions": [
                    {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "eq",
                          "parameter": "mergeTopLight",
                          "value": true
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 3
                            }
                          ]
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 3
                            }
                          ]
                        }
                      ]
                    },
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 2
                    }
                  ]
                }
              }
            ]
          }
        ]
      },
      {
        "op": "all",
        "conditions": [
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "windowType",
                "value": "mixed"
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 2
                  },
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 3
                  }
                ]
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 2
                  },
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 3
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "cell22",
                "value": "hinged"
              },
              {
                "op": "not",
                "condition": {
                  "op": "all",
                  "conditions": [
                    {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "eq",
                          "parameter": "mergeTopLight",
                          "value": true
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 3
                            }
                          ]
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 3
                            }
                          ]
                        }
                      ]
                    },
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 2
                    }
                  ]
                }
              }
            ]
          }
        ]
      },
      {
        "op": "all",
        "conditions": [
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "windowType",
                "value": "mixed"
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 2
                  },
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 3
                  }
                ]
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 3
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "cell23",
                "value": "hinged"
              },
              {
                "op": "not",
                "condition": {
                  "op": "all",
                  "conditions": [
                    {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "eq",
                          "parameter": "mergeTopLight",
                          "value": true
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 3
                            }
                          ]
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 3
                            }
                          ]
                        }
                      ]
                    },
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 2
                    }
                  ]
                }
              }
            ]
          }
        ]
      },
      {
        "op": "all",
        "conditions": [
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "windowType",
                "value": "mixed"
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 3
                  }
                ]
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 1
                  },
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 2
                  },
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 3
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "cell31",
                "value": "hinged"
              },
              {
                "op": "not",
                "condition": {
                  "op": "all",
                  "conditions": [
                    {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "eq",
                          "parameter": "mergeTopLight",
                          "value": true
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 3
                            }
                          ]
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 3
                            }
                          ]
                        }
                      ]
                    },
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 3
                    }
                  ]
                }
              }
            ]
          }
        ]
      },
      {
        "op": "all",
        "conditions": [
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "windowType",
                "value": "mixed"
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 3
                  }
                ]
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 2
                  },
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 3
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "cell32",
                "value": "hinged"
              },
              {
                "op": "not",
                "condition": {
                  "op": "all",
                  "conditions": [
                    {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "eq",
                          "parameter": "mergeTopLight",
                          "value": true
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 3
                            }
                          ]
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 3
                            }
                          ]
                        }
                      ]
                    },
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 3
                    }
                  ]
                }
              }
            ]
          }
        ]
      },
      {
        "op": "all",
        "conditions": [
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "windowType",
                "value": "mixed"
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 3
                  }
                ]
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 3
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "cell33",
                "value": "hinged"
              },
              {
                "op": "not",
                "condition": {
                  "op": "all",
                  "conditions": [
                    {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "eq",
                          "parameter": "mergeTopLight",
                          "value": true
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 3
                            }
                          ]
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 3
                            }
                          ]
                        }
                      ]
                    },
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 3
                    }
                  ]
                }
              }
            ]
          }
        ]
      }
    ]
  }
}
```

### openingSide — 单扇合页侧

```json
{
  "key": "openingSide",
  "displayName": {
    "zh-CN": "单扇合页侧"
  },
  "valueType": "enum",
  "defaultValue": "left",
  "group": "hinged",
  "choices": [
    {
      "value": "left",
      "displayName": {
        "zh-CN": "左侧"
      }
    },
    {
      "value": "right",
      "displayName": {
        "zh-CN": "右侧"
      }
    }
  ],
  "visibleWhen": {
    "op": "all",
    "conditions": [
      {
        "op": "any",
        "conditions": [
          {
            "op": "eq",
            "parameter": "windowType",
            "value": "hinged"
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "all",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "windowType",
                    "value": "mixed"
                  },
                  {
                    "op": "any",
                    "conditions": [
                      {
                        "op": "eq",
                        "parameter": "rows",
                        "value": 1
                      },
                      {
                        "op": "eq",
                        "parameter": "rows",
                        "value": 2
                      },
                      {
                        "op": "eq",
                        "parameter": "rows",
                        "value": 3
                      }
                    ]
                  },
                  {
                    "op": "any",
                    "conditions": [
                      {
                        "op": "eq",
                        "parameter": "columns",
                        "value": 1
                      },
                      {
                        "op": "eq",
                        "parameter": "columns",
                        "value": 2
                      },
                      {
                        "op": "eq",
                        "parameter": "columns",
                        "value": 3
                      }
                    ]
                  }
                ]
              },
              {
                "op": "all",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "cell11",
                    "value": "hinged"
                  },
                  {
                    "op": "not",
                    "condition": {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "all",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "mergeTopLight",
                              "value": true
                            },
                            {
                              "op": "any",
                              "conditions": [
                                {
                                  "op": "eq",
                                  "parameter": "columns",
                                  "value": 2
                                },
                                {
                                  "op": "eq",
                                  "parameter": "columns",
                                  "value": 3
                                }
                              ]
                            },
                            {
                              "op": "any",
                              "conditions": [
                                {
                                  "op": "eq",
                                  "parameter": "rows",
                                  "value": 2
                                },
                                {
                                  "op": "eq",
                                  "parameter": "rows",
                                  "value": 3
                                }
                              ]
                            }
                          ]
                        },
                        {
                          "op": "eq",
                          "parameter": "rows",
                          "value": 1
                        }
                      ]
                    }
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "all",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "windowType",
                    "value": "mixed"
                  },
                  {
                    "op": "any",
                    "conditions": [
                      {
                        "op": "eq",
                        "parameter": "rows",
                        "value": 1
                      },
                      {
                        "op": "eq",
                        "parameter": "rows",
                        "value": 2
                      },
                      {
                        "op": "eq",
                        "parameter": "rows",
                        "value": 3
                      }
                    ]
                  },
                  {
                    "op": "any",
                    "conditions": [
                      {
                        "op": "eq",
                        "parameter": "columns",
                        "value": 2
                      },
                      {
                        "op": "eq",
                        "parameter": "columns",
                        "value": 3
                      }
                    ]
                  }
                ]
              },
              {
                "op": "all",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "cell12",
                    "value": "hinged"
                  },
                  {
                    "op": "not",
                    "condition": {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "all",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "mergeTopLight",
                              "value": true
                            },
                            {
                              "op": "any",
                              "conditions": [
                                {
                                  "op": "eq",
                                  "parameter": "columns",
                                  "value": 2
                                },
                                {
                                  "op": "eq",
                                  "parameter": "columns",
                                  "value": 3
                                }
                              ]
                            },
                            {
                              "op": "any",
                              "conditions": [
                                {
                                  "op": "eq",
                                  "parameter": "rows",
                                  "value": 2
                                },
                                {
                                  "op": "eq",
                                  "parameter": "rows",
                                  "value": 3
                                }
                              ]
                            }
                          ]
                        },
                        {
                          "op": "eq",
                          "parameter": "rows",
                          "value": 1
                        }
                      ]
                    }
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "all",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "windowType",
                    "value": "mixed"
                  },
                  {
                    "op": "any",
                    "conditions": [
                      {
                        "op": "eq",
                        "parameter": "rows",
                        "value": 1
                      },
                      {
                        "op": "eq",
                        "parameter": "rows",
                        "value": 2
                      },
                      {
                        "op": "eq",
                        "parameter": "rows",
                        "value": 3
                      }
                    ]
                  },
                  {
                    "op": "any",
                    "conditions": [
                      {
                        "op": "eq",
                        "parameter": "columns",
                        "value": 3
                      }
                    ]
                  }
                ]
              },
              {
                "op": "all",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "cell13",
                    "value": "hinged"
                  },
                  {
                    "op": "not",
                    "condition": {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "all",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "mergeTopLight",
                              "value": true
                            },
                            {
                              "op": "any",
                              "conditions": [
                                {
                                  "op": "eq",
                                  "parameter": "columns",
                                  "value": 2
                                },
                                {
                                  "op": "eq",
                                  "parameter": "columns",
                                  "value": 3
                                }
                              ]
                            },
                            {
                              "op": "any",
                              "conditions": [
                                {
                                  "op": "eq",
                                  "parameter": "rows",
                                  "value": 2
                                },
                                {
                                  "op": "eq",
                                  "parameter": "rows",
                                  "value": 3
                                }
                              ]
                            }
                          ]
                        },
                        {
                          "op": "eq",
                          "parameter": "rows",
                          "value": 1
                        }
                      ]
                    }
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "all",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "windowType",
                    "value": "mixed"
                  },
                  {
                    "op": "any",
                    "conditions": [
                      {
                        "op": "eq",
                        "parameter": "rows",
                        "value": 2
                      },
                      {
                        "op": "eq",
                        "parameter": "rows",
                        "value": 3
                      }
                    ]
                  },
                  {
                    "op": "any",
                    "conditions": [
                      {
                        "op": "eq",
                        "parameter": "columns",
                        "value": 1
                      },
                      {
                        "op": "eq",
                        "parameter": "columns",
                        "value": 2
                      },
                      {
                        "op": "eq",
                        "parameter": "columns",
                        "value": 3
                      }
                    ]
                  }
                ]
              },
              {
                "op": "all",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "cell21",
                    "value": "hinged"
                  },
                  {
                    "op": "not",
                    "condition": {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "all",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "mergeTopLight",
                              "value": true
                            },
                            {
                              "op": "any",
                              "conditions": [
                                {
                                  "op": "eq",
                                  "parameter": "columns",
                                  "value": 2
                                },
                                {
                                  "op": "eq",
                                  "parameter": "columns",
                                  "value": 3
                                }
                              ]
                            },
                            {
                              "op": "any",
                              "conditions": [
                                {
                                  "op": "eq",
                                  "parameter": "rows",
                                  "value": 2
                                },
                                {
                                  "op": "eq",
                                  "parameter": "rows",
                                  "value": 3
                                }
                              ]
                            }
                          ]
                        },
                        {
                          "op": "eq",
                          "parameter": "rows",
                          "value": 2
                        }
                      ]
                    }
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "all",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "windowType",
                    "value": "mixed"
                  },
                  {
                    "op": "any",
                    "conditions": [
                      {
                        "op": "eq",
                        "parameter": "rows",
                        "value": 2
                      },
                      {
                        "op": "eq",
                        "parameter": "rows",
                        "value": 3
                      }
                    ]
                  },
                  {
                    "op": "any",
                    "conditions": [
                      {
                        "op": "eq",
                        "parameter": "columns",
                        "value": 2
                      },
                      {
                        "op": "eq",
                        "parameter": "columns",
                        "value": 3
                      }
                    ]
                  }
                ]
              },
              {
                "op": "all",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "cell22",
                    "value": "hinged"
                  },
                  {
                    "op": "not",
                    "condition": {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "all",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "mergeTopLight",
                              "value": true
                            },
                            {
                              "op": "any",
                              "conditions": [
                                {
                                  "op": "eq",
                                  "parameter": "columns",
                                  "value": 2
                                },
                                {
                                  "op": "eq",
                                  "parameter": "columns",
                                  "value": 3
                                }
                              ]
                            },
                            {
                              "op": "any",
                              "conditions": [
                                {
                                  "op": "eq",
                                  "parameter": "rows",
                                  "value": 2
                                },
                                {
                                  "op": "eq",
                                  "parameter": "rows",
                                  "value": 3
                                }
                              ]
                            }
                          ]
                        },
                        {
                          "op": "eq",
                          "parameter": "rows",
                          "value": 2
                        }
                      ]
                    }
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "all",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "windowType",
                    "value": "mixed"
                  },
                  {
                    "op": "any",
                    "conditions": [
                      {
                        "op": "eq",
                        "parameter": "rows",
                        "value": 2
                      },
                      {
                        "op": "eq",
                        "parameter": "rows",
                        "value": 3
                      }
                    ]
                  },
                  {
                    "op": "any",
                    "conditions": [
                      {
                        "op": "eq",
                        "parameter": "columns",
                        "value": 3
                      }
                    ]
                  }
                ]
              },
              {
                "op": "all",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "cell23",
                    "value": "hinged"
                  },
                  {
                    "op": "not",
                    "condition": {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "all",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "mergeTopLight",
                              "value": true
                            },
                            {
                              "op": "any",
                              "conditions": [
                                {
                                  "op": "eq",
                                  "parameter": "columns",
                                  "value": 2
                                },
                                {
                                  "op": "eq",
                                  "parameter": "columns",
                                  "value": 3
                                }
                              ]
                            },
                            {
                              "op": "any",
                              "conditions": [
                                {
                                  "op": "eq",
                                  "parameter": "rows",
                                  "value": 2
                                },
                                {
                                  "op": "eq",
                                  "parameter": "rows",
                                  "value": 3
                                }
                              ]
                            }
                          ]
                        },
                        {
                          "op": "eq",
                          "parameter": "rows",
                          "value": 2
                        }
                      ]
                    }
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "all",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "windowType",
                    "value": "mixed"
                  },
                  {
                    "op": "any",
                    "conditions": [
                      {
                        "op": "eq",
                        "parameter": "rows",
                        "value": 3
                      }
                    ]
                  },
                  {
                    "op": "any",
                    "conditions": [
                      {
                        "op": "eq",
                        "parameter": "columns",
                        "value": 1
                      },
                      {
                        "op": "eq",
                        "parameter": "columns",
                        "value": 2
                      },
                      {
                        "op": "eq",
                        "parameter": "columns",
                        "value": 3
                      }
                    ]
                  }
                ]
              },
              {
                "op": "all",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "cell31",
                    "value": "hinged"
                  },
                  {
                    "op": "not",
                    "condition": {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "all",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "mergeTopLight",
                              "value": true
                            },
                            {
                              "op": "any",
                              "conditions": [
                                {
                                  "op": "eq",
                                  "parameter": "columns",
                                  "value": 2
                                },
                                {
                                  "op": "eq",
                                  "parameter": "columns",
                                  "value": 3
                                }
                              ]
                            },
                            {
                              "op": "any",
                              "conditions": [
                                {
                                  "op": "eq",
                                  "parameter": "rows",
                                  "value": 2
                                },
                                {
                                  "op": "eq",
                                  "parameter": "rows",
                                  "value": 3
                                }
                              ]
                            }
                          ]
                        },
                        {
                          "op": "eq",
                          "parameter": "rows",
                          "value": 3
                        }
                      ]
                    }
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "all",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "windowType",
                    "value": "mixed"
                  },
                  {
                    "op": "any",
                    "conditions": [
                      {
                        "op": "eq",
                        "parameter": "rows",
                        "value": 3
                      }
                    ]
                  },
                  {
                    "op": "any",
                    "conditions": [
                      {
                        "op": "eq",
                        "parameter": "columns",
                        "value": 2
                      },
                      {
                        "op": "eq",
                        "parameter": "columns",
                        "value": 3
                      }
                    ]
                  }
                ]
              },
              {
                "op": "all",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "cell32",
                    "value": "hinged"
                  },
                  {
                    "op": "not",
                    "condition": {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "all",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "mergeTopLight",
                              "value": true
                            },
                            {
                              "op": "any",
                              "conditions": [
                                {
                                  "op": "eq",
                                  "parameter": "columns",
                                  "value": 2
                                },
                                {
                                  "op": "eq",
                                  "parameter": "columns",
                                  "value": 3
                                }
                              ]
                            },
                            {
                              "op": "any",
                              "conditions": [
                                {
                                  "op": "eq",
                                  "parameter": "rows",
                                  "value": 2
                                },
                                {
                                  "op": "eq",
                                  "parameter": "rows",
                                  "value": 3
                                }
                              ]
                            }
                          ]
                        },
                        {
                          "op": "eq",
                          "parameter": "rows",
                          "value": 3
                        }
                      ]
                    }
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "all",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "windowType",
                    "value": "mixed"
                  },
                  {
                    "op": "any",
                    "conditions": [
                      {
                        "op": "eq",
                        "parameter": "rows",
                        "value": 3
                      }
                    ]
                  },
                  {
                    "op": "any",
                    "conditions": [
                      {
                        "op": "eq",
                        "parameter": "columns",
                        "value": 3
                      }
                    ]
                  }
                ]
              },
              {
                "op": "all",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "cell33",
                    "value": "hinged"
                  },
                  {
                    "op": "not",
                    "condition": {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "all",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "mergeTopLight",
                              "value": true
                            },
                            {
                              "op": "any",
                              "conditions": [
                                {
                                  "op": "eq",
                                  "parameter": "columns",
                                  "value": 2
                                },
                                {
                                  "op": "eq",
                                  "parameter": "columns",
                                  "value": 3
                                }
                              ]
                            },
                            {
                              "op": "any",
                              "conditions": [
                                {
                                  "op": "eq",
                                  "parameter": "rows",
                                  "value": 2
                                },
                                {
                                  "op": "eq",
                                  "parameter": "rows",
                                  "value": 3
                                }
                              ]
                            }
                          ]
                        },
                        {
                          "op": "eq",
                          "parameter": "rows",
                          "value": 3
                        }
                      ]
                    }
                  }
                ]
              }
            ]
          }
        ]
      },
      {
        "op": "eq",
        "parameter": "hingedCount",
        "value": 1
      }
    ]
  }
}
```

### openingDirection — 开启方向

```json
{
  "key": "openingDirection",
  "displayName": {
    "zh-CN": "开启方向"
  },
  "valueType": "enum",
  "defaultValue": "outward",
  "group": "hinged",
  "choices": [
    {
      "value": "inward",
      "displayName": {
        "zh-CN": "内开"
      }
    },
    {
      "value": "outward",
      "displayName": {
        "zh-CN": "外开"
      }
    }
  ],
  "visibleWhen": {
    "op": "any",
    "conditions": [
      {
        "op": "eq",
        "parameter": "windowType",
        "value": "hinged"
      },
      {
        "op": "all",
        "conditions": [
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "windowType",
                "value": "mixed"
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 1
                  },
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 2
                  },
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 3
                  }
                ]
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 1
                  },
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 2
                  },
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 3
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "cell11",
                "value": "hinged"
              },
              {
                "op": "not",
                "condition": {
                  "op": "all",
                  "conditions": [
                    {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "eq",
                          "parameter": "mergeTopLight",
                          "value": true
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 3
                            }
                          ]
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 3
                            }
                          ]
                        }
                      ]
                    },
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 1
                    }
                  ]
                }
              }
            ]
          }
        ]
      },
      {
        "op": "all",
        "conditions": [
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "windowType",
                "value": "mixed"
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 1
                  },
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 2
                  },
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 3
                  }
                ]
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 2
                  },
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 3
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "cell12",
                "value": "hinged"
              },
              {
                "op": "not",
                "condition": {
                  "op": "all",
                  "conditions": [
                    {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "eq",
                          "parameter": "mergeTopLight",
                          "value": true
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 3
                            }
                          ]
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 3
                            }
                          ]
                        }
                      ]
                    },
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 1
                    }
                  ]
                }
              }
            ]
          }
        ]
      },
      {
        "op": "all",
        "conditions": [
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "windowType",
                "value": "mixed"
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 1
                  },
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 2
                  },
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 3
                  }
                ]
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 3
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "cell13",
                "value": "hinged"
              },
              {
                "op": "not",
                "condition": {
                  "op": "all",
                  "conditions": [
                    {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "eq",
                          "parameter": "mergeTopLight",
                          "value": true
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 3
                            }
                          ]
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 3
                            }
                          ]
                        }
                      ]
                    },
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 1
                    }
                  ]
                }
              }
            ]
          }
        ]
      },
      {
        "op": "all",
        "conditions": [
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "windowType",
                "value": "mixed"
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 2
                  },
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 3
                  }
                ]
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 1
                  },
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 2
                  },
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 3
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "cell21",
                "value": "hinged"
              },
              {
                "op": "not",
                "condition": {
                  "op": "all",
                  "conditions": [
                    {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "eq",
                          "parameter": "mergeTopLight",
                          "value": true
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 3
                            }
                          ]
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 3
                            }
                          ]
                        }
                      ]
                    },
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 2
                    }
                  ]
                }
              }
            ]
          }
        ]
      },
      {
        "op": "all",
        "conditions": [
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "windowType",
                "value": "mixed"
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 2
                  },
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 3
                  }
                ]
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 2
                  },
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 3
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "cell22",
                "value": "hinged"
              },
              {
                "op": "not",
                "condition": {
                  "op": "all",
                  "conditions": [
                    {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "eq",
                          "parameter": "mergeTopLight",
                          "value": true
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 3
                            }
                          ]
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 3
                            }
                          ]
                        }
                      ]
                    },
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 2
                    }
                  ]
                }
              }
            ]
          }
        ]
      },
      {
        "op": "all",
        "conditions": [
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "windowType",
                "value": "mixed"
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 2
                  },
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 3
                  }
                ]
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 3
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "cell23",
                "value": "hinged"
              },
              {
                "op": "not",
                "condition": {
                  "op": "all",
                  "conditions": [
                    {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "eq",
                          "parameter": "mergeTopLight",
                          "value": true
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 3
                            }
                          ]
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 3
                            }
                          ]
                        }
                      ]
                    },
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 2
                    }
                  ]
                }
              }
            ]
          }
        ]
      },
      {
        "op": "all",
        "conditions": [
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "windowType",
                "value": "mixed"
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 3
                  }
                ]
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 1
                  },
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 2
                  },
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 3
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "cell31",
                "value": "hinged"
              },
              {
                "op": "not",
                "condition": {
                  "op": "all",
                  "conditions": [
                    {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "eq",
                          "parameter": "mergeTopLight",
                          "value": true
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 3
                            }
                          ]
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 3
                            }
                          ]
                        }
                      ]
                    },
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 3
                    }
                  ]
                }
              }
            ]
          }
        ]
      },
      {
        "op": "all",
        "conditions": [
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "windowType",
                "value": "mixed"
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 3
                  }
                ]
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 2
                  },
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 3
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "cell32",
                "value": "hinged"
              },
              {
                "op": "not",
                "condition": {
                  "op": "all",
                  "conditions": [
                    {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "eq",
                          "parameter": "mergeTopLight",
                          "value": true
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 3
                            }
                          ]
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 3
                            }
                          ]
                        }
                      ]
                    },
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 3
                    }
                  ]
                }
              }
            ]
          }
        ]
      },
      {
        "op": "all",
        "conditions": [
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "windowType",
                "value": "mixed"
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "rows",
                    "value": 3
                  }
                ]
              },
              {
                "op": "any",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "columns",
                    "value": 3
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "eq",
                "parameter": "cell33",
                "value": "hinged"
              },
              {
                "op": "not",
                "condition": {
                  "op": "all",
                  "conditions": [
                    {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "eq",
                          "parameter": "mergeTopLight",
                          "value": true
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "columns",
                              "value": 3
                            }
                          ]
                        },
                        {
                          "op": "any",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 2
                            },
                            {
                              "op": "eq",
                              "parameter": "rows",
                              "value": 3
                            }
                          ]
                        }
                      ]
                    },
                    {
                      "op": "eq",
                      "parameter": "rows",
                      "value": 3
                    }
                  ]
                }
              }
            ]
          }
        ]
      }
    ]
  }
}
```

### glassThickness — 玻璃厚度

```json
{
  "key": "glassThickness",
  "displayName": {
    "zh-CN": "玻璃厚度"
  },
  "valueType": "number",
  "defaultValue": 5,
  "group": "fillings",
  "constraints": {
    "minimum": 1,
    "maximum": 30
  },
  "unit": "mm",
  "quantity": "length"
}
```

### glassType — 玻璃类型

```json
{
  "key": "glassType",
  "displayName": {
    "zh-CN": "玻璃类型"
  },
  "valueType": "string",
  "defaultValue": "待工程选型",
  "group": "fillings"
}
```

### meshType — 纱网类型

```json
{
  "key": "meshType",
  "displayName": {
    "zh-CN": "纱网类型"
  },
  "valueType": "string",
  "defaultValue": "待工程选型",
  "group": "fillings",
  "visibleWhen": {
    "op": "all",
    "conditions": [
      {
        "op": "any",
        "conditions": [
          {
            "op": "eq",
            "parameter": "windowType",
            "value": "sliding"
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "all",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "windowType",
                    "value": "mixed"
                  },
                  {
                    "op": "any",
                    "conditions": [
                      {
                        "op": "eq",
                        "parameter": "rows",
                        "value": 1
                      },
                      {
                        "op": "eq",
                        "parameter": "rows",
                        "value": 2
                      },
                      {
                        "op": "eq",
                        "parameter": "rows",
                        "value": 3
                      }
                    ]
                  },
                  {
                    "op": "any",
                    "conditions": [
                      {
                        "op": "eq",
                        "parameter": "columns",
                        "value": 1
                      },
                      {
                        "op": "eq",
                        "parameter": "columns",
                        "value": 2
                      },
                      {
                        "op": "eq",
                        "parameter": "columns",
                        "value": 3
                      }
                    ]
                  }
                ]
              },
              {
                "op": "all",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "cell11",
                    "value": "sliding"
                  },
                  {
                    "op": "not",
                    "condition": {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "all",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "mergeTopLight",
                              "value": true
                            },
                            {
                              "op": "any",
                              "conditions": [
                                {
                                  "op": "eq",
                                  "parameter": "columns",
                                  "value": 2
                                },
                                {
                                  "op": "eq",
                                  "parameter": "columns",
                                  "value": 3
                                }
                              ]
                            },
                            {
                              "op": "any",
                              "conditions": [
                                {
                                  "op": "eq",
                                  "parameter": "rows",
                                  "value": 2
                                },
                                {
                                  "op": "eq",
                                  "parameter": "rows",
                                  "value": 3
                                }
                              ]
                            }
                          ]
                        },
                        {
                          "op": "eq",
                          "parameter": "rows",
                          "value": 1
                        }
                      ]
                    }
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "all",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "windowType",
                    "value": "mixed"
                  },
                  {
                    "op": "any",
                    "conditions": [
                      {
                        "op": "eq",
                        "parameter": "rows",
                        "value": 1
                      },
                      {
                        "op": "eq",
                        "parameter": "rows",
                        "value": 2
                      },
                      {
                        "op": "eq",
                        "parameter": "rows",
                        "value": 3
                      }
                    ]
                  },
                  {
                    "op": "any",
                    "conditions": [
                      {
                        "op": "eq",
                        "parameter": "columns",
                        "value": 2
                      },
                      {
                        "op": "eq",
                        "parameter": "columns",
                        "value": 3
                      }
                    ]
                  }
                ]
              },
              {
                "op": "all",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "cell12",
                    "value": "sliding"
                  },
                  {
                    "op": "not",
                    "condition": {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "all",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "mergeTopLight",
                              "value": true
                            },
                            {
                              "op": "any",
                              "conditions": [
                                {
                                  "op": "eq",
                                  "parameter": "columns",
                                  "value": 2
                                },
                                {
                                  "op": "eq",
                                  "parameter": "columns",
                                  "value": 3
                                }
                              ]
                            },
                            {
                              "op": "any",
                              "conditions": [
                                {
                                  "op": "eq",
                                  "parameter": "rows",
                                  "value": 2
                                },
                                {
                                  "op": "eq",
                                  "parameter": "rows",
                                  "value": 3
                                }
                              ]
                            }
                          ]
                        },
                        {
                          "op": "eq",
                          "parameter": "rows",
                          "value": 1
                        }
                      ]
                    }
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "all",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "windowType",
                    "value": "mixed"
                  },
                  {
                    "op": "any",
                    "conditions": [
                      {
                        "op": "eq",
                        "parameter": "rows",
                        "value": 1
                      },
                      {
                        "op": "eq",
                        "parameter": "rows",
                        "value": 2
                      },
                      {
                        "op": "eq",
                        "parameter": "rows",
                        "value": 3
                      }
                    ]
                  },
                  {
                    "op": "any",
                    "conditions": [
                      {
                        "op": "eq",
                        "parameter": "columns",
                        "value": 3
                      }
                    ]
                  }
                ]
              },
              {
                "op": "all",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "cell13",
                    "value": "sliding"
                  },
                  {
                    "op": "not",
                    "condition": {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "all",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "mergeTopLight",
                              "value": true
                            },
                            {
                              "op": "any",
                              "conditions": [
                                {
                                  "op": "eq",
                                  "parameter": "columns",
                                  "value": 2
                                },
                                {
                                  "op": "eq",
                                  "parameter": "columns",
                                  "value": 3
                                }
                              ]
                            },
                            {
                              "op": "any",
                              "conditions": [
                                {
                                  "op": "eq",
                                  "parameter": "rows",
                                  "value": 2
                                },
                                {
                                  "op": "eq",
                                  "parameter": "rows",
                                  "value": 3
                                }
                              ]
                            }
                          ]
                        },
                        {
                          "op": "eq",
                          "parameter": "rows",
                          "value": 1
                        }
                      ]
                    }
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "all",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "windowType",
                    "value": "mixed"
                  },
                  {
                    "op": "any",
                    "conditions": [
                      {
                        "op": "eq",
                        "parameter": "rows",
                        "value": 2
                      },
                      {
                        "op": "eq",
                        "parameter": "rows",
                        "value": 3
                      }
                    ]
                  },
                  {
                    "op": "any",
                    "conditions": [
                      {
                        "op": "eq",
                        "parameter": "columns",
                        "value": 1
                      },
                      {
                        "op": "eq",
                        "parameter": "columns",
                        "value": 2
                      },
                      {
                        "op": "eq",
                        "parameter": "columns",
                        "value": 3
                      }
                    ]
                  }
                ]
              },
              {
                "op": "all",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "cell21",
                    "value": "sliding"
                  },
                  {
                    "op": "not",
                    "condition": {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "all",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "mergeTopLight",
                              "value": true
                            },
                            {
                              "op": "any",
                              "conditions": [
                                {
                                  "op": "eq",
                                  "parameter": "columns",
                                  "value": 2
                                },
                                {
                                  "op": "eq",
                                  "parameter": "columns",
                                  "value": 3
                                }
                              ]
                            },
                            {
                              "op": "any",
                              "conditions": [
                                {
                                  "op": "eq",
                                  "parameter": "rows",
                                  "value": 2
                                },
                                {
                                  "op": "eq",
                                  "parameter": "rows",
                                  "value": 3
                                }
                              ]
                            }
                          ]
                        },
                        {
                          "op": "eq",
                          "parameter": "rows",
                          "value": 2
                        }
                      ]
                    }
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "all",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "windowType",
                    "value": "mixed"
                  },
                  {
                    "op": "any",
                    "conditions": [
                      {
                        "op": "eq",
                        "parameter": "rows",
                        "value": 2
                      },
                      {
                        "op": "eq",
                        "parameter": "rows",
                        "value": 3
                      }
                    ]
                  },
                  {
                    "op": "any",
                    "conditions": [
                      {
                        "op": "eq",
                        "parameter": "columns",
                        "value": 2
                      },
                      {
                        "op": "eq",
                        "parameter": "columns",
                        "value": 3
                      }
                    ]
                  }
                ]
              },
              {
                "op": "all",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "cell22",
                    "value": "sliding"
                  },
                  {
                    "op": "not",
                    "condition": {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "all",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "mergeTopLight",
                              "value": true
                            },
                            {
                              "op": "any",
                              "conditions": [
                                {
                                  "op": "eq",
                                  "parameter": "columns",
                                  "value": 2
                                },
                                {
                                  "op": "eq",
                                  "parameter": "columns",
                                  "value": 3
                                }
                              ]
                            },
                            {
                              "op": "any",
                              "conditions": [
                                {
                                  "op": "eq",
                                  "parameter": "rows",
                                  "value": 2
                                },
                                {
                                  "op": "eq",
                                  "parameter": "rows",
                                  "value": 3
                                }
                              ]
                            }
                          ]
                        },
                        {
                          "op": "eq",
                          "parameter": "rows",
                          "value": 2
                        }
                      ]
                    }
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "all",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "windowType",
                    "value": "mixed"
                  },
                  {
                    "op": "any",
                    "conditions": [
                      {
                        "op": "eq",
                        "parameter": "rows",
                        "value": 2
                      },
                      {
                        "op": "eq",
                        "parameter": "rows",
                        "value": 3
                      }
                    ]
                  },
                  {
                    "op": "any",
                    "conditions": [
                      {
                        "op": "eq",
                        "parameter": "columns",
                        "value": 3
                      }
                    ]
                  }
                ]
              },
              {
                "op": "all",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "cell23",
                    "value": "sliding"
                  },
                  {
                    "op": "not",
                    "condition": {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "all",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "mergeTopLight",
                              "value": true
                            },
                            {
                              "op": "any",
                              "conditions": [
                                {
                                  "op": "eq",
                                  "parameter": "columns",
                                  "value": 2
                                },
                                {
                                  "op": "eq",
                                  "parameter": "columns",
                                  "value": 3
                                }
                              ]
                            },
                            {
                              "op": "any",
                              "conditions": [
                                {
                                  "op": "eq",
                                  "parameter": "rows",
                                  "value": 2
                                },
                                {
                                  "op": "eq",
                                  "parameter": "rows",
                                  "value": 3
                                }
                              ]
                            }
                          ]
                        },
                        {
                          "op": "eq",
                          "parameter": "rows",
                          "value": 2
                        }
                      ]
                    }
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "all",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "windowType",
                    "value": "mixed"
                  },
                  {
                    "op": "any",
                    "conditions": [
                      {
                        "op": "eq",
                        "parameter": "rows",
                        "value": 3
                      }
                    ]
                  },
                  {
                    "op": "any",
                    "conditions": [
                      {
                        "op": "eq",
                        "parameter": "columns",
                        "value": 1
                      },
                      {
                        "op": "eq",
                        "parameter": "columns",
                        "value": 2
                      },
                      {
                        "op": "eq",
                        "parameter": "columns",
                        "value": 3
                      }
                    ]
                  }
                ]
              },
              {
                "op": "all",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "cell31",
                    "value": "sliding"
                  },
                  {
                    "op": "not",
                    "condition": {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "all",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "mergeTopLight",
                              "value": true
                            },
                            {
                              "op": "any",
                              "conditions": [
                                {
                                  "op": "eq",
                                  "parameter": "columns",
                                  "value": 2
                                },
                                {
                                  "op": "eq",
                                  "parameter": "columns",
                                  "value": 3
                                }
                              ]
                            },
                            {
                              "op": "any",
                              "conditions": [
                                {
                                  "op": "eq",
                                  "parameter": "rows",
                                  "value": 2
                                },
                                {
                                  "op": "eq",
                                  "parameter": "rows",
                                  "value": 3
                                }
                              ]
                            }
                          ]
                        },
                        {
                          "op": "eq",
                          "parameter": "rows",
                          "value": 3
                        }
                      ]
                    }
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "all",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "windowType",
                    "value": "mixed"
                  },
                  {
                    "op": "any",
                    "conditions": [
                      {
                        "op": "eq",
                        "parameter": "rows",
                        "value": 3
                      }
                    ]
                  },
                  {
                    "op": "any",
                    "conditions": [
                      {
                        "op": "eq",
                        "parameter": "columns",
                        "value": 2
                      },
                      {
                        "op": "eq",
                        "parameter": "columns",
                        "value": 3
                      }
                    ]
                  }
                ]
              },
              {
                "op": "all",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "cell32",
                    "value": "sliding"
                  },
                  {
                    "op": "not",
                    "condition": {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "all",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "mergeTopLight",
                              "value": true
                            },
                            {
                              "op": "any",
                              "conditions": [
                                {
                                  "op": "eq",
                                  "parameter": "columns",
                                  "value": 2
                                },
                                {
                                  "op": "eq",
                                  "parameter": "columns",
                                  "value": 3
                                }
                              ]
                            },
                            {
                              "op": "any",
                              "conditions": [
                                {
                                  "op": "eq",
                                  "parameter": "rows",
                                  "value": 2
                                },
                                {
                                  "op": "eq",
                                  "parameter": "rows",
                                  "value": 3
                                }
                              ]
                            }
                          ]
                        },
                        {
                          "op": "eq",
                          "parameter": "rows",
                          "value": 3
                        }
                      ]
                    }
                  }
                ]
              }
            ]
          },
          {
            "op": "all",
            "conditions": [
              {
                "op": "all",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "windowType",
                    "value": "mixed"
                  },
                  {
                    "op": "any",
                    "conditions": [
                      {
                        "op": "eq",
                        "parameter": "rows",
                        "value": 3
                      }
                    ]
                  },
                  {
                    "op": "any",
                    "conditions": [
                      {
                        "op": "eq",
                        "parameter": "columns",
                        "value": 3
                      }
                    ]
                  }
                ]
              },
              {
                "op": "all",
                "conditions": [
                  {
                    "op": "eq",
                    "parameter": "cell33",
                    "value": "sliding"
                  },
                  {
                    "op": "not",
                    "condition": {
                      "op": "all",
                      "conditions": [
                        {
                          "op": "all",
                          "conditions": [
                            {
                              "op": "eq",
                              "parameter": "mergeTopLight",
                              "value": true
                            },
                            {
                              "op": "any",
                              "conditions": [
                                {
                                  "op": "eq",
                                  "parameter": "columns",
                                  "value": 2
                                },
                                {
                                  "op": "eq",
                                  "parameter": "columns",
                                  "value": 3
                                }
                              ]
                            },
                            {
                              "op": "any",
                              "conditions": [
                                {
                                  "op": "eq",
                                  "parameter": "rows",
                                  "value": 2
                                },
                                {
                                  "op": "eq",
                                  "parameter": "rows",
                                  "value": 3
                                }
                              ]
                            }
                          ]
                        },
                        {
                          "op": "eq",
                          "parameter": "rows",
                          "value": 3
                        }
                      ]
                    }
                  }
                ]
              }
            ]
          }
        ]
      },
      {
        "op": "eq",
        "parameter": "screenEnabled",
        "value": true
      }
    ]
  }
}
```
