# 钢楼梯款式、参数体系与切管 CAM 拆单方案调研

## 1. 调研目标

本文面向钢楼梯的参数化设计与切管 CAM 产品规划，重点研究：

1. 市面上的钢楼梯主要有哪些款式；
2. 哪些“款式”只是楼梯路线不同，哪些属于真正不同的钢结构形式；
3. 哪些钢楼梯适合使用矩形管、方管、圆管、槽钢等长型材加工；
4. 一套楼梯应如何拆成梯梁、踏步支撑、平台、立柱、扶手等一个个零件；
5. 哪些结构值得作为切管 CAM 的标准参数化模板；
6. 哪些结构实际上更适合板材激光、折弯或滚弯，不应强行归入切管模板。

核心目标不是堆积大量“楼梯款式”，而是用尽量少的参数化母型覆盖尽量多的真实钢楼梯产品。

---

## 2. 一个重要结论：钢楼梯必须分两个维度分类

市场资料经常把以下名称全部混在一起：

- 直楼梯；
- L 型楼梯；
- U 型楼梯；
- Z 型楼梯；
- 单梁楼梯；
- 双梁楼梯；
- 锯齿楼梯；
- 悬浮楼梯；
- 螺旋楼梯。

但从 CAM 角度，这其实混合了两种完全不同的概念。

### 2.1 第一维：楼梯路线 / 平面布局

决定人从下层走到上层的路线：

```text
Straight          直跑
L-Shape           L 型 / 90°转角
U-Shape           U 型 / 180°折返
Multi-Flight      多跑 / Z 型
Winder            扇形踏步转角
Spiral            螺旋
Helical / Curved  弧形 / 旋转
Steep Access      陡梯 / 船梯 / 交错踏步梯
```

### 2.2 第二维：梯梁 / 承重结构

决定钢结构到底怎么做：

```text
MonoStringer          单中梁
TwinStringer          双梁
SideStringer          两侧边梁
BoxStringer           箱型梁
TubeStringer          方管 / 矩形管梁
ChannelStringer       槽钢梁
PlateStringer         钢板梁
ZigZagStringer        锯齿梁
Cantilever            悬挑
FoldedPlate           折板式
```

因此：

> “L 型单中梁楼梯”和“L 型双侧梁楼梯”应该由同一个路线模型，加不同的承重结构策略生成，而不应该做成两个毫无关系的产品模板。

---

# 3. 楼梯路线类款式

## 3.1 直跑楼梯 Straight Stair

最基础、最常见。

```text
上层
             ┌──
          ┌──┘
       ┌──┘
    ┌──┘
 ┌──┘
└──────────── 下层
```

特点：

- 没有转向；
- 没有中间平台时结构最简单；
- 梯梁通常是一根或两根连续斜梁；
- 所有踏步参数一致；
- 非常适合参数化；
- 非常适合切管 CAM。

典型结构：

```text
直跑 + 单中梁
直跑 + 双中梁
直跑 + 两侧梁
直跑 + 锯齿梁
直跑 + 槽钢工业梁
```

**建议优先级：P0。**

---

## 3.2 带中间平台的直跑楼梯

当楼层高度较大时，可以在同一方向设置中间平台。

本质仍然是：

```text
Flight 1
+
Landing
+
Flight 2
```

两个 Flight 的方向相同，因此不应建立独立生成器。

---

## 3.3 L 型楼梯 / Quarter-Turn Stair

由两个直跑楼梯组成，中间转向约 90°。

平面示意：

```text
↑
↑
↑
└────→→→
```

### 平台转角

```text
Flight 1
+
Landing
+
Flight 2
```

这是钢结构中最容易制造、最适合模块化的一种。

**切管适配度：很高。**

### 扇形踏步转角 Winder

不用矩形平台，而用数个扇形或梯形踏步完成转向。

会引入：

- 不同形状踏步；
- 复杂支撑；
- 转角处特殊梯梁；
- 三角形 / 梯形板件。

