# 极简防护网 1.2.1 历史归档

极简防护网 `minimal-protective-grille` / `1.2.1` 曾按用户要求从活动产品目录移除。本目录保留这一历史版本，不作为运行包部署的活动模板。

旧版规格、参数、脚本、显示声明和资源完整保存在 [reference](reference/README.md)。归档的 66 个物理参数、边框与内杆排布、把手禁区、接头和安装孔实现仅供历史参考；[archive.json](archive.json) 记录原模板快照文件及已核对的 SHA-256。

这份参考源码不作为独立可执行模板，原有共享层相对路径未改写，也不提供兼容或迁移入口。

2026-10-26 根据新的客户反馈重做了独立的 [2.0.0 活动模板](../../../templates/product/minimal_protective_grille/template.json)，与防盗窗并列。新版采用局部把手净口，保留完整横杆，并支持两竖管、四边框、下横管及创建后布局编辑；不沿用旧版整行删杆的把手禁区语义。

旧版三份测试已原样移至 [reference/tests](reference/tests/)，文件内容及 SHA-256 在移动前后核对一致：

- `MinimalProtectiveGrilleTests.py`
- `MinimalProtectiveGrilleAssemblyTests.py`
- `MinimalProtectiveGrilleSourceSpanTests.py`

这些测试针对历史 1.2.1 布局与已撤销的 `generate()` 接口，不参与现行测试发现，不为其增加兼容入口。当前测试入口为 `src/tests/icax-plugins/product/TubeDesigner/TubeDesignerTest/MinimalProtectiveGrilleProductTests.py` 与同目录 `MinimalProtectiveGrilleManufacturingTests.py`；前端参数及目录回归为 `src/tests/iCAX-UI/MinimalProtectiveGrilleProductTest.mjs`。
