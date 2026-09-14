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