更加偏向板材和焊接结构。

**建议优先级：P2。**

第一阶段建议优先支持平台式 L 型，而不是 Winder。

---

## 3.4 U 型楼梯 / Switchback Stair

两跑楼梯方向相反，中间通过平台折返 180°。

```text
↑ ↑
↑ ↑
↑ ↑
└─┘
```

本质仍然是：

```text
Flight 1
+
Landing
+
Flight 2
```

只是：

```text
Direction2 = Direction1 + 180°
```

**建议优先级：P0。**

---

## 3.5 开井式 U 型楼梯 Open-Well

两跑之间留有较大的中间井口。

与普通紧凑 U 型的区别主要是：

```text
GapBetweenFlights
```

以及平台尺寸变化。

因此不需要单独建立生成器。

---

## 3.6 Z 型 / 多跑楼梯

市场经常把三跑、连续折转的楼梯称为：

```text
Z Shape
Three-Flight
Multi-Flight
```

从 CAM 架构看：

> **Z 型本质仍然只是多个 Flight 与 Landing 的组合。**

不建议建立 `ZStairGenerator`。

统一使用：

```text
Flight[]
Landing[]
```

即可。

---

# 4. 梯梁 / 承重结构款式

## 4.1 单中梁楼梯 Mono / Center Stringer

这是现代钢楼梯中非常典型的结构。

```text
      踏步
 ────────────
       │
       │
    中央梯梁
       │
```

常见中央梯梁材料：

```text
矩形钢管
方钢管
箱型焊接梁
H 型结构
钢板焊接箱梁
```

### 单中梁 + 矩形管

这是切管行业非常值得关注的结构。

每一级踏步通过：

```text
TreadBracket
```

焊接或螺接到中央梁上。

最终拆单：

```text
Flight
│
├─ CenterStringer        ×1
├─ TreadBracket          ×N
├─ TreadSupportFrame     ×N（可选）
└─ ConnectionPlate       若干
```

其中 `CenterStringer` 本身是典型长管件。

**切管适配度：★★★★★**

**建议优先级：P0。**

---

## 4.2 双中梁 / Twin Stringer

在踏步下方布置两根平行梯梁：

```text
        Tread
 ─────────────────
     │        │
     │        │
 Stringer  Stringer
```

相对于单中梁：

- 受力更分散；
- 踏步支撑更直接；
- 可以使用相对较小截面的两根梁；
- 适合较宽楼梯；
- 制造和安装相对直观。

常见材料：

```text
矩形管
方管
箱型梁
槽钢
```

**切管适配度：★★★★★**

**建议优先级：P0。**

---

## 4.3 两侧边梁楼梯 Side Stringer

梯梁位于踏步左右两侧。

```text
梁 │────────────│ 梁
梁 │   踏步     │ 梁
梁 │────────────│ 梁
```

常见梯梁形式：

```text
钢板
槽钢
矩形管
箱梁
```

工业楼梯中大量使用：

```text
C-Channel Stringer
```

建筑装饰楼梯也常使用：

```text
Tube Stringer
Box Stringer
Plate Stringer
```

如果使用矩形管 / 方管，切管适配度非常高；使用整块钢板时则主要属于平面激光。

**建议优先级：P0。**

---

## 4.4 锯齿梁楼梯 Zig-Zag Stringer

锯齿梁沿每一级踏步和踢面形成连续折线：

```text
      ┌───
   ┌──┘
┌──┘
```

典型现代楼梯经常采用两侧锯齿形钢梁。

常见制造方式：

- 厚钢板激光切割；
- 钢板拼焊；
- 折板；
- 多段型材焊接。

因此：

> 锯齿梁虽然是重要款式，但并不天然属于纯切管产品。

如果使用：

```text
矩形管分段 + 斜接焊
```

也可以进入切管系统，但会产生大量节点。

**建议优先级：P1。**

---

## 4.5 箱型梁 / Box Stringer

箱型梁可以是：

