# 装配工艺模板

每种装配工艺独占一个目录和一份 `assembly.json`，例如 `bend/assembly.json`。目录名必须等于文件中的 `id`。资源库通过扫描 `assembly/*/assembly.json` 发现模板；新增目录或删除目录后，点击装配工艺列表上的“刷新模板”重新读取。无效模板不会进入列表，错误会由模板目录名和校验信息指出。

示例场景的工艺列表按连接形态折叠分组。折叠分组只改变列表显示；选择工艺卡片时检查当前成品，不适用时保留成品并显示原因。成品造型和尺寸在独立成品区编辑；只有用户点击“载入示例成品”才使用模板提供的示例替换当前成品。选择工艺默认只显示成品，点击“下料件”后才计算工艺方案和下料几何。同一成品输入与管型版本共用成品缓存，工艺切换或工艺参数变化不会重新生成成品。产品节点模式继续按实际连接提供工艺模板。

冷折弯使用 `bend` 模板；开槽折弯不再通过其中的“折弯方式”或“槽口工艺”切换，而是由 `segmented-bend`、`node-v-notch-integrated`、`node-embedded-arc-integrated`、`node-edge-arc-integrated`、`flexible-slit-bend-integrated` 五份并列模板分别声明。删除任一目录，即从资源库移除该具体工艺。它们的单件槽口工具通过各自 `partProcesses[].resource.id` 引用，仍可复用模具模板中的具体参数。

## 一份模板定义什么

- `participants` 定义 2～4 个**逻辑管段角色**，如 `segmentA`、`segmentB`。它们是连接关系中的输入，未必等于最终下料件。
- `layoutShape` 定义资源库左侧按**默认成品连接形态**分组：`l`（L 形）、`t`（T 形）、`cross`（十字形）、`straight`（共线形）或 `orthogonal-corner`（三向直角）。它与 `category` 的连接关系分类、`manufacturingPlan` 的下料实现方式彼此独立。模板必须声明此字段。形态为模板的默认分组，调整参数不会使卡片在分组之间跳动：例如四端汇交可调为空间四向，仍留在十字形组。当前不预置 Π 形双支模板；同一主管上的两个 T 连接分别选用 T 形工艺。
- `parameters` 只定义工艺参数、默认值、范围和 `visibleWhen` 条件，不得声明 `scope: scene`。显示分组也由模板的 `level` 决定：`basic` 直接显示，`advanced` 放入“更多参数”；条件字段可声明为 `basic`，在上级选项启用后直接显示。前端通用编辑器读取这些声明，不按模板 ID 或参数名补写显示规则。成品形状、尺寸、截面和长度统一定义在独立的 `templates/finished-product/shapes.json`。`inputContract` 声明工艺实际需要的管段角色、几何量及范围；`exampleInput` 只记录示例成品的造型、成品区域到角色的映射和必要几何量来源。表达式可用 `$angle`、`$setback` 等引用输入或工艺量；可用 `xxxVariants` / `when` 选择适用分支。隐藏的子参数保留草稿值，但不会继续驱动不适用的工艺分支。
- `manufacturingPlan` 定义 `integrated`、`separate` 或 `hybrid` 实现方式，以及 `blankParts`。每个逻辑角色必须且只能归属一个下料件；一体成形必须将多个逻辑管段归并为更少的下料件。
- `partProcesses` 引用 `templates/mold/<工艺 id>/tool.json` 中的单件工艺，声明作用角色、`appliesWhen`、参数映射和加工落点。`previewScene.manufacturingParts[].processes` 再把这些工艺引用放到具体下料件上。资源库预览会据此请求原生单件模型及加工结果。
- `assemblyPath`、`bomRules`、`dimensionChecks` 分别描述工序、辅料和检查；`outputs.manufacturingPartCount` 必须与 `blankParts` 数量一致。
- `previewScene` 不得定义 `designParts`。其 `manufacturingParts` 给每个下料件指定源成品区域对应的工艺角色、长度公式、加工引用和下料展示位置。`parameterDiagram` 描述工艺参数，成品参数图使用独立造型目录的定义。宿主可临时绑定 `inputContract` 的角色及必要几何量来验证表达式；这些内部字段不得保存回模板或放进公开模板目录。
- `catalogueDiagram.paths` 是左侧列表缩略图的成品连接轮廓，使用 `0 0 64 64` 坐标和 SVG `d` 路径；每条路径的 `tone` 为 `a`、`b`、`c`、`joint`、`cut`、`detail` 或 `void`。它由每份模板声明，不按模板 ID 在界面里写死。缩略图应画出实际连接位置和必要的接缝／切口，不能把 L 形端接画成 T 形侧接。
- `previewScene.preferredView` 可选，声明示例首次出现时的相机方向，如 `back-top-right`。方向可由 `front`/`back`、`left`/`right`、`top`/`bottom` 中不同轴的词组合，也可用 `iso`；同一模板参数变化或成品／下料件切换后保留用户调整的相机。

