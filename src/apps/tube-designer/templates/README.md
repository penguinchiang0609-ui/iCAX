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

## 产品模板示意图

产品模板的卡片图标和参数示意图也属于模板包本身，不在前端脚本中登记。放在产品模板自己的 `resource/` 目录，并使用以下约定文件名：

```text
product/<id>/resource/icon.svg       # 左侧卡片图标
product/<id>/resource/schematic.svg  # 产品卡片、添加产品、零件清单使用的示意图
```

两个文件都是可选的 SVG。系统模板启动枚举时会把它们作为模板展示数据提供给界面；个人 `.itpt` 模板则直接从包内资源读取。模板没有资源时，界面只显示通用占位图，不会因为缺图去修改中心 MJS。若需要自定义文件名，可在 `extensions.catalog` 中用 `icon`、`schematic` 指定相对于模板目录的资源路径。