```text
成品矩形管
```

也可以是：

```text
钢板焊接箱梁
```

其优势：

- 扭转刚度较高；
- 外观整洁；
- 易隐藏焊缝和连接件；
- 很适合现代建筑楼梯。

如果使用标准矩形管，则与切管 CAM 高度匹配。

---

## 4.6 悬挑 / Floating / Cantilever Stair

“Floating Stair”并不是唯一结构。

市场所谓“悬浮楼梯”可能通过：

```text
单中梁 + 开放踏步
双梁 + 隐藏支架
锯齿梁
墙体内隐藏钢梁
真正单侧悬挑踏步
```

实现。

因此：

> **Floating 更像外观效果，不应该作为底层结构类型。**

真正悬挑式每一级踏步单独从墙体或隐藏钢结构中悬挑出来，对建筑主体连接要求高，结构工程属性强。

**建议优先级：P2。**

---

# 5. 工业钢楼梯

工业钢楼梯通常强调：

- 制造简单；
- 结构稳固；
- 防滑；
- 户外耐候；
- 模块化；
- 螺栓连接；
- 平台与护栏一体化。

典型结构：

```text
两侧槽钢梯梁
+
钢格栅踏步
+
平台框架
+
圆管 / 方管扶手
```

也有：

```text
Plate Stringer
Box Stringer
Tube Stringer
```

工业楼梯常见踏步包括：

- 花纹钢板；
- 钢格栅；
- 防滑板；
- 混凝土填充 Stair Pan。

### 对切管 CAM 的意义

虽然踏步经常不是管件，但以下部件都非常适合切管：

```text
扶手
立柱
平台框架
部分梯梁
横撑
连接管
```

**建议优先级：P0 / P1。**

---

# 6. 螺旋、弧形和空间节省型楼梯

## 6.1 螺旋楼梯 Spiral Stair

典型组成：

```text
CenterColumn
Tread[]
OuterRail
Baluster[]
ConnectionParts
```

其中：

- 中心柱通常是圆管；
- 栏杆立柱通常也是管；
- 外圈扶手需要滚弯；
- 踏步通常为板件、框架或组合件。

核心参数：

```text
FloorHeight              H
CenterColumnDiameter     Dc
InnerRadius              Ri
OuterRadius              Ro
StairDiameter            D
TreadCount               N
TotalRotationAngle       Φ
RotationDirection        CW / CCW
```

每一级踏步旋转角：

\[
\Delta \varphi = \frac{\Phi}{N}
\]

每级高度：

\[
R = \frac{H}{N}
\]

**建议优先级：P2。**

---

## 6.2 弧形 / Helical / Curved Stair

Helical 楼梯不一定围绕一根中心柱旋转，常有：

```text
InnerCurvedStringer
OuterCurvedStringer
```

需要：

- 型材滚弯；
- 空间曲线；
- 复杂踏步；
- 曲线栏杆。

对于纯切管 CAM 第一阶段价值有限。

**建议优先级：P2 / P3。**

---

## 6.3 船梯 Ships Ladder / Ship Stair

用于：

- 屋顶；
- 设备平台；
- 塔架；
- 夹层；
- 工业设备。

特点：

- 坡度大；
- 占地小；
- 两侧通常设置扶手；
- 踏步较窄。

结构仍然通常是：

```text
Stringer ×2
Tread ×N
Handrail ×2
```

几何和拆单并不复杂。

---

## 6.4 交错踏步梯 Alternating Tread Stair

踏步左右交错，常用于工业场景和紧凑空间。

从 CAM 角度：

```text
StraightFlight
+
AlternatingTreadPattern
```

即可，不需要重新定义路线。

**建议优先级：P1 / P2。**

---

# 7. 踏步系统

钢楼梯的“钢”并不代表踏步一定是钢管。

真实产品中踏步可以非常多样。

## 7.1 木踏步

现代单梁、双梁钢楼梯中非常常见：

```text
钢结构
+
实木踏板
```

