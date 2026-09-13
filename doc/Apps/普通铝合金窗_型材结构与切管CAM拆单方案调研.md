# 普通铝合金窗行业款式、型材结构与切管 CAM 拆单方案调研

## 1. 调研目标

本文面向切管 CAM 产品设计，对普通铝合金窗进行结构化分析，重点研究：

1. 普通铝合金窗有哪些值得支持的基础款式；
2. 一樘窗由哪些型材部件组成；
3. 外框、中梃、横梃、活动窗扇、固定玻璃和纱窗之间是什么关系；
4. 推拉窗、平开窗、固定窗等结构应该如何统一抽象；
5. 可移动纱窗如何纳入同一套窗户拆单模型；
6. 如何根据型材系列规则，把整樘窗自动拆成一根根可加工的铝型材零件；
7. 哪些尺寸可以由几何推导，哪些必须由型材系统规则驱动；
8. 切管 CAM 第一阶段应该优先实现哪些能力。

本文不以枚举大量市场“款式”为目标，而是尝试用少量底层结构覆盖大部分普通住宅和工程铝合金窗。

---

# 2. 核心判断

普通铝合金窗和百叶窗虽然最终都可以拆成一根根型材，但两者的参数化思想并不相同。

百叶更加偏向：

> **Geometry Driven —— 几何驱动**

例如根据：

```text
W
H
叶片方向
叶片倾角
间距
```

即可通过几何关系推导大部分零件。

而铝合金窗更加偏向：

> **System Driven —— 型材系统驱动**

用户通常给出：

```text
总宽
总高
窗型
分格
窗扇数量
开启方式
是否带纱窗
```

但具体：

```text
外框用什么型材
窗扇用什么型材
中间勾企用什么型材
窗扇尺寸扣多少
玻璃尺寸扣多少
纱窗尺寸扣多少
端部切什么角
需要加工哪些孔槽
```

往往取决于某个具体厂家、具体系列的铝型材系统。

因此普通窗户模块不应只做“几何生成器”，而应该建立：

> **窗型生成器 + ProfileSystem 型材系统数据库**

---

# 3. 一樘普通铝窗的基本结构

从 CAM 和拆单角度，一樘完整窗户建议抽象为：

```text
WindowAssembly
│
├─ OuterFrame              外框
│
├─ Mullion[]               中梃 / 竖中梃
│
├─ Transom[]               横梃
│
├─ Aperture[]              一个个分格洞口
│
├─ FixedLight[]            固定玻璃区域
│
├─ Sash[]                  活动玻璃窗扇
│
├─ ScreenSash[]            活动纱窗
│
└─ Hardware                五金附件
```

其中真正需要进入切管拆单的核心部分是：

```text
OuterFrame
Mullion
Transom
Sash
ScreenSash
```

玻璃、纱网、胶条、滑轮、锁具等可以作为 BOM 或附件处理。

---

# 4. 为什么不能把一樘窗简单理解为“四根框料”

普通窗户表面看起来很简单，但实际上一个活动窗扇内部可能包含不同角色的型材。

例如普通推拉窗中，一个活动窗扇可能由：

```text
TopRail
BottomRail
JambStile
InterlockStile
```

组成。

也就是说：

- 上横料；
- 下横料；
- 靠外框的一侧竖料；
- 与另一扇窗相互咬合的一侧竖料；

不一定是同一种型材。

所以窗户拆单不能简单建模成：

```text
SashProfile × 4
```

而应该支持不同角色分别映射到不同 Profile。

---

# 5. 建议支持的基础窗型

从底层结构看，普通铝窗不需要做几十种独立模板。

第一阶段可以收敛为三大类：

```text
FixedPanel
SlidingPanelSystem
HingedPanelSystem
```

再通过：

```text
Mullion
Transom
Aperture
```

组合出复杂窗型。

---

# 6. 固定窗

固定窗是最简单的结构。

典型外观：

```text
┌────────────────┐
│                │
│    固定玻璃     │
│                │
└────────────────┘
```

其基本结构可以是：

```text
OuterFrame
+
Glass
+
GlazingBead / Seal
```

需要注意：

> 固定玻璃不一定总有一圈独立的“窗扇料”。