最小的**字段关系片段**如下；它不是独立可运行的完整文件。新建工艺时，先复制最接近的现有 `assembly.json`，改名并修改其角色、参数、工艺和预览声明，再删除不适用的内容。

```json
{
  "schema": "icax.assembly-template",
  "schemaVersion": 1,
  "id": "my-joint",
  "participants": [{ "role": "segmentA", "label": "A" }, { "role": "segmentB", "label": "B" }],
  "inputContract": {
    "schemaVersion": 1,
    "roles": ["segmentA", "segmentB"],
    "geometryParameters": [{ "key": "angle", "displayName": "转角", "valueType": "number", "defaultValue": 90, "min": 1, "max": 170, "unit": "°" }],
    "requirements": {}
  },
  "exampleInput": {
    "shapeId": "l",
    "roles": { "segmentA": "armA", "segmentB": "armB" },
    "parameterBindings": { "angle": "angle" },
    "requirements": {}
  },
  "manufacturingPlan": {
    "realization": "integrated",
    "blankParts": [{ "id": "jointBlank", "label": "连续下料件", "participants": ["segmentA", "segmentB"] }]
  },
  "previewScene": {
    "manufacturingParts": [{ "id": "jointBlank", "sourceRole": "segmentA", "length": "$segmentA_length + $segmentB_length", "processes": [], "explodedPose": {} }]
  }
}
```

`$segmentA_length` 这类量从传入的成品区域派生，只供内部公式使用，不会成为用户参数。预览请求分开传入 `finishedProduct`（完整成品输入）和 `parameters`（工艺参数）。成品由自己的输入生成并缓存；工艺模板不得改写传入成品，修改或重置工艺也保留当前成品。切换至不适用的工艺时仅显示原因。资源库示例也可分别传入工艺 `parameters`、`sceneParameters` 和 `sceneParts`；工艺参数不接收场景字段，示例场景输入不得与完整成品输入混用。

## 成品适用性函数

每个模板目录提供 `applicability.py`，定义 `check_applicability(finished_product, parameters)`，返回 `{"applicable": True, "reason": ""}` 或 `{"applicable": False, "reason": "具体原因"}`。第一个参数是独立的完整成品，第二个是宿主规范化后的工艺参数。函数直接读取成品的造型、截面、长度与姿态判断，不生成模型或加工计划，不得修改任一输入。

可单独调用 `TubeDesigner.CheckAssemblyTemplateApplicability`，传入 `{templateId, finishedProduct, parameters}`。宿主先验证完整成品格式及 `inputContract` 的必要输入，再调用该模板的函数，返回 `{schema: "icax.assembly-applicability", schemaVersion: 1, templateId, applicable, reason}`。截面一致性、配合和工艺适用规则由模板函数检查。`ResolveAssemblyTemplatePreview` 收到完整成品时使用同一个检查入口，不适用则拒绝工艺计算并保留成品。

适用性通过表示模板的成品输入条件满足；实际加工实体、装配配合和干涉仍需原生生成与核验。模板需要但尚未实现的截面判断必须明确拒绝，不能用生成候选模型替代输入检查。成品编辑器位于 `webpage/finishedProductEditor.mjs`，仅读取独立造型目录、成品输入和管型资源；其字段、折叠状态和尺寸草稿均不以工艺模板 ID 保存。模板内部的只读角色适配不用于渲染成品编辑区。

## 模板示例成品函数

每个模板目录提供 `example.py`，定义 `get_example_product(parameters)`，返回一份完整的 `icax.finished-product` 输入。示例的造型与必要尺寸由该模板选择，成品数据不得包含工艺参数，也不得修改传入的规范化工艺参数。共享层仅提供独立成品输入创建及截面数据设置函数，不按模板 ID 选择造型或尺寸。

场景先调用适用性检查，始终保留当前成品及其缓存模型。显式点击“载入示例成品”时调用 `TubeDesigner.GetAssemblyTemplateExampleProduct({templateId, parameters})`，返回 `{schema: "icax.assembly-example-product", schemaVersion: 1, templateId, finishedProduct}`。宿主使用同一适用性入口验证示例后返回。这个接口不生成成品模型或下料计划；成品模型单独通过 `ResolveAssemblyTemplatePreview({finishedOnly: true, finishedProduct})` 获取，下料在场景切到下料件时请求。