切管 CAM 只负责：

- 梯梁；
- 踏步支架；
- 踏步框架。

木踏板作为外购 / 独立零件。

## 7.2 钢板踏步

例如：

```text
花纹钢板
防滑板
冲孔板
```

属于板材加工。

## 7.3 钢格栅踏步

工业楼梯非常常见。

通常带：

- 端板；
- 安装孔；
- 防滑前缘。

主要属于钢格栅 / 板件供应链。

## 7.4 Stair Pan

商业钢楼梯经常使用折弯钢板形成踏步托盘：

```text
Steel Pan
+
Concrete Fill
```

本质是：

```text
钣金切割
+
折弯
```

而不是切管。

## 7.5 玻璃 / 石材踏步

钢结构负责支撑，玻璃、石材作为覆盖件。

---

# 8. 踏步支撑方式

对切管 CAM，真正需要重点建模的往往不是踏板，而是：

```text
TreadSupport
```

常见方式：

```text
SingleBracket
TwinBracket
CrossTubeFrame
AngleFrame
PlateBracket
```

TreadBracket 可以由：

- 矩形管；
- 方管；
- 钢板；
- 折弯件；

构成。

---

# 9. 平台 Landing

L 型、U 型、多跑楼梯最重要的附加结构是平台。

建议平台建模为独立对象：

```text
Landing
│
├─ Frame
├─ CrossMember[]
├─ Deck
└─ Support[]
```

平台框架非常适合：

```text
矩形管
方管
槽钢
```

基本参数：

```text
LandingWidth
LandingLength
LandingElevation
TurnAngle

FrameProfile
CrossMemberProfile
CrossMemberSpacing
```

---

# 10. 栏杆与扶手

钢楼梯通常还包含完整护栏系统：

```text
Guard / Handrail
│
├─ Post[]
├─ TopRail
├─ Handrail
└─ Infill
```

常见扶手形式：

```text
圆管扶手
方管扶手
矩形管扶手
木扶手 + 钢立柱
```

常见填充方式：

```text
竖杆
横杆
钢索
玻璃
钢板
网板
```

其中最适合切管的是：

```text
Post
TopRail
HorizontalRail
VerticalPicket
```

---

# 11. 楼梯核心几何参数

建议统一定义：

```text
FloorHeight          H
StairWidth           W
AvailableRun         L

RiserCount           N
RiserHeight          R
TreadGoing           G

StairAngle           θ
FlightCount

LandingWidth
LandingLength
```

每级高度：

\[
R = \frac{H}{N}
\]

楼梯坡角：

\[
\theta = \arctan\left(\frac{R}{G}\right)
\]

如果一跑总垂直高度为：

\[
H_f
\]

总水平长度为：

\[
L_f
\]

则梯梁中心线长度：

\[
L_s = \sqrt{H_f^2 + L_f^2}
\]

但真实下料长度还要考虑：

```text
StartConnection
EndConnection
EndCutPlane
InsertionDepth
MountingPlate
```

因此最终不应只用中心线长度作为成品下料长度，而应通过连接面求交得到真实端部。

---

# 12. 梯梁的端部连接

典型连接包括：

```text
FloorConnection
LandingConnection
BeamConnection
WallConnection
ColumnConnection
```

连接方式可能是：

```text
直接焊接
端板螺接
插入式
连接耳板
角码
节点板
```

因此每根 Stringer 最终需要记录：

```text
StartJoint
EndJoint
```

---

# 13. 最适合的参数化抽象

不建议建立：

```text
StraightStair
LStair
UStair
ZStair
```

四套完全独立模型。

更合理的是：

```text
StairAssembly
│
├─ Flight[]
├─ Landing[]
├─ SupportSystem
├─ TreadSystem
└─ GuardSystem
```

## Flight

```text
Flight
{
    StartPoint
    Direction

    Width

    RiserCount
    RiserHeight
    TreadGoing

    StartElevation
    EndElevation
}
```

## Landing