一些系统中，玻璃直接安装到外框或中梃形成的洞口中。

因此软件中建议使用：

```text
FixedLight
```

而不是强制使用：

```text
FixedSash
```

---

## 6.1 固定窗拆单

最简单情况下：

```text
FixedWindow
│
├─ FrameTop
├─ FrameBottom
├─ FrameLeft
└─ FrameRight
```

如果带中梃或横梃：

```text
FixedWindow
│
├─ OuterFrame × 4
├─ Mullion × N
└─ Transom × M
```

玻璃尺寸则由对应 Aperture 和型材系统规则计算。

### 建议优先级

**P0。**

---

# 7. 水平推拉窗

推拉窗应该作为普通铝窗模块的重点。

典型双扇：

```text
┌──────────────────────────┐
│        A      │      B    │
│       ←→      │           │
└──────────────────────────┘
```

但从拆单角度，其结构应理解为：

```text
SlidingWindow
│
├─ OuterFrame
│
├─ SlidingSash A
│
└─ SlidingSash B
```

每一扇 SlidingSash 进一步拆为：

```text
SlidingSash
│
├─ TopRail
├─ BottomRail
├─ JambStile
└─ InterlockStile
```

---

# 8. 推拉窗中的勾企、光企和互锁关系

推拉窗最重要的一个结构特点是：

> 两扇活动窗在关闭状态下，中间会发生互锁或搭接。

国内行业常见名称包括：

- 勾企；
- 光企；
- 互锁料；
- 边企；
- 中企；
- 扇竖料。

具体名称会因地区、厂家和型材系列不同而变化。

软件内部建议不要依赖市场俗称，而统一用角色表达：

```text
JambStile
InterlockStile
MeetingStile
```

其核心区别在于：

```text
JambStile
```

通常面向外框一侧。

```text
InterlockStile
```

用于两扇推拉扇中间相互搭接或咬合的位置。

因此：

> 同一扇推拉窗的左竖料和右竖料可能不是同一种型材。

---

# 9. 推拉窗不要按“两扇、三扇、四扇”分别开发

市场上存在：

```text
双扇推拉
三扇推拉
四扇推拉
一固定 + 一活动
活动 + 固定 + 活动
两玻璃扇 + 一纱窗
三轨推拉
四轨推拉
```

如果每种组合都做一个独立模板，很快会失控。

建议统一抽象为：

```text
SlidingPanelSystem
│
├─ TrackSystem
│
└─ Panel[]
```

---

# 10. TrackSystem 轨道系统

推拉窗与平开窗最大的区别之一在于：

> 多个窗扇可以在正视图上相互重叠，但处于不同轨道。

因此不能只通过二维分格表达。

建议建立：

```text
TrackSystem
{
    TrackCount
    TrackSpacing
}
```

每一扇 Panel 记录：

```text
TrackIndex
ClosedPosition
TravelRange
```

---

## 10.1 两轨双扇推拉

```text
Track 0:
    GlassSash A

Track 1:
    GlassSash B
```

---

## 10.2 三轨双玻璃 + 一纱窗

```text
Track 0:
    GlassSash A

Track 1:
    GlassSash B

Track 2:
    ScreenSash
```

---

## 10.3 多扇推拉

继续增加：

```text
Panel[]
```

即可。

因此：

> 两扇、三扇、四扇并不是不同的底层产品结构，只是 Panel 数量和轨道分配不同。

---

# 11. 可移动纱窗

用户特别关注的“可移动纱窗”建议直接纳入窗扇系统，而不是当成一个简单附件。

典型滑动纱窗结构：

```text
ScreenSash
│
├─ TopRail
├─ BottomRail
├─ LeftStile
├─ RightStile
│
├─ Mesh
├─ Roller
└─ Handle
```

从拆管角度看，ScreenSash 和普通 GlassSash 非常接近：

```text
GlassSash
    填充 = Glass

ScreenSash
    填充 = Mesh
```

因此建议把纱窗定义为一种独立 Panel：

```text
PanelType = ScreenSash
```

---

# 12. 纱窗为什么不能只作为附件处理

因为滑动纱窗本身就需要：

