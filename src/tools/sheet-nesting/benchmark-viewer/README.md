# 排样实时报告

`/materials.html` 汇总 `TestData/Plane/catalog/catalog.json` 的来源压缩包、集中 DXF 和排样任务。可以按来源包、可运行、缺素材、参考资料、默认单位和本轮结果筛选；任务详情列出原始数量来源、统一 PK 条件与原始板材信息。未声明数量的默认值单独标记：DXF 来源按 1 份原文件，原始轮廓按原零件 1 件。输入受限和等待排样不计为已经执行。

`task_parts.csv` 每行对应一项原始来源，`quantity` 表示原始 DXF 导入份数；同文件内的材料分量另列 `components_in_dxf`，实际排件总数另列 `native_pieces`，对应原生规格 ID 保留在 `native_part_ids`。例如一份 DXF 内有 7 个独立材料分量，CSV 和网页均为一行、导入 1 份、实际排 7 件，只提供一个原始文件下载，不能把它写成导入 7 份 DXF。原始轮廓来源的 `quantity` 仍为该原零件的需求数量；未提取到材料分量时保留空值。`tasks.csv` 中 `source_specifications/source_quantity` 记录原始来源种数和需求份数，`specifications/required` 记录原生材料规格数和实际排件需求。数量展示与 CSV 整理不改原始文件、原生输入、几何或布局。

原始轮廓已经存在的任务可以直接运行，缺少预转换的 DXF 只标记为“DXF待按需导出”，不当作缺少排样输入；仅已存在的原始 DXF 提供下载链接。原始轮廓无效单独筛选并保留来源与校验证据。首次排齐、最佳方案找到、整轮耗时与进程总耗时分别显示；`bestFoundWallSeconds`／CSV 可选列 `icax_best_found_seconds` 仅采用与最终原件和布局精确匹配、完整合法的同名邻接 `.incumbent.json` 发布时间，时间须有限且在整轮耗时范围内，缺失或不匹配时不推测。来源卷料另列固定幅宽下的占用长度和占用段利用率，整板利用率保留原值。规则余料只读取原生 `remnants`，已认证矩形显示原始尺寸、坐标和面积，未评估与已评估未找到分别显示；面积上界不当作已认证余料。

素材接口只读：`/api/materials/summary`、`/api/materials/tasks`、`/api/materials/tasks/{id}`、`/api/materials/assets`、`/api/materials/assets/{id}`。任务和素材列表支持 `offset/limit`（每页最多 200）、`collection/filter/q`。下载仅允许四份指定 CSV 与集中 `DXF` 目录中按素材 ID 找到的 DXF，不接受任意文件路径。

`catalog/results/{taskid}.json` 的 `{taskId, runSignature, benchmark}` 记录会增量覆盖任务结果。任务 ID 与输入 SHA-256 必须匹配；旧输入结果标记为 `stale-input`，不会算作当前成绩。`catalog/progress.json` 单独提供本批实际进度。图形读取 `benchmark.problem` 原轮廓及 `layouts` 位姿，按统一物理比例绘制所有板；缺原轮廓时明确不画推测图。

本地只读查看原始输入排样批测。每个案例计算结束后自动出现原生结果，独立审核与目标计分到达后继续补全；不运行求解器，不改输入、布局或计分规则。

使用 Python 3 启动：

```powershell
python src/tools/sheet-nesting/benchmark-viewer/server.py --batch output/research/plane-nesting/20261107/all-previous-all-initial-03 --port 8768
```

打开 `http://127.0.0.1:8768/`。服务仅监听本机，页面每 4 秒自动刷新。可以按来源、比较资格、完整性、目标与时限筛选，选中案例查看原轮廓排样图、原参数、未排需求和核验信息。后台更新保留筛选、输入选区、当前案例、滚动与缩放。

`/construction.html` 查看通用原生构造事件。按任务、路线、独立分支及阶段筛选，可以逐事件回放候选与真实状态，查看各路终局和调度比较的完整评分键。图形只显示事件本身或同路线、同分支、同状态编号的已记录快照；没有快照就明确显示缺失，不从最终布局补造历史。