```text
Landing
{
    Width
    Length

    Elevation

    IncomingDirection
    OutgoingDirection
}
```

转向角：

```text
0°       同方向
90°      L 型
180°     U 型
其他      特殊多跑
```

因此直跑、L、U、Z 可以全部统一。

---

# 14. SupportSystem

承重结构单独定义：

```text
SupportSystem =
{
    MonoStringer,
    TwinStringer,
    SideStringer,
    TubeStringer,
    ChannelStringer,
    BoxStringer,
    ZigZagStringer,
    Cantilever
}
```

这样：

```text
U 型 + 单中梁
U 型 + 双梁
U 型 + 两侧槽钢梁
```

都只是参数组合。

---

# 15. TreadSystem

```text
TreadSystem
{
    Type

    Width
    Depth
    Thickness

    SupportType
    Material
}
```

Type：

```text
Wood
SteelPlate
CheckerPlate
Grating
Pan
Glass
Stone
```

---

# 16. GuardSystem

```text
GuardSystem
{
    Side

    PostProfile
    PostSpacing

    TopRailProfile
    HandrailProfile

    InfillType
}
```

---

# 17. 直跑单中梁楼梯拆单示例

例如：

```text
Straight
+
MonoStringer
+
WoodTread
+
TubeRailing
```

拆单：

```text
StairAssembly
│
├─ CenterStringer            ×1
│
├─ TreadSupport
│   ├─ Bracket-001
│   ├─ Bracket-002
│   ├─ ...
│   └─ Bracket-N
│
├─ Tread                     ×N
│
├─ LeftGuard
│   ├─ Post                  ×M
│   ├─ TopRail               ×1
│   └─ Infill                若干
│
└─ RightGuard
    ├─ Post                  ×M
    ├─ TopRail               ×1
    └─ Infill                若干
```

如果支架完全相同：

```text
TreadBracket
Qty = N
```

即可归并。

---

# 18. U 型双梁楼梯拆单示例

```text
StairAssembly
│
├─ Flight 1
│   ├─ Stringer-L
│   ├─ Stringer-R
│   └─ TreadSupport[]
│
├─ Landing
│   ├─ Frame[]
│   └─ CrossMember[]
│
├─ Flight 2
│   ├─ Stringer-L
│   ├─ Stringer-R
│   └─ TreadSupport[]
│
└─ GuardSystem
```

因此：

> U 型楼梯根本不需要特殊的“U 型梯梁算法”，只需要两个普通 Flight 加一个 Landing。

---

# 19. 切管 CAM 最值得支持的零件

## 19.1 高价值管件

```text
MonoStringer
TwinStringer
TubeSideStringer

LandingFrame
CrossMember

TreadSupportTube

GuardPost
TopRail
Handrail
Picket
HorizontalRail

SupportColumn
Bracing
```

## 19.2 更适合平面激光的零件

```text
PlateStringer
ZigZagPlateStringer

ConnectionPlate
BasePlate
Gusset

TreadPlate
TreadPan
```

## 19.3 需要滚弯的零件

```text
SpiralHandrail
CurvedStringer
HelicalRail
```

这类应与滚弯 / 弯管能力结合，不应只靠二维切管解决。

---

# 20. TubePart 拆单数据

最终管件建议统一输出：

```text
TubePart
{
    PartId
    Role

    Profile
    Material

    Length

    StartCutPlane
    EndCutPlane

    HoleFeatures[]
    SlotFeatures[]
    CutFeatures[]

    Quantity

    AssemblyTransform
}
```

---

# 21. PlatePart

由于钢楼梯天然会混合管材和板材，建议同时允许：

```text
PlatePart
{
    PartId
    Role

    Thickness
    Material

    Contour
    HoleFeatures[]

    Quantity
}
```

这样才能正确描述：

```text
管梁 + 钢板支架
管扶手 + 底板
槽钢梯梁 + 连接板
```

而不是为了“切管”强行把所有零件解释成管子。

---