- 一圈专用纱窗型材；
- 四根或多根铝型材；
- 拼角；
- 滑轮；
- 拉手；
- 卡槽；
- 纱网槽；
- 可能存在独立轨道。

部分型材系统中还会规定：

```text
纱窗宽度 = 某洞口宽度 - 固定扣减值
```

例如可能存在类似：

```text
W - 40.2
```

这样的系列专用工艺尺寸。

因此：

> 纱窗也必须通过 ProfileSystem 和尺寸规则完成独立拆单。

---

# 13. 第一阶段纱窗只建议支持滑动纱窗

纱窗市场还有：

```text
固定纱窗
平开纱窗
推拉纱窗
内置纱窗
卷轴隐形纱窗
折叠纱窗
```

对于切管 CAM 第一阶段，没有必要一次覆盖。

建议先做：

```text
SlidingScreenSash
```

原因：

- 和推拉铝窗天然配套；
- 结构简单；
- 型材明确；
- 可以完整拆成四根型材；
- 与现有 TrackSystem 可以直接复用。

隐形卷轴纱窗属于另外一套产品系统，可以后续单独做。

---

# 14. 平开窗

平开窗是另一大核心母型。

典型单扇：

```text
┌────────────────┐
│              / │
│            /   │
│          /     │
└────────────────┘
```

基本结构：

```text
CasementWindow
│
├─ OuterFrame
└─ OperableSash
```

OperableSash：

```text
OperableSash
│
├─ TopSashProfile
├─ BottomSashProfile
├─ LeftSashProfile
└─ RightSashProfile
```

附加：

```text
Glass
Hinge
Handle
Lock
Seal
GlazingBead
```

---

# 15. 单扇和双扇平开不需要两套生成器

可通过参数控制：

```text
SashCount
OpeningDirection
PrimarySash
SecondarySash
```

例如：

```text
SingleLeft
SingleRight
DoubleCasement
```

都属于：

```text
HingedPanelSystem
```

的一种配置。

---

# 16. 上悬、下悬、内倒和平开内倒

市场常见：

```text
Casement
Awning
Hopper
Tilt
TiltAndTurn
```

从型材拆单角度看，本质通常仍然是：

```text
OuterFrame
+
OperableSash
```

变化主要集中在：

- 合页位置；
- 开启轴；
- 五金系统；
- 执手；
- 锁点；
- 五金孔槽。

因此建议：

```text
OperableSash
{
    OperationType
}
```

而不是分别开发：

```text
CasementGenerator
AwningGenerator
TiltGenerator
```

### 第一阶段建议

优先支持：

```text
Casement
```

其余作为 P1。

---

# 17. 中梃和横梃

复杂窗户真正应该抽象的是：

> **分格系统。**

例如：

```text
┌─────────┬─────────┐
│ 固定玻璃 │ 活动窗扇 │
│         │         │
└─────────┴─────────┘
```

或者：

```text
┌───────────────────┐
│      固定亮        │
├─────────┬─────────┤
│ 活动扇A │ 活动扇B │
└─────────┴─────────┘
```

这不应该视为两个全新的窗户款式。

应统一抽象成：

```text
OuterFrame
+
Mullion
+
Transom
+
Aperture[]
```

---

# 18. Mullion 中梃

竖向分隔型材：

```text
Mullion
```

用于把窗体分成左右多个区域。

例如：

```text
┌──────┬──────┐
│      │      │
│      │      │
│      │      │
└──────┴──────┘
```

中间竖料就是 Mullion。

---

# 19. Transom 横梃

横向分隔型材：

```text
Transom
```

例如：

```text
┌─────────────┐
│             │
├─────────────┤
│             │
└─────────────┘
```

中间横料即为 Transom。

---

# 20. Aperture 洞口

中梃和横梃把外框划分成若干：

```text
Aperture
```

每个 Aperture 可以被配置成：

```text
FixedLight
SlidingPanelSystem
HingedSash
```

但对于推拉窗，需要注意：

> 一个 SlidingPanelSystem 本身可能包含多个轨道和多个重叠 Panel。

因此推拉结构应作为一个 Aperture 内部的独立 TrackSystem。

---

# 21. 窗框拼接方式

窗户外框和窗扇型材常见两类拼接方式：

---