成品区域的长度是目标外形尺寸。插入深度、插舌啮合等属于工艺额外原料，由 `manufacturingParts[].stockLengthAddition` 推导；不能从成品长度扣除。独立造型目录修改后运行 `src/tools/build/generate_finished_product_shapes.py` 更新浏览器初始目录，部署脚本会检查两份目录一致后才复制资源。

## 成品预览和标注

`previewScene.formedPreviews` 可按 `when` 选择成品外形配方。目前内置 `continuous-cold-bend`（连续冷折）和 `node-groove-fold`（节点开槽折弯）。配方需要指向已声明的 `blankId` 和按设计管段顺序排列的 `roles`；前者还需要角度、半径和 K 因子，后者需要角度与已声明的槽口 `processId`。这是**成形外形示意**，不是可交付的实体建模结果。

`previewScene.annotations` 声明成品（`finished`）或下料件（`blank`）的标注。当前支持 `length-role`、`length-blank`、`value-note`；前两者分别引用 `role`、`blankId`，后者给出 `value`。例如：

```json
"annotations": [
  { "id": "a-length", "view": "finished", "kind": "length-role", "role": "segmentA", "label": "A 段设定长" },
  { "id": "blank-length", "view": "blank", "kind": "length-blank", "blankId": "jointBlank", "label": "总下料长度" },
  { "id": "angle", "view": "finished", "kind": "value-note", "label": "折弯角", "value": "$angle", "unit": "°" }
]
```

## 特殊工艺的同目录脚本

声明 `"previewScene": { "formedPreviewScript": "assembly.py", ... }` 后，可在该模板目录放 `assembly.py`，实现以下一个或两个函数：

```python
def build_plan(plan):
    # 可修改下料长度、加工请求或下料位置；保留成品输入、设计件及计划结构。
    return plan

def build_formed_preview(plan):
    # 数组按 xyz 三元组和三角面顶点索引排列，长度单位为 mm。
    return {"kind": 1, "positions": [0, 0, 0, 10, 0, 0, 0, 10, 0],
            "indices": [0, 1, 2], "metadata": {}}
```

`build_plan` 先运行，`build_formed_preview` 随后读取它返回的计划。宿主校验脚本结果：不得改变模板身份、输入参数、逻辑角色、下料件数量和归属；加工只能引用该模板声明且当前分支适用的单件工艺。网格须为有限数值、有效三角形索引。示例三角形只说明返回格式，实际模板必须生成完整可见表面。

`segmented-bend/assembly.py` 根据分段槽单件工具计算出的槽数和间距修正示例下料长度，再生成逐槽折转的目标外形网格。嵌入圆弧槽和柔性缝模板也各有同目录脚本生成目标外形预览。所有这类网格只表示目标几何，尚未求解槽口闭合、回弹或实体干涉；嵌入圆弧槽的保留圆弧属于下料切口，不是成品折弯半径。

`two-end-end-angle/assembly.py` 按实际截面和转角计算斜切所需的额外原料及下料件配准；成品外形直接使用独立输入，不能因斜接加工而改变长度或位置。

## 三向直角节点

`orthogonal-corner` 接收三根独立直管共用的端点，三条轴线两两垂直。独立成品造型包含 `armA`、`armB`、`armC`，映射到 `memberA`、`memberB`、`memberC`；这不是主管中间开孔的 T 节点。A/B 可选双端斜接或 A 长件包接 B，C 可选端面贴焊、轮廓插入或圆弧公母。工艺复用 `end-miter`、`branch-profile`、`paired-end-tabs`、`paired-side-slots` 的真实刀具。

斜接时 C 的入口跨越 A/B 的共同角面，两个接收件分别加工自己实际覆盖的材料，C 端只加工一次。包接时由 A 的长端承接 C。成品保持同一中心线与截面；原料余量、端部退切和入口位置均由真实截面及节点坐标推导。模板适用性检查拒绝超出承接范围、进入远侧壁和无法确认的截面方向；示例不代替实际产品截面的核验。当前组合使用三根独立直管，连续折弯 L 与第三件的组合不在此模板的分件加工范围内。

需要复合节点的产品绑定可以声明 `productBinding.operationScript = "assembly.py"`，并列出 `operationScriptDependencies`。同目录脚本提供 `build_bound_operations(context)`，其中 `context` 包含原工艺 `parameters`、宿主已核对的真实 `participants`、当前 `fitChecks` 和全部启用的 `processes`。返回 `parameters` 原样、每个启用工序对应的 `operations` 以及 `nodeContacts`。每个 operation 保留 `processId` 和 `role`，提供完整工具参数 `values` 与加工用 `anchor`；只有需要相邻截面的工具可以附带已核对的 `sectionFrame`。这些加工定位不改写产品的永久端点锚点。