# 22. 相同零件自动归并

如果多个踏步支架：

- 管型相同；
- 长度相同；
- 切面相同；
- 孔槽相同；

则合并：

```text
TreadBracket-A
Qty = 14
```

栏杆立柱同理。

这对于：

- BOM；
- 套料；
- 批量加工；
- 生产管理；

非常重要。

---

# 23. 款式合并建议

## 23.1 直跑、L、U、Z

统一为：

```text
Flight[]
+
Landing[]
```

它们只是路线组合不同。

## 23.2 单梁、双梁、边梁

统一作为：

```text
SupportSystem
```

属于结构策略，不属于路线。

## 23.3 悬浮楼梯

不要独立定义：

```text
FloatingStair
```

因为“悬浮”可能通过：

```text
MonoStringer
ZigZag
Cantilever
HiddenSupport
```

实现。

应记录真实承重结构。

## 23.4 木踏步、玻璃踏步、石材踏步

只是：

```text
TreadSystem.Type
```

不同，不应成为独立楼梯款式。

## 23.5 玻璃栏杆、钢索栏杆、竖杆栏杆

只是：

```text
GuardSystem.InfillType
```

不同。

---

# 24. 第一阶段 P0

如果目标是服务切管行业，最值得先做：

1. **直跑楼梯**：`Flight ×1`
2. **L 型平台楼梯**：`Flight + Landing + Flight`
3. **U 型折返楼梯**：`Flight + Landing + Flight`
4. **单中矩形管梁**：`MonoStringer`
5. **双矩形管梁**：`TwinStringer`
6. **两侧管梁**：`SideTubeStringer`
7. **平台矩形管框架**：`LandingFrame`
8. **踏步支撑**：`TubeBracket / PlateBracket / CrossTubeFrame`
9. **管材栏杆**：`Post / TopRail / Handrail / Picket / HorizontalRail`
10. **自动拆单**：输出 `TubePart[] + PlatePart[]`

---

# 25. 第二阶段 P1

建议加入：

```text
槽钢工业梯梁
工业钢格栅踏步
工业平台
多跑 / Z 型
锯齿板梁
交错踏步梯
船梯
复杂栏杆填充
```

---

# 26. 第三阶段 P2

再考虑：

```text
Winder 扇形转角
真正悬挑楼梯
螺旋楼梯
弧形 / Helical 楼梯
滚弯梯梁
曲线扶手
```

这些会明显增加：

- 空间几何；
- 弯管；
- 滚弯；
- 板件；
- 结构节点；

复杂度。

---

# 27. 推荐的软件对象模型

```text
StairAssembly
│
├─ Geometry
│   ├─ FloorHeight
│   ├─ StairWidth
│   └─ Flight[]
│
├─ Landing[]
│
├─ SupportSystem
│   ├─ Type
│   ├─ Profile
│   ├─ Offset
│   └─ JointRule
│
├─ TreadSystem
│   ├─ Type
│   ├─ Tread
│   └─ Support
│
├─ GuardSystem
│   ├─ Post
│   ├─ TopRail
│   ├─ Handrail
│   └─ Infill
│
└─ ConnectionSystem
    ├─ FloorJoint
    ├─ LandingJoint
    └─ BasePlate
```

---

# 28. 推荐的参数层次

## 一级：总体

```text
总高度
楼梯宽度
路线类型
转向
```

## 二级：踏步

```text
级数
级高
踏步深度
踏步类型
```

## 三级：钢结构

```text
单中梁 / 双梁 / 侧梁
型材规格
梁间距
梁偏置
```

## 四级：平台

```text
平台尺寸
平台型材
横撑数量 / 间距
```

## 五级：栏杆

```text
左 / 右
立柱管型
扶手管型
立柱间距
填充方式
```

## 六级：连接

```text
焊接
端板
螺栓
底板
安装孔
```

---

# 29. 最终建议

钢楼梯不应该按照市场上看到的几十种外观名称逐一建立模板。