## 21.1 45°拼角

```text
╲────────────╱
│            │
│            │
╱────────────╲
```

即型材端部切：

```text
45°
```

再使用：

- 角码；
- 压角；
- 螺钉；
- 胶；

等方式装配。

---

## 21.2 90°直拼

```text
┌────────────
│
│
└────────────
```

某些推拉窗、轨道窗系统中会大量使用 90°直切。

---

# 22. 拼接方式不能写死

同样是铝合金窗，不同系列可能规定：

```text
FrameJoint = 45°
```

也可能规定：

```text
FrameJoint = 90°
```

甚至外框和窗扇采用不同的拼接方式。

因此不能简单写：

```cpp
FrameCutAngle = 45;
```

建议把规则放在：

```text
ProfileSystem.JointRule
```

中。

---

# 23. ProfileSystem 型材系统

这是普通铝合金窗模块最重要的数据对象之一。

建议定义：

```text
ProfileSystem
{
    Id
    Manufacturer
    Series
    WindowCategory
}
```

例如：

```text
XX Brand
Series 80 Sliding
```

---

# 24. ProfileSystem 中应该管理的型材

至少包括：

```text
FrameProfiles
SashProfiles
MullionProfiles
TransomProfiles

InterlockProfiles
TrackProfiles

ScreenProfiles

GlazingBeadProfiles

AccessoryProfiles
```

例如：

```text
ProfileSystem: Series-80 Sliding

OuterFrame:
    HeadProfile
    SillProfile
    LeftJambProfile
    RightJambProfile

SlidingSash:
    TopRailProfile
    BottomRailProfile
    JambStileProfile
    InterlockStileProfile

ScreenSash:
    ScreenTopProfile
    ScreenBottomProfile
    ScreenLeftProfile
    ScreenRightProfile
```

---

# 25. Profile 不能只保存二维截面

单个型材建议至少保存：

```text
Profile
{
    ProfileId

    SectionGeometry

    ReferenceOrigin
    ReferenceAxis

    Role

    DefaultCutRule

    JointRules[]

    MachiningRules[]

    CompatibleProfiles[]
}
```

---

# 26. ReferenceOrigin 和 ReferenceAxis

这两个数据非常重要。

因为同一个型材截面里可能存在：

- 外包络；
- 玻璃槽；
- 胶条槽；
- 轨道；
- 扣接面；
- 拼接参考面。

软件计算尺寸时不能简单以型材外包络中心作为基准。

因此每个型材都应有明确：

```text
ReferenceOrigin
ReferenceAxis
ReferencePlane
```

用来参与装配和尺寸计算。

---

# 27. CompatibleProfiles

建议建立型材兼容关系。

例如：

```text
OuterFrameA
    compatible with:
        SashA
        FixedGlazingA

SashA
    compatible with:
        InterlockB
        GlazingBeadC

ScreenSashA
    compatible with:
        ScreenTrack3
```

这样一套窗户就成为：

> 一套具有装配规则的型材系统。

---

# 28. 尺寸计算不能完全依靠统一几何公式

这一点是普通铝窗与百叶最大的区别之一。

在百叶中，经常可以使用：

\[
L = W - 2B
\]

这样的通用关系。

但在系统窗中，厂家工艺手册可能直接规定：

```text
H - 117
H - 130
W - 40.2
H - 249.2
```

这种尺寸。

这些扣减来自：

- 型材自身截面；
- 搭接；
- 插入关系；
- 胶条；
- 角码；
- 滑轮；
- 端盖；
- 锁具；
- 轨道；
- 玻璃安装深度。

因此软件必须支持：

```text
DimensionRule
```

---

# 29. DimensionRule 尺寸规则

建议例如：

```text
Rule:
    Role = SlidingSash.TopRail

    Length =
        ApertureWidth / 2
        - 23.5
```

或者：

```text
ScreenSash.Width =
    ApertureWidth
    - 40.2
```

更理想的方式是支持参数表达式：

```text
LengthExpr
```

例如：

```text
ApertureWidth
PanelWidth
FrameInsideWidth
ProfileOverlap
```

等语义变量。

---

# 30. 外框拆单

普通矩形外框通常拆成：