宿主验证脚本未改变输入、工序数量和身份、角色、参数范围与当前适用分支；脚本和声明依赖参与绑定摘要。原生检查候选与最终三件实体的 A/B、A/C、B/C 三对碰撞，并核对模板声明的接触距离和 C 的入口深度。碰撞测量与入口检查仍不等于完整公差或焊接承载认证。

## 验证及现有限制

修改后先检查模板是否能进入资源库，再用实际参数调用 `TubeDesigner.ResolveAssemblyTemplatePreview`，查看下料件数量、加工结果、成品示意和标注。仅能通过 Python 脚本解析，并不能证明原生预览可生成。

真实产品的“应用到产品”当前只支持 `productBinding.mode = "incremental-cut"` 的**分件增量加工**模板，还需要声明各角色锚点和工艺绑定。`integrated` 模板可以在资源库显示一件下料件及成形示意，但尚不能把产品中的多根真实构件合并、替换为一根真实下料件；这类模板应声明 `"productBinding": { "mode": "unsupported", "reason": "..." }`，不要把示意图当作已落地的产品实体。

可绑定模板还必须用 `productBinding.compatibleProductTopologies` 明确声明适用的**成品连接拓扑**，例如 `[{"topology":"L"}]`、`[{"topology":"T"}]` 或 `[{"topology":"straight"}]`。L 表示两件端—端转角，T 表示主管侧面接支管端部，straight 表示同轴端—端。它描述成品节点关系，不描述切口外观，也不按构件数量猜测。需要随工艺参数改变适用节点时，每条声明可加与普通参数条件同格式的 `when`：斜接模板的非零 `jointAngle` 适用 L，零度退化只适用 straight。界面筛选和原生提交都会按当前参数与已提交产品连接的 `properties.topology` 校验。

成品节点锚点和刀具落点分开声明。L 形长短包接的两个产品角色都是 `end`，不能为了在长件侧壁加工而把成品端点伪装为 T 形的 `side`。只有对应单件工艺明确声明以下转换时，绑定运行器才从长件端点推导侧壁刀具位置：

```json
"productBinding": {
  "anchors": {"memberA": "end", "memberB": "end"},
  "processes": {
    "long-side-slots-two": {
      "target": "part", "sectionRole": "memberB",
      "relativePlacement": {
        "kind": "end-inset-side",
        "faceSource": "anchor.approachFace",
        "inset": {"sectionRole": "memberB", "axis": "width", "factor": 0.5}
      }
    }
  }
}
```

产品节点给长件端锚点声明制造局部坐标中的 `approachFace`，表示该端面向支件的侧壁；它属于成品连接方向，不是工艺模板名称。刀具中心距长件所选端点内缩 `支件实际截面宽度 × factor`，起端向内加，末端向内减，主件加工面取节点的 `approachFace`。绑定计划仍保留原始 `end` 锚点；缺少方向、截面尺寸越界、端部余量不为零或未声明转换规则时拒绝绑定。此规则只确定单件加工，不证明两件装配贴合；整件下料仍须跨节点核对所有加工、成品位置与公差。

## 一根构件上的多个节点

产品模板先声明成品构件的逻辑中心线 `assemblyFrame.member.start/end` 和各节点的 L、T 等关系，再另行声明该构件的一根未切原料：`assemblyFrame.member.stockInterval.startStation/endStation` 是沿逻辑中心线的物理原管范围，可以超出两个成品节点。端锚点的 `stockAllowance` 是该端超出逻辑节点的原料余量；`contactInset` 是节点到相邻构件**近侧外表面**的距离。二者来自相邻管型的真实外轮廓投影，非对称截面不能默认各为半个管宽。侧锚点的 `station` 始终是逻辑中心线位置，宿主在加工前转换到物理原料坐标。

不同连接各选一份工艺模板；模板只声明哪些零件、哪些端或侧面需要切，以及参数从何处来。普通长短包接的短件端切量是 `stockAllowance + contactInset`，公母插舌再减插入长度；T 形单面插入在支管物理端回切 `stockAllowance` 到节点中心线，相贯端切还要用主管真实截面生成轮廓刀具。已绑定的刀具按构件身份叠加在同一根原料上，不因同一构件有两个以上节点就生成多件下料。下料结果和成品位置必须经原生坐标映射核对；接头局部实体不相交只能排除明显穿透，不能据此声称配合、公差或装配顺序已经验证。
