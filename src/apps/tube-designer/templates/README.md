# TubeDesigner 模板目录

系统安装目录和用户数据目录使用同一套资源结构：

```text
template/
├─ product/<id>/template.json + template.py + resource/
├─ profile/<id>/profile.json + profile.py + resource/
├─ mold/<id>/tool.json + tool.py 或 geometry.json + resource/
└─ accessory/<id>/model.json + resource/
```

安装目录中的根是 `apps/tube-designer/templates`，属于系统内置资源；用户根是
`%LOCALAPPDATA%/iCAX/icax.tube-designer/template`，属于用户资源。
程序按目录内容自动发现模板，不在 JavaScript 或 C++ 中登记具体模板 ID。用户根跟随系统用户数据目录，卸载或重装程序时不应清理。

当前 `product/` 包含 6 个公开产品和 3 个装配验证模板。百叶窗、普通铝合金窗、雕花装饰门和极简防护网已移出活动目录，其规格、参数与参考实现保存在 [暂缓产品实现档案](../docs/deferred-products/index.md)，不随运行包部署。

## 产品编辑入口

全部产品使用公共编辑流程，不能按模板 ID、名称或产品类别切换另一套界面或提交路径。

- 结构只在添加产品时选择，创建后固定；`parameterDependencies.creationOnly` 声明实际宿主结构字段，前端和原生更新、重新生成入口共同校验。场景无“结构与布局”面板，创建后无“更改结构”入口。
- 尺寸和布局数值在创建后通过场景标注双击修改，包括分跨数量、截数、首末偏移；不能因为尚无几何尺寸线就隐藏或锁定这些数值。精确标注由生成脚本提供，其他数值由模板 `sceneSpecificationAnnotations` 声明参数引线；数量或角度不绘成长度尺寸。
- 产品编号、生产数量在右栏；用料、管型和装配按模板声明分组。修改用料、管型或截面参数使场景模型过期，重新生成后才能拆单。`parameterDependencies.manufacturingOnly` 只声明装配工艺字段，其修改仅使零件清单过期。右栏编辑归属与模型依赖分开，不能用免重新生成声明限制用料编辑。实例编号、数量不是几何参数。
- 标注与右栏共用 `UpdateProductParameters` 和项目撤销／重做。尺寸／布局修改沿用模型过期、确认重新生成流程；结构固定不能阻止尺寸重新生成。条件统一复用 `parameterConditions.mjs`，保留不适用字段的草稿。
- 默认值来自当前模板，不按产品名称或 ID 分支；参数响应使用公共局部更新机制，保留左右栏最新焦点、选区、各层滚动、节点和监听器。

实例标题通过 `display.shared.instanceSummary` 声明。数组中首个满足 `visibleWhen` 的条目生效；`text` 中的 `{parameter}` 格式化数值，`{parameter:choice}` 读取当前模板真实选项的显示名。未声明标题时使用模板的主尺寸声明。默认视角可由 `display.views.scene.viewDirections` 的 `when` 和三维 `direction` 声明；条件仍复用公共解释器。

机制回归包括 `ProductParameterMechanismTest.mjs`（全部 9 个模板及更名不变性）和 `ProductParameterMechanism.browser.mjs`（6 个公开模板、原生生成的标注、实际 WebGL 双击编辑、异步焦点与滚动）。原生预览必须另行覆盖实际模板，浏览器的可控传输不能代替原生校验。

## 参数显示分级

产品、管型、模具参数通过自身描述符的 `presentation` 声明界面行为：

```json
{"key":"outerCorners","valueType":"string","defaultValue":"","presentation":{"advanced":true,"visible":true}}
```

- `advanced`：布尔值，默认 `false`。高级参数在编辑器和示意图参数说明中统一放入默认折叠的“高级设置”；对应图上标记也默认隐藏。展开后显示高级标记，并沿用正常编辑与高亮联动，收起后再次隐藏。
- `visible`：布尔值，默认 `true`。`false` 固定隐藏该参数的输入、说明、图上标记以及 Excel 列选择；原生 Excel 导出和导入也拒绝将它声明为数据列。隐藏参数仍保留在模板内部，生成时使用默认值。需要随其他参数变化显示时，仍使用参数的 `visibleWhen`，最终可见为两者同时满足。Excel 各行可以使用不同结构条件，因此动态 `visibleWhen` 不按导出时的默认值裁掉列。
- 隐藏或折叠是展示规则，不会清空草稿、改变生成输入，也不代表生成脚本可以忽略该参数。实际适用分支仍由模板脚本负责。
- 不按字段名、JSON 类型或模板 ID 推测分级。新增模板只需自己声明，无须修改前端登记表。