```text
FrameTop
FrameBottom
FrameLeft
FrameRight
```

每根零件保存：

```text
ProfileId
Length
StartCut
EndCut
MachiningFeatures
```

例如 45°拼框：

```text
FrameTop:
    StartCut = +45°
    EndCut   = -45°
```

90°直拼：

```text
StartCut = 90°
EndCut   = 90°
```

但具体长度是否等于：

```text
W
H
```

仍取决于该系统的尺寸基准定义和 JointRule。

---

# 31. 推拉窗拆单示例

以两扇推拉玻璃窗为例：

```text
Window
│
├─ OuterFrame
│   ├─ FrameTop
│   ├─ FrameBottom
│   ├─ FrameLeft
│   └─ FrameRight
│
├─ SlidingSash A
│   ├─ SashTopA
│   ├─ SashBottomA
│   ├─ JambStileA
│   └─ InterlockStileA
│
└─ SlidingSash B
    ├─ SashTopB
    ├─ SashBottomB
    ├─ JambStileB
    └─ InterlockStileB
```

最基础就有：

```text
4 + 4 + 4 = 12 根型材零件
```

---

# 32. 推拉窗 + 活动纱窗拆单示例

增加一扇滑动纱窗：

```text
Window
│
├─ OuterFrame × 4
│
├─ SlidingSash A × 4
│
├─ SlidingSash B × 4
│
└─ ScreenSash × 4
```

即：

```text
4 + 8 + 4 = 16 根型材
```

这还没有包含：

- 压线；
- 附加轨道；
- 加强料；
- 中梃；
- 装饰盖板。

---

# 33. ScreenSash 纱窗拆单

建议：

```text
ScreenSash
│
├─ ScreenTop
├─ ScreenBottom
├─ ScreenLeft
└─ ScreenRight
```

并记录：

```text
ScreenProfileId
ScreenMeshType
RollerType
HandleType
TrackIndex
```

如果四边使用同一种型材，可自动归并。

---

# 34. 活动窗扇拆单

平开窗：

```text
OperableSash
│
├─ SashTop
├─ SashBottom
├─ SashLeft
└─ SashRight
```

根据系列不同，也可能：

```text
SashLeftProfile != SashRightProfile
```

因此数据结构应允许四边独立指定 ProfileRole。

---

# 35. 加工特征

如果以后软件不仅负责定长下料，还需要支持型材加工，则应该从架构开始就保留：

```text
MachiningFeatures[]
```

---

# 36. 外框常见加工

可能包括：

```text
DrainageHole
MountingHole
CornerCleatHole
ScrewHole
ConnectorHole
```

---

# 37. 推拉窗扇常见加工

可能包括：

```text
RollerHole
RollerSlot

LockHole
LockSlot

HandleHole
HandleSlot

DrainHole
```

---

# 38. 平开窗扇常见加工

可能包括：

```text
HandleHole
LockSlot
HingeHole
HardwareSlot
CornerConnectorHole
```

---

# 39. 纱窗常见加工

可能包括：

```text
RollerFeature
HandleFeature
CornerJointFeature
ScreenMeshGroove
```

其中部分槽是型材自身已有截面特征，不需要后加工；部分需要打孔或铣削。

因此必须区分：

```text
ProfileIntrinsicFeature
```

和：

```text
MachiningFeature
```

---

# 40. 固定玻璃和玻璃尺寸

玻璃本身不是管件，但拆单软件通常仍然需要输出。

建议：

```text
GlassPart
{
    Width
    Height
    Thickness
    Type
    Quantity
}
```

尺寸不要简单使用 Aperture 外尺寸。

而应通过：

```text
GlassDimensionRule
```

计算：

```text
GlassWidth
GlassHeight
```

因为玻璃要考虑：

- 插入深度；
- 压线；
- 胶条；
- 安装间隙。

---

# 41. 纱网尺寸

同理：

```text
ScreenMeshPart
{
    Width
    Height
    MeshType
}
```

其尺寸可以由：

```text
ScreenSashInnerWidth
ScreenSashInnerHeight
```

结合预留量计算。

---

# 42. 建议的数据结构

整体模型：

