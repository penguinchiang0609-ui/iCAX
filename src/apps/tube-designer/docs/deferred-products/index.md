# 暂缓产品实现档案

这四款产品已从当前运行模板目录移出，保留当时的规格、实现和完整源码供后续整理。文档描述归档时已存在的行为，不表示这些产品仍可在当前产品目录中添加，也不表示已完成生产验收。

| 产品 | 模板标识 / 版本 | 当前实现说明 | 参考源码 |
| --- | --- | --- | --- |
| 百叶窗 | `louver-window` / `2.1.0` | [规格与实现](louver-window/README.md)、[35 项参数](louver-window/parameters.md) | [reference](louver-window/reference/template.py) |
| 普通铝合金窗 | `aluminium-window` / `1.1.0` | [规格与实现](aluminium-window/README.md)、[35 项参数](aluminium-window/parameters.md) | [reference](aluminium-window/reference/template.py) |
| 雕花装饰门 | `decorative-door` / `1.0.0` | [规格与实现](decorative-door/README.md)、[45 项参数](decorative-door/parameters.md) | [reference](decorative-door/reference/template.py) |
| 极简防护网 | `minimal-protective-grille` / `1.2.1` | [暂缓说明](minimal-protective-grille/README.md)、[规格与实现](minimal-protective-grille/reference/README.md) | [reference](minimal-protective-grille/reference/template.py) |

## 归档位置与完整性

原目录分别是 `templates/product/louver_window`、`aluminium_window`、`decorative_door`、`minimal_protective_grille`。各原目录整体移动至上述产品子目录的 `reference/`；脚本、描述符、README、图标、示意图、系列数据与图案数据保留原文件内容。各产品的 `archive.json` 记录所有非缓存文件的原路径、归档路径、长度及 SHA-256，并记录移动后的核对结果。缓存也随目录保留，但不作为源码完整性的依据。

`reference` 是参考快照，**不是可直接部署或独立执行的模板包**。原实现按 `Path(__file__).resolve().parents[2] / '_shared'` 定位共享层；移动后该相对路径不再指向运行模板共享目录。没有为了归档改写这些导入或加入兼容入口。研究用测试如需读取参考实现，应由专用测试加载器显式指定共享资源路径，不能将其重新加入活动目录扫描。

共享几何、管型、装配工艺和 SDK 仍由当前项目维护，没有复制进这四份参考包。`dependencies.json` 记录本次阅读的外部源码文件及其指纹，帮助理解原依赖版本；它不是依赖安装清单，也不保证脱离原项目后可运行。

## 公共接口与证据范围

归档时的公共产品入口是 `display(parameter_values)` 与 `manufacturing(parameter_values)`。前者输出 `icax.display-model` v2；后者输出 `icax.manufacturing-model` v4 的连接及工艺声明，由独立加工执行器完成加工。内部 `NeutralModel` 记录和旧测试中的 `generate` 调用不等同于公共接口：公共结果不再携带内部 `parameters`、`template` 或内部扩展表。

v4 制造声明只有 `schema/schemaVersion/connections/processes`；每个 process 的 definition 包含 templateId、processInput、parameters、processDrafts、targets 或 outputs、dependencies。SDK 将内部加工调用的 v1 数据转换为带 scope/itemKey/state 的 `icax.assembly-process-input` v2 引用；宿主以已保存的显示设计对象提供初始几何，`compose_manufacturing_model` 才组装私有 v3 执行输入。私有输入不是第二份产品对象表，产品入口也不能把完成加工的实体冒充纯声明。协议来源为 `src/iCAX-Engine/framework/TemplateRuntime/python/icax_template_sdk/display.py` 的 `to_display_model`、`manufacturing.py` 的 `_to_manufacturing_input_model/to_manufacturing_model/compose_manufacturing_model`，以及 `templates/_shared/assembly_geometry_process_runtime.py` 的 `invoke`。

这三款归档产品只核对了文档、源码、参数声明和归档指纹，没有重新执行其原生 CAD、浏览器或制造验收。文档列出的既有测试是实现意图与定位资料，不能当成本次通过记录；其中部分历史测试还引用旧入口或旧模式名。活动产品清单及相关运行测试不再把这三款作为当前可用产品。