## 产品模板引用管型和模具

产品模板不能复制一份“圆管／矩形管”目录，也不能把某个模具的算法抄进产品脚本。产品只声明构件或工序角色，实例保存所选资源的来源、稳定 ID、版本、摘要和参数。

管型字段使用 `presentation.editor = "profile-library"`，并在 `extensions.resourceRoles.profiles` 中显式声明“参数字段 → 构件角色”：

```json
{
  "extensions": {
    "resourceRoles": {
      "profiles": {
        "frame": { "parameter": "frameProfile" }
      }
    }
  },
  "parameters": [{
    "key": "frameProfile",
    "displayName": { "zh-CN": "外框管型" },
    "valueType": "string",
    "defaultValue": "",
    "presentation": {
      "editor": "profile-library",
      "resourceRole": "frame",
      "allowedScopes": ["system", "user"],
      "allowedForms": ["parametric", "fixed"],
      "allowExternalDxf": true
    }
  }]
}
```

选择结果保存在 `parameters.tubeDesignerProfileOverrides[resourceRole]`。其中包含当前实例实际采用的精确二维轮廓及管型参数；产品脚本继续通过 `_shared/tube_profile_catalog.py` 的 `load_profile(parameters, resourceRole)` 取得截面。选择程式管型后，原模板内同角色的宽、深、圆角和壁厚字段不再重复显示。

模具字段使用 `presentation.editor = "tool-library"`，并在 `extensions.resourceRoles.tools` 中显式声明。`targets` 和 `categories` 是资源选择约束，不是按模具 ID 写死的判断：

```json
{
  "extensions": {
    "resourceRoles": {
      "tools": {
        "frameCornerGroove": {
          "parameter": "frameCornerGrooveTool",
          "targetProfileRole": "frame",
          "targets": ["part"],
          "categories": ["slot"]
        }
      }
    }
  },
  "parameters": [
    {
      "key": "frameCornerGrooveTool",
      "displayName": { "zh-CN": "外框转角开槽模具" },
      "valueType": "string",
      "defaultValue": "",
      "presentation": {
        "editor": "tool-library",
        "resourceRole": "frameCornerGroove",
        "allowedScopes": ["system", "user"],
        "targets": ["part"],
        "categories": ["slot"]
      }
    }
  ]
}
```

选择结果保存在 `parameters.tubeDesignerToolBindings[resourceRole]`，协议为 `icax.product-resource-binding`。绑定只保存可信资源引用、参数和不可执行的描述快照；模板脚本无权遍历系统或个人资源目录。原生层会拒绝未声明的管型／模具角色、无目标管型的模具角色、跨模板引用和非法参数对象。产品的展示逻辑与加工逻辑仍然分开，但两者读取同一份资源绑定，避免各维护一套选型数据。

## 产品模板示意图

产品模板的卡片图标和参数示意图也属于模板包本身，不在前端脚本中登记。放在产品模板自己的 `resource/` 目录，并使用以下约定文件名：

```text
product/<id>/resource/icon.svg       # 左侧卡片图标
product/<id>/resource/schematic.svg  # 产品卡片、添加产品、零件清单使用的示意图
```

两个文件都是可选的 SVG。系统模板启动枚举时会把它们作为模板展示数据提供给界面；个人 `.itpt` 模板则直接从包内资源读取。模板没有资源时，界面只显示通用占位图，不会因为缺图去修改中心 MJS。若需要自定义文件名，可在 `extensions.catalog` 中用 `icon`、`schematic` 指定相对于模板目录的资源路径。

钢楼梯结构示意使用模板 `productDiagram.kind = "layered-svg"` 和带 `visibleWhen` 的 SVG 图层声明。梯梁、踏步支撑、踏板和路线的图层条件复用 `parameterConditions.mjs`，添加控件更改后即时重绘；前端不登记楼梯模板 ID 或物理参数键。