素材任务详情中的“查看本任务各策略构造过程”打开 `/construction.html?materialTask=<任务ID>`。接口 `/api/construction/materials/{任务ID}` 及其 `/events`、`/events/{ordinal}`、`/index`、`/result` 读取本轮 `catalog/live` 或 `catalog/results` 中 `nativeConstructionTrace` 指向的原生记录，校验输入 SHA、程序 SHA、完整运行条件和原生结果邻接路径；缺少记录时保留该任务并明确显示缺失，不跳去旧案例。原生过程增长后页面持续更新，保留当前事件、路线、焦点、滚动和已绘制的状态图。

素材任务的过程采集有每路细节、进展、终态和最终选择的独立份额。页面显示采样省略、进展省略、过大终局快照省略及观察写入异常，采样省略不等于算法失败。没有完整快照时只显示实际评分和摘要，不能把最终选中布局补作其他策略的终局。素材任务成绩保留实际记录开销；几何检查、排齐结论与过程采集失败分别显示。

生产 CLI 使用 `SheetNestingTask --initial <problem> <output.json> <seconds> <attempts> <workers> --trace <邻接.trace-index.json>` 开启有界采集，素材 runner 默认开启，可显式 `--without-construction-trace` 关闭。采集模式进入运行签名，原始结果声明真实索引文件；报告中的描述符绑定输入 SHA、程序 SHA、完整运行条件与本次原生输出。CLI、runner 和网页各自保留采集失败及省略标记，不将只完成排样视作过程记录也已完整。

过程图与素材终局图共用原件显示范围计算。过程图可切换“已记录区域 + 空白”和“整板”，百万长度的卷料默认只显示真实已记录原件及当前候选覆盖的区域；坐标原点、原板尺寸与零件坐标保持不变，各板采用同一物理比例。显示裁切不改变布局，不用于推导占用段利用率或宣称排样改善。

在既有 `SheetNestingPublicBench` 的 `construct` 模式中，给任意 `plane.*` 策略 ID 加 `.trace` 后缀即可采集，包括 `plane.initial.trace`。这一旧诊断入口默认关闭观察，开启后写出同目录的 `.construction.jsonl` 全事件流与 `.trace-index.json` 索引；未设事件或字节截断只适用于该旧入口，不适用于上面的生产 CLI。有界采样和搜索的有限候选/状态截断分别记录。记录会消耗时间，旧诊断入口的采集条件和耗时不能充当当前生产速度验收。

旧专项过程目录为工作区内 `output/research/plane-nesting/20261108/construction-traces`，本轮素材过程文件由绑定记录限定在 `TestData/Plane/catalog/raw` 内；读取服务使用文件偏移分页，不把整份事件流送给浏览器。`/api/construction/tasks` 同时列出旧专项和已绑定的本轮素材任务。旧专项的 `/api/construction/{task}`、`/api/construction/{task}/events`、`/api/construction/{task}/events/{ordinal}` 继续提供元信息、分页和单事件。原始文档审查也由页面提供固定链接。

旧批次已停止。几何合法、排齐、板数/占长满足参考都是独立事实，不能标成布局质量合格。对照栏已改为中性的参考标量说明。

报告批次由 `run-configuration.json`、`indexes/*.json`、`executions/*.json`、`reviews/*.json` 和 `case-scores.json` 提供。执行与审核记录先于汇总到达时，页面明确显示待核验或待计分，不能据原生完整标志提前宣称目标达到。图形只读取本轮原件、孔、解析圆与最终位姿，所有板使用同一物理比例。

可选的 `highlights.json` 是专项结果的数据清单，每项包含 `id/title/verification/raw/image/target/description`。专项重复测试独立显示，不代替正式批次中的案例执行计数。

接口只有读取功能：`/api/summary`、`/api/cases`、`/api/cases/{index}`、`/api/g025`（当前专项）、按案例标识访问的证据文件及既有报告下载。停止服务可在启动终端按 Ctrl+C；关闭浏览器不停止正在运行的批测。

验证接口增量到达、审核绑定与两种原生输出格式：

```powershell
cd src/tools/sheet-nesting/benchmark-viewer
python -m unittest -v test_server.py
```
