# 普通铝合金窗：归档时规格与实现

模板 `aluminium-window`，描述符及内部模型版本 `1.1.0`。原目录为 `templates/product/aluminium_window`，现暂停运行入口；[reference](reference/template.py) 保留窗型生成器、系列读取器、演示系列、原 README 与图标。**随包唯一系列是结构演示，未提供可投产厂家系列，制造请求会拒绝该演示数据。**

默认尺寸 1500 × 1500 mm，单行单列推拉窗、2 扇玻璃、3 轨、开启 1 扇滑动纱窗，玻璃厚度 5 mm；系统来源为 `demonstration`。完整 35 项参数及条件见 [参数附录](parameters.md) 与 [原描述符](reference/template.json)。型材截面和材料由系列文件决定，不是通过这些窗型参数任意猜测厂家尺寸。

## 窗型、分格和定位

现有窗型为固定、推拉、平开、混合。固定格不额外制造窗扇框；推拉 2–4 扇、2–4 轨；平开 1–2 扇，单扇选左/右合页，记录内开/外开；滑动纱窗 1–2 扇但还须满足系列轨道与宽度规则。预览统一为关闭状态，行程范围及开向是元数据，没有开合运动仿真。

采用矩形 1–3 行 × 1–3 列网格，按实际中梃/横梃占位扣除后，以行列权重分配净尺寸。混合模式只读取有效格的 fixed/sliding/hinged 选择；隐藏、未使用格及活动扇参数保留草稿而不参与生成。`mergeTopLight` 只在多行且多列时有效：顶部固定玻璃跨列，竖中梃到横梃处终止，不穿过通长玻璃。

系列的 `roles` 将外框四边、中梃、横梃、推拉扇上下边/边企/勾企、平开扇四边/对接料、纱窗四边映射到实际型材。截面坐标减 `referenceOrigin`、旋转 `referenceAngle` 后放到装配基准：U 指向框内，V 指向窗深。`face` 是装配占位，不能随意用轮廓包围盒中心替代基准。

拼角由系列对 frame/sliding/hinged/screen 分别选 `miter/side_wrap/horizontal_wrap`；45° 拼角要求相邻面宽相同。连接必须经双方 `compatibleProfiles` 互相允许；扇料深度与轨距、框深、洞口尺寸必须可容纳。轨道分配来自 `trackLayouts`，同轨关闭态不许重叠；行程受框边和相邻扇约束。纱窗需独立轨道，不能与玻璃轨混用。

## 系列数据协议及生产阻断

`profile_system.py` 只读数据，接受 `icax.window-profile-system` v1、毫米单位。必需身份、角色、实际轮廓、尺寸、拼角及兼容规则；外部文件必须为绝对路径 `.json`，最多 2 MB。原文件 SHA-256 用于系列身份与资源摘要。

尺寸表达式最多 512 字符、100 个 AST 节点，仅允许数值、已有变量、正负号及四则运算；不允许调用、属性访问、幂或任意脚本。数值必须有限且绝对值不超过 1e8。窗型阶段提供 Width/Height/ApertureWidth/ApertureHeight/PanelCount/PanelIndex，计算后才提供 PanelWidth/PanelHeight，填充阶段提供 InnerWidth/InnerHeight。

`ready` 的源码判据为 `status=='verified'` 且 `manufacturer`、`source` 非空；这是文件声明，不是软件认证。`systems/demonstration.json` 的厂家为空、状态 demonstration、id 为 demonstration-not-for-production，轮廓是演示矩形空心料，不能仅改状态便宣称生产可用。

演示常数为 SideGap=4、HeightGap=4、Overlap=20、TrackSpacing=32、GlassGap=3、ScreenGap=6、MeetingGap=4 mm。外框/中梃/横梃 50 × 120 mm；slide-top 32 × 20、slide-bottom 38 × 20、jamb 35 × 20、interlock 42 × 20、hinged/meeting 35 × 30、screen 24 × 12 mm，壁厚均 1.5 mm。这些只是保存的演示数据，不是采购规格。完整精确轮廓、角色、尺寸公式、轨道组合和五金条目见 [demonstration.json](reference/systems/demonstration.json)。演示没有二次加工规则，五金名字均保留待系列选型语义。

## 显示、加工与拆单

公共 `display()` 复制宿主参数后采用 `external_templates` 成品表示，无论隐藏的加工来源草稿如何；显示生成独立外形、实际系列截面、玻璃和纱网示意，不构造端切或孔槽加工布尔。显示所用端部外形与制造原管不是同一加工完成体；显示不会证明下料已就绪。

公共 `manufacturing()` 在声明上下文输出 `icax.manufacturing-model` v4 的连接及工艺调用，由独立加工执行器执行。未核验系列先拒绝制造。`builtin_rules` 下按系列拼角与 `machiningRules` 声明加工：当前只支持 `roundThroughDepth`、`rectThroughDepth`，station 沿轴向、across 沿装配 U，尺寸可引用 Length/Face/Depth 及常数。未知加工类型报错。型材自带轨道及槽由原轮廓表达，不重复切除。

`external_templates` 下按实际截面求原管毛坯余量，每个框/扇角声明两个端部相遇的 L 节点，中梃/横梃端为侧面—端部 T 节点；玻璃、纱网和五金不作为管材节点。方头原料标记 `ready=false`，追加 v2 `profile-stock-preparation` 和待分配节点，忽略系列旧拼角、孔槽规则。核验系列也不能使未分配、未验证整窗适配的方头原料自动变成最终下料。

内部型材清单按截面、材料、长度、局部裁切面及加工特征归并，保留独立装配实例。另有玻璃/纱网采购清单和五金采购清单：采购裁切尺寸由系列规则算出，可与可见净尺寸不同；玻璃和纱网仅显示，制造目标仅含型材。五金来自 `hardwareRules` 的名称/数量，没有未知型号实体。SDK 公共输出不直接保留内部 `windowAssembly` 扩展表及 `parameters`。

## 未实现与资料定位

没有品牌实测系列；未实现上悬、内倒、平开内倒、提升推拉、卷轴纱窗、压线、端部支架、特殊连接件完整几何、任意递归拼窗、气密/水密/承载认证、机床 NC 或开合仿真。盲孔、专用锁具/滑轮加工也没有通用占位实现。`reference/README.md` 保存原调查依据与说明，其中旧菜单入口仅表示归档前位置。

`generate_system` 定义布局与系列规则，`member/ring` 定义截面、拼角及加工，`profile_system.py` 定义数据与表达式边界。既有参考测试为 `AluminiumWindowTests.py`、`ProductPureFunctionBoundaryTests.py`、`ProductDisplaySeparationTests.py`（均在 `src/tests/icax-plugins/product/TubeDesigner/TubeDesignerTest/`），以及 `src/tests/iCAX-UI/AluminiumWindowTest.mjs`；原生资料包括 `ProductTemplatePreviewSDO.AluminiumWindowPanelsAndMovableScreen` 与 `TemplateRuntimeTest.AluminiumWindowSeriesProductionAndNativeSolids`。后者使用临时合成核验系列，不是已部署厂家资料。本次未运行这些测试，历史内部接口断言不等同当前公共协议。