最合理的底层模型是：

> **楼梯路线 + 梯跑 + 平台 + 承重结构 + 踏步系统 + 栏杆系统**

其中：

```text
Straight / L / U / Z
```

只负责描述路线。

```text
Mono / Twin / Side / ZigZag / Channel / Cantilever
```

只负责描述承重结构。

```text
Wood / Plate / Grating / Pan / Glass
```

只负责描述踏步。

```text
Tube / Cable / Glass / Picket
```

只负责描述栏杆。

这样，一个产品可以自由组合成：

```text
U 型
+
双矩形管梯梁
+
木踏步
+
方管立柱
+
圆管扶手
```

或者：

```text
直跑
+
两侧槽钢梁
+
钢格栅踏步
+
工业圆管护栏
```

而不需要为每一个组合重新开发一种“楼梯款式”。

---

# 30. 对切管 CAM 最重要的产品边界

如果当前产品的核心仍然是切管，那么钢楼梯模块第一阶段应优先服务：

```text
直管梯梁
矩形管梯梁
方管梯梁
管材平台
管材踏步支架
管材护栏
```

而以下能力不要为了完整性过早投入：

```text
大面积板式锯齿梁
折弯 Stair Pan
复杂 Winder
真正悬挑结构
螺旋 / Helical
大半径滚弯结构
```

它们分别更依赖：

```text
平面激光
折弯
结构设计
滚弯 / 弯管
```

而不是纯切管。

因此最值得首先产品化的母型可以进一步收敛为：

```text
StairAssembly
    =
Flight[]
+
Landing[]
+
TubeStringerSystem
+
TreadSupportSystem
+
TubeGuardSystem
```

这套模型已经能够覆盖相当大一部分适合切管加工的住宅、商业和工业钢楼梯。

---

# 31. 调研参考

本次调研主要参考了以下钢楼梯制造商、工业楼梯系统和部件资料：

1. **Acadia Stairs — Straight Stringer Stairs**
   - 将直线钢楼梯的主要结构归纳为 single stringer、double stringer、side stringer，并说明可组成 straight、L、U、winder 等布局。

2. **Mrail — Minimalist Modern Stairs**
   - 产品覆盖 cantilever、central stringer、mono-stringer、twin stringer、side stringer、zig-zag 等现代钢楼梯结构。

3. **Muzata — Floating Stair / Dual Stringer Stairs**
   - 展示 mono stringer、double stringer、zig-zag 等典型现代钢结构楼梯。

4. **Redbird Engineering Sales — Stairs and Handrail**
   - 工业楼梯梯梁包含 plate stringer、channel stringer、box stringer、tube stringer，踏步包括 checker plate、grating、concrete-fill pan 等。

5. **Lapeyre Stair — Industrial Stairs**
   - 工业楼梯采用 channel stringer，并提供 diamond plate、bar grate、smooth plate 等踏步及管材护栏。

6. **Lapeyre Stair / Duvinage — Alternating Tread Stairs、Ships Ladders**
   - 用于分析空间节省型工业楼梯和陡梯结构。

7. **Stair Components & Systems**
   - 用于调研 stair pan、landing pan、tread、carrier angle 等商业钢楼梯部件。

8. **Pacific Stair Corporation 钢楼梯系统规范资料**
   - 用于确认工业 / 商业钢楼梯中 plate、channel、pipe / tube railing 等常见制造体系。

9. **钢楼梯相关工程规范资料**
   - 可见 Stringer 可采用 steel plate、steel channel、steel rectangular tube；平台框架同样可以采用 rectangular tube 等结构。

---

## 说明

本文中的尺寸、结构形式和参数属于**产品建模与制造拆单调研**，不是具体工程项目的结构设计结论。

实际钢楼梯的：

- 梯梁截面；
- 板厚；
- 焊缝；
- 锚固；
- 荷载；
- 踏步尺寸；
- 栏杆高度；
- 安全间隙；

应根据项目所在地规范和结构工程计算确定。