```text
WindowAssembly
│
├─ ProfileSystem
│
├─ Size
│   ├─ Width
│   └─ Height
│
├─ OuterFrame
│
├─ Divider[]
│   ├─ Mullion
│   └─ Transom
│
├─ Aperture[]
│
└─ Panel[]
    ├─ FixedLight
    ├─ SlidingSash
    ├─ HingedSash
    └─ ScreenSash
```

---

# 43. Panel 数据建议

```text
Panel
{
    PanelId

    Type

    ApertureId

    Width
    Height

    TrackIndex

    OpeningDirection

    OperationType
}
```

其中：

```text
Type =
    FixedLight
    SlidingSash
    HingedSash
    ScreenSash
```

---

# 44. 推拉 Panel 专属数据

```text
SlidingPanel
{
    TrackIndex
    ClosedPosition
    OpenDirection
    TravelRange
    Overlap
}
```

这些信息主要用于：

- 生成装配模型；
- 判断互锁关系；
- 计算窗扇宽度；
- 生成正确竖料角色。

---

# 45. 窗扇角色

建议不要简单写：

```text
Left
Right
Top
Bottom
```

而应额外存在：

```text
ProfileRole
```

例如：

```text
TopRail
BottomRail
JambStile
InterlockStile
MeetingStile
```

这样才能正确匹配不同系列的专用型材。

---

# 46. ProfilePart 最终拆单结果

最终建议输出：

```text
ProfilePart[]
```

单个零件：

```text
ProfilePart
{
    PartId

    AssemblyId
    PanelId

    Role

    ProfileId
    Material

    Length

    StartCut
    EndCut

    MachiningFeatures[]

    Quantity

    AssemblyTransform
}
```

---

# 47. 相同零件归并

如果两根型材满足：

- ProfileId 相同；
- 长度相同；
- 两端切角相同；
- 孔槽加工相同；
- 材质相同；

则可以自动归并：

```text
Part A
Qty = 2
```

用于：

- BOM；
- 型材下料；
- 优化排料；
- 生产任务；
- 加工程序生成。

---

# 48. ProfileSystem 规则建议

建议一个具体型材系列至少包含以下几类规则：

```text
ProfileMappingRule
DimensionRule
JointRule
MachiningRule
GlassRule
ScreenRule
CompatibilityRule
```

---

# 49. ProfileMappingRule

决定某一个结构角色使用哪种型材。

例如：

```text
Role:
    OuterFrame.Head

Profile:
    P-001
```

```text
Role:
    SlidingSash.InterlockStile

Profile:
    P-108
```

---

# 50. JointRule

例如：

```text
FrameCorner:
    45Degree
```

或者：

```text
SlidingSashCorner:
    90Degree
```

还可以附带：

```text
CornerConnector
Screw
Cleat
```

等附件信息。

---

# 51. MachiningRule

例如：

```text
Role = SlidingSash.BottomRail
```

自动生成：

```text
RollerSlot
```

位置由：

```text
DistanceFromLeft
DistanceFromRight
```

或者公式计算。

---

# 52. 窗户生成流程

推荐整体流程：

```text
用户输入窗户参数
      ↓
选择 ProfileSystem
      ↓
生成矩形外框
      ↓
根据分格生成 Mullion / Transom
      ↓
形成 Aperture[]
      ↓
为每个 Aperture 配置 Panel
      ↓
生成 FixedLight / Sash / ScreenSash
      ↓
根据 ProfileMappingRule 选择型材
      ↓
根据 DimensionRule 计算长度
      ↓
根据 JointRule 生成端部切割
      ↓
根据 MachiningRule 生成加工特征
      ↓
生成 ProfilePart[]
      ↓
相同零件归并
      ↓
进入型材排料和加工程序
```

---

# 53. UI 参数建议

第一阶段用户界面可以非常克制。

---

## 53.1 基础尺寸

```text
总宽 W
总高 H
```

---

## 53.2 窗型

```text
固定窗
推拉窗
平开窗
组合窗
```

---

## 53.3 推拉窗参数

```text
轨道数
玻璃窗扇数量
固定玻璃数量
是否带纱窗
纱窗数量
```

---

## 53.4 平开窗参数

```text
窗扇数量
开启方向
左开 / 右开
内开 / 外开
```

