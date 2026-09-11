# 管型包

每个管型位于独立目录中：

```text
profiles/<管型ID>/
  profile.json
  profile.py
```

`profile.json` 使用版本 2 的 `icax.tube-profile-descriptor`，声明稳定 ID、版本、名称和 `parameters` 参数数组。每个参数声明 `key`、`valueType`、显示名称、`defaultValue` 以及适用的 `min` / `max` / `step`；这些默认值既供管型库独立预览，也在产品模板没有传值时作为后备值。加载器按 `<模板前缀><参数名首字母大写>` 读取实际参数，因此新增管型自己的参数不需要修改共享加载代码。

`profile.py` 提供两个入口：

- `build(parameters)`：校验 JSON 声明的参数，返回截面边界、壁厚、规格文本以及脚本需要保留的其他数据。
- `contours(profile, clearance, swap_axes)`：一次返回中性模型 `profile2d` 使用的非空轮廓数组。第一个元素是外轮廓，后续元素都是内孔；实心或开口型材只返回一个元素。

产品模板的 `template.json` 把管型 ID 写入 `*ProfileType` 枚举，产品 `template.py` 通过共享目录加载管型包。新增标准等截面管型不需要修改或重新注册 C++ 类型。

当前内置管型：

- 空心管：`rect` 矩形管、`round` 圆管、`ellipse` 椭圆管、`flat-oval` 腰圆管、`polygon` 多边形管（正多边形 / 星形）。
- 开口/实腹型材：`angle` L 型角钢、`channel` C/U 型槽钢、`i-section` 工字钢/H 型钢、`t-section` T 型钢、`z-section` Z 型钢。

管型库直接扫描上述目录并把它们显示在“系统内置”分组中。系统定义不会复制到 `usr.db`，因此不能重命名或删除；参数可以在当前页面修改，用于截面、三维预览和导出。用户导入的可编辑包与 DXF 则单独显示在“我的管型”分组中。

管型脚本只输出 `circle`、`ellipse`、`capsule`、`roundedRectangle`、`polygon` 或通用 `path` 二维轮廓。`path` 可以混合直线、圆弧、椭圆弧、贝塞尔、B 样条和 NURBS 曲线段。C++ 中性模型层只负责按数组顺序把外边界和内孔构造成一个截面，再执行一次标准拉伸；它不包含上述管型的业务枚举、尺寸规则或管壁布尔逻辑。

第三方 ASCII DXF 通过 `_shared/dxf_profile_importer.py` 转换成同一轮廓数组。实例参数中的 `tubeDesignerProfileOverrides` 按管型前缀保存冻结的轮廓快照；DXF 管型不接受宽度、深度、壁厚或圆角参数修改。只有用户明确“保存为我的管型”时，轮廓副本才作为 `profile/imported-dxf` 记录写入用户库。

## 可编辑管型包

第三方可编辑管型使用扩展名 `.ittt`。它本质上是使用产品固定 magic number 加密的 ZIP 压缩包，根目录只允许：

```text
profile.json
profile.py
```

压缩包统一使用 ZIP 传统密码保护，密码由产品固定 magic number 自动处理，用户不需要输入或保存密码。AES ZIP 不属于 Python 标准库支持范围，导入时会明确提示加密格式不受支持。

可编辑包沿用版本 2 的 `icax.tube-profile-descriptor`。与内置管型不同，第三方包的每个参数都必须声明 `defaultValue`，并可选声明 `min`、`max`、`step` 和 `options`。脚本入口仍是 `build(parameters)` 与 `contours(profile, clearance, swap_axes)`。

导入时会限制压缩包与解压内容大小、拒绝目录穿越和额外文件，并用默认参数执行脚本、构建一个短拉伸体验证轮廓。可编辑包包含可执行 Python 脚本，因此只能导入可信来源。

管型库保存包的描述、脚本、默认参数和默认截面。产品选择管型后保存的是一次求值产生的具体轮廓快照及实例参数；以后修改或删除库中管型不会改变已经存在的产品实例。
