# 普通铝合金窗：窗型与型材系统分离

入口：资源库 → 产品 → 窗 → 普通铝合金窗。

本模板按《普通铝合金窗_型材结构与切管CAM拆单方案调研.md》建立窗型、分格、窗扇、轨道和型材系统规则。**内置截面只是结构演示，不是厂家窗料，不允许生产拆单。**

## 当前窗型

- 固定窗：不强制增加一圈窗扇，直接形成固定玻璃洞口。
- 推拉窗：2至4扇、2至4轨，系列文件明确分配每扇所在轨道。
- 滑动纱窗：独立四根纱窗料、独立轨道和纱网采购尺寸；1至2扇，需系列宽度规则允许同轨布置。
- 平开窗：单扇左/右合页、双扇对开，内开/外开信息。当前预览为关闭状态，不包含开合运动仿真。
- 分格：1至3行、1至3列，独立净尺寸比例，混合模式逐格选择固定/推拉/平开。
- 通长上固定亮：顶部固定洞口跨列，下部可配置活动扇；竖中梃在横梃处终止，而不是穿过上固定玻璃。
- 拼角：按系列为外框、推拉扇、平开扇、纱窗分别指定45°拼角、左右包上下、上下包左右。不同面宽不能直接冒称45°拼角。

## 接入实际厂家系列

选择“厂家系列 JSON 文件”，填写本机文件绝对路径。文件不包含可执行脚本；尺寸表达式仅支持数值、变量及加减乘除，禁止函数调用和任意代码执行。

参考 systems/demonstration.json 的**数据结构**制作真实系列，不能沿用其中示例截面或扣减值作为生产依据。

- schema：icax.window-profile-system；schemaVersion：1；units：mm。
- id、manufacturer、series、revision、source：系列身份、厂家和资料来源。
- status：未核验使用非 verified 状态；有来源且完成系列核对后才声明 verified。这个声明不是软件自动认证，也不是承载/气密/水密性能认证。
- profiles：实际材料轮廓 contours（含内轮廓）、材质、face 装配占位、depth、referenceOrigin、referenceAngle、compatibleProfiles。型材标识在下游附带系列前缀及内容摘要，避免不同系列同名料混淆。
- roles：frame.top/bottom/left/right、mullion、transom、sliding.top/bottom/jamb/interlock、hinged.top/bottom/left/right/meeting、screen.top/bottom/left/right。角色可映射到不同实际型材。
- joints：frame、sliding、hinged、screen，各自选择 miter/side_wrap/horizontal_wrap。
- compatibility：panels 支持的 fixed/sliding/hinged/screen，maxTracks；两根型材的连接还需各自 compatibleProfiles 互相允许。
- trackLayouts：键为“轨数/玻璃扇数/glass或screen”，glass 数组指定各玻璃扇轨道；screen 指定纱窗轨道，必须独立于玻璃扇。不是在生成器中按厂家写分支。
- dimensions、constants：窗扇外尺寸和定位、搭接、玻璃/纱网裁切尺寸、玻璃适用厚度、轨距等规则。

尺寸语义变量：Width、Height、ApertureWidth、ApertureHeight、PanelCount、PanelIndex（从0开始）、PanelWidth、PanelHeight、InnerWidth、InnerHeight。具体可用时机见演示文件：计算宽高后才有 PanelWidth/PanelHeight，填充计算阶段才有 InnerWidth/InnerHeight。

型材截面的原始坐标减去 referenceOrigin，再按 referenceAngle 转到装配基准；不可默认所有截面以外包络中心定位。基准U指向框内，V指向窗深。face 是从外侧装配基准至内侧洞口基准的占位，不是随意从任意复杂轮廓包络猜出的数值。需要特殊接头、端部榫口或非矩形装配基准的系列，不应仅更改 status 就投入生产。

## 加工与输出

输出保留每根型材的角色、所属窗扇、实际截面、材质、毛坯长度、局部裁切面、装配变换、加工特征；按型材、材质、长度、裁切面和加工特征归并，不丢失装配实例。

machiningRules 按角色声明二次加工，目前支持 roundThroughDepth、rectThroughDepth：station 沿料长，across 沿装配U，diameter 或 width/height 定义孔槽大小；表达式可引用 Length、Face、Depth 及系列常数。真实减运算贯穿深度方向。未知加工类型明确报错，不伪装完成。盲孔、特定锁具/滑轮/连接件加工仍需其完整规则和对应几何操作。

型材本身的轨道、胶条槽、纱网槽应在 contours 中表达，不应当作后加工重复切除。五金采购条目来自 hardwareRules，当前不自动生成未知型号五金实体。

显示结果包含玻璃、纱网示意；玻璃/纱网采购裁切尺寸由系列规则计算，允许和可见尺寸不同。制造结果只含型材管件。三份清单分别是型材、玻璃与纱网、五金。

未核验系列只允许 display，manufacturing 和未指定用途的兼容调用均拒绝。返回参数保持宿主输入，系统文件独立摘要记录在 windowAssembly 中。

## 本次不包含

没有实际厂家系列资料，因此未提供可直接生产的品牌系列。也未把上悬、内倒、平开内倒、提升推拉、卷轴纱窗、压线、端部支架等用占位字段冒充完整实现。当前分格是矩形网格及通长上亮，不是任意递归拼窗编辑器。

## 回归

- AluminiumWindowTests.py：基本根数、72组窗型/分格/用途、系列规则、角色、轨道行程、上亮、加工孔、表达式安全、参数不变、生产阻断。
- AluminiumWindowTest.mjs、ParameterConditionsTest.mjs：字段联动、非活动分格和隐藏草稿。
- ProductTemplatePreviewSDO.AluminiumWindowPanelsAndMovableScreen：实际原生预览。
- TemplateRuntimeTest.AluminiumWindowSeriesProductionAndNativeSolids：临时合成测试系列、生产路径、实体有效性、体积及零件两两干涉。测试系列不部署为真实品牌数据。