---

## 53.5 分格

可以提供：

```text
增加竖中梃
增加横梃
```

每条记录：

```text
Position
ProfileRole
```

---

## 53.6 型材系列

用户选择：

```text
Manufacturer
Series
```

之后：

- Profile；
- 扣减；
- 切角；
- 孔槽；

尽量自动确定。

---

# 54. 第一阶段开发优先级

## P0

建议优先实现：

```text
固定窗
```

```text
两轨双扇推拉窗
```

```text
双扇推拉 + 一扇滑动纱窗
```

```text
单扇平开窗
```

```text
双扇平开窗
```

```text
Mullion
```

```text
Transom
```

```text
固定玻璃 + 活动扇组合
```

```text
45° / 90°拼接规则
```

```text
ProfileSystem
```

```text
DimensionRule
```

```text
ProfilePart 自动拆单
```

---

# 55. 第二阶段 P1

建议加入：

```text
三扇推拉
四扇推拉
多轨推拉
```

```text
上悬窗
下悬窗
内倒窗
平开内倒
```

```text
加工孔槽
```

```text
锁具加工
```

```text
滑轮加工
```

```text
排水孔
```

```text
压线自动拆单
```

---

# 56. 第三阶段 P2

后续可再支持：

```text
提升推拉
```

```text
折叠窗
```

```text
隐藏扇
```

```text
卷轴隐形纱窗
```

```text
特殊幕墙窗
```

这些已经属于更复杂的门窗系统。

---

# 57. 不建议建立的大量独立款式

以下不建议分别建立模板：

```text
两扇推拉窗
三扇推拉窗
四扇推拉窗
带纱窗两扇推拉
三轨带纱窗推拉
```

更合理的是：

```text
SlidingPanelSystem
+
TrackSystem
+
Panel[]
```

同样：

```text
左开平开窗
右开平开窗
双扇平开窗
```

都应属于：

```text
HingedPanelSystem
```

通过参数表达。

---

# 58. 和百叶模块可以共用哪些底层能力

普通窗户与百叶可以共享：

```text
Profile
SectionGeometry
Material
CutPlane
MachiningFeature
ProfilePart
AssemblyTransform
BOM
Nesting
```

但不要强行共享同一套上层生成逻辑。

百叶：

```text
Geometry Driven
```

窗户：

```text
Profile System Driven
```

二者只在最终：

```text
ProfilePart[]
```

这一层汇合。

---

# 59. 推荐最终架构

建议窗户模块最终建立：

```text
WindowAssembly
│
├─ ProfileSystem
│
├─ WindowLayout
│
├─ OuterFrame
│
├─ Divider[]
│
├─ Aperture[]
│
├─ Panel[]
│   ├─ FixedLight
│   ├─ SlidingSash
│   ├─ HingedSash
│   └─ ScreenSash
│
└─ Hardware[]
```

其中最核心的是：

```text
ProfileSystem
WindowLayout
Panel
DimensionRule
ProfilePart
```

---

# 60. 最终结论

对于普通铝合金窗，不需要为了“款式数量”建立大量独立模板。

真正值得实现的是：

> **矩形窗框 + 分格系统 + 窗扇系统 + 轨道系统 + 纱窗系统 + 型材系统规则 + 自动拆单**

从产品抽象上，可以收敛为三大基础结构：

```text
FixedPanel
SlidingPanelSystem
HingedPanelSystem
```

然后通过：

```text
Mullion
Transom
Aperture
TrackSystem
ScreenSash
```

进行组合。

其中最关键的设计判断是：

> **普通铝合金窗不是单纯靠几何尺寸驱动，而是由具体型材系列及其工艺规则驱动。**

因此，软件必须建立：

```text
ProfileSystem
```

用于管理：

```text
型材角色
截面
装配基准
尺寸扣减
拼接规则
孔槽加工
兼容关系
玻璃规则
纱窗规则
```

最终把整樘窗转换为：

```text
ProfilePart[]
```

每一根零件明确：

```text
使用什么型材
长度是多少
两端怎么切
需要加工哪些孔槽
属于哪一扇窗
数量是多少
```

这才是普通铝合金窗真正适合切管 CAM 的产品化方向。
