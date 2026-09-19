# Sigma 型钢参数化几何定义与 CAM 建模建议

## 1. 文档目的

本文用于定义 Sigma 型钢（Σ 型钢）的：

- 截面拓扑；
- 参数体系；
- 几何构造方法；
- 对称与非对称变体；
- 圆角和等厚 Offset 规则；
- CAM 面语义；
- 合法性检查；
- 数据结构和 UI 建议。

本文只讨论**等截面、等厚、冷弯/辊弯 Sigma 型钢**。

不讨论：

- 热轧异型钢；
- 沿长度方向变截面的构件；
- 局部压筋、局部冲压成形；
- 多腔闭口型材；
- 钢板拼焊组合梁。

---

# 2. Sigma 型钢是什么

Sigma 型钢本质上是一种复杂冷弯开口型材。

它可以理解为：

> **卷边 C 型钢 + 腹板上的偏移折弯加强。**

普通卷边 C 型钢的腹板通常是一条直线：

```text
      ┌──────────┐
      │          │
      │
      │
      │
      └──────────┐
                 │
```

Sigma 型钢在腹板中增加两组过渡折弯，使中央腹板相对上下外腹板产生横向偏移：

```text
             上翼缘
       ┌────────────┐
       │            └─ 上卷边
       │
        ╲
         ╲          上斜过渡
          │
          │         中央腹板
          │
         ╱          下斜过渡
        ╱
       │
       └────────────┐
             下翼缘 └─ 下卷边
```

其主要识别特征是：

- 等厚板带成形；
- 开口截面；
- 上下翼缘通常朝同一侧；
- 翼缘自由端通常带卷边；
- 腹板存在附加加强折弯；
- 中央腹板相对外腹板发生偏移。

---

# 3. Sigma 不是全球唯一固定截面

“Sigma 型钢”描述的是一类截面思想，不是一个全球统一的单一几何。

不同厂家可能存在：

- 对称 Sigma；
- 不对称 Sigma；
- Sigma Plus；
- 等翼缘或不等翼缘；
- 等卷边或不等卷边；
- 不同中央腹板高度；
- 不同腹板偏移量；
- 不同斜过渡长度；
- 不同折弯半径；
- 直卷边或斜卷边；
- 无卷边版本。

因此不建议只定义：

\[
Sigma(H,B,C,t,R)
\]

因为这一组参数无法唯一确定腹板的 Sigma 折线。

更合理的产品结构是：

```text
复杂等厚开口型材
└─ Sigma 快捷模板
```

底层仍应由通用的：

\[
\boxed{
\text{中心层折线/圆弧}+\text{等厚 Offset}
}
\]

生成器负责。

---

# 4. 推荐的截面方向

建议建立统一局部坐标系：

```text
y
↑

│                  开口方向
│                     →
│
└────────────────────────→ x
```

约定：

- \(+y\)：截面上方；
- \(+x\)：翼缘伸出及截面开口方向；
- 型材轴向为 \(+z\)；
- 上下外腹板参考线位于 \(x=0\)；
- 中央腹板位于 \(x=e\)。

其中：

\[
e>0
\]

表示中央腹板向开口方向偏移。

镜像件不重新定义几何类型，只记录：

```text
mirrorX
mirrorY
```

或截面朝向矩阵。

---

# 5. 推荐的两层几何表达

## 5.1 第一层：Sigma 参数模板

Sigma 模板负责让用户通过少量参数快速生成常见轮廓。

## 5.2 第二层：通用等厚开口轮廓

模板生成后，统一转换成：

```cpp
OpenThinWallProfile
{
    centerPath;
    thickness;
    bendData;
}
```

也就是说：

> Sigma 只是参数输入方式，不应该形成另一套独立的几何内核。

这样后续的：

- 货架立柱；
- 光伏支架；
- 檐口梁；
- 多折边 C 型钢；
- 非标檩条；

都可以复用同一个等厚开口截面生成器。

---

# 6. 标准 Sigma 中心层拓扑

从上卷边自由端沿板带中心层走到下卷边自由端，推荐固定语义顺序：

```text
上卷边
  ↓
上翼缘
  ↓
上外腹板
  ↓
上斜过渡
  ↓
中央腹板
  ↓
下斜过渡
  ↓
下外腹板
  ↓
下翼缘
  ↓
下卷边
```

对应 9 段主体直线：

1. TopLip
2. TopFlange
3. UpperOuterWeb
4. UpperTransition
5. CenterWeb
6. LowerTransition
7. LowerOuterWeb
8. BottomFlange
9. BottomLip

各相邻直线之间再由相切圆弧连接。

---

# 7. 推荐参数

## 7.1 基础参数

推荐模板参数：

| 参数 | 含义 |
|---|---|
| \(H\) | 截面外轮廓总高度 |
| \(B_t\) | 上翼缘宽度 |
| \(B_b\) | 下翼缘宽度 |
| \(C_t\) | 上卷边长度 |
| \(C_b\) | 下卷边长度 |
| \(t\) | 板厚 |
| \(R_i\) | 默认折弯内圆角 |
| \(e\) | 中央腹板相对外腹板的横向偏移量 |
| \(a_t\) | 上翼缘以下的上外腹板直段高度 |
| \(a_b\) | 下翼缘以上的下外腹板直段高度 |
| \(q_t\) | 上斜过渡的竖向投影 |
| \(q_b\) | 下斜过渡的竖向投影 |
| \(\theta_t\) | 上卷边与上翼缘夹角 |
| \(\theta_b\) | 下卷边与下翼缘夹角 |

内部还需要得到中央腹板高度：

\[
h_c
\]

---

## 7.2 内部中心层高度

若 \(H\) 定义为截面最上表面到最下表面的外尺寸，并且上下翼缘水平、板厚一致，则上下翼缘中心层之间的距离为：

\[
\boxed{
H_m=H-t
}
\]

其中：

\[
H_m=\text{上下翼缘中心层间距}
\]

中央腹板中心层直段高度：

\[
\boxed{
h_c=H_m-a_t-q_t-a_b-q_b
}
\]

必须满足：

\[
h_c>0
\]

---

# 8. 对称 Sigma 的简化参数

对于上下对称的 Sigma：

\[
B_t=B_b=B
\]

\[
C_t=C_b=C
\]

\[
a_t=a_b=a
\]

\[
q_t=q_b=q
\]

\[
\theta_t=\theta_b=\theta
\]

于是：

\[
\boxed{
h_c=H_m-2a-2q
}
\]

对称模板可简化为：

\[
\boxed{
Sigma(H,B,C,t,R_i,e,a,q,\theta)
}
\]

其中直卷边通常：

\[
\theta=90^\circ
\]

---

# 9. 理论中心层顶点

以下先忽略圆角，将各折弯位置视为理论直线交点。

取：

\[
y_t=\frac{H_m}{2}
\]

\[
y_b=-\frac{H_m}{2}
\]

外腹板参考线：

\[
x=0
\]

中央腹板：

\[
x=e
\]

则主体折线关键点为：

\[
P_1=(0,y_t)
\]

上翼缘与上外腹板理论交点。

\[
P_2=(0,y_t-a_t)
\]

上外腹板与上斜过渡理论交点。

\[
P_3=(e,y_t-a_t-q_t)
\]

上斜过渡与中央腹板理论交点。

\[
P_4=(e,y_b+a_b+q_b)
\]

中央腹板与下斜过渡理论交点。

\[
P_5=(0,y_b+a_b)
\]

下斜过渡与下外腹板理论交点。

\[
P_6=(0,y_b)
\]

下外腹板与下翼缘理论交点。

中央腹板高度：

\[
|P_3P_4|=h_c
\]

---

# 10. 斜过渡几何

## 10.1 上斜过渡长度

\[
\boxed{
L_t=\sqrt{e^2+q_t^2}
}
\]

## 10.2 下斜过渡长度

\[
\boxed{
L_b=\sqrt{e^2+q_b^2}
}
\]

## 10.3 相对于竖直方向的倾角

上过渡偏离竖直方向的角度：

\[
\boxed{
\varphi_t=\arctan\left(\frac{|e|}{q_t}\right)
}
\]

下过渡：

\[
\boxed{
\varphi_b=\arctan\left(\frac{|e|}{q_b}\right)
}
\]

## 10.4 相对于水平方向的锐角

\[
\boxed{
\alpha_t=\arctan\left(\frac{q_t}{|e|}\right)
}
\]

\[
\boxed{
\alpha_b=\arctan\left(\frac{q_b}{|e|}\right)
}
\]

两组角度满足：

\[
\alpha+\varphi=90^\circ
\]

不建议同时让用户输入：

\[
e,\ q,\ L,\ \alpha
\]

否则会产生过约束。

推荐只输入：

\[
e,\ q
\]

其余自动计算。

---

# 11. 翼缘与卷边

## 11.1 上翼缘

上翼缘中心层理论线从：

\[
(0,y_t)
\]

沿 \(+x\) 方向延伸。

其理论长度为：

\[
B_t
\]

## 11.2 下翼缘

下翼缘从：

\[
(0,y_b)
\]

沿 \(+x\) 方向延伸。

其理论长度为：

\[
B_b
\]

## 11.3 卷边

常见内卷边：

- 上卷边向下；
- 下卷边向上；
- 默认与翼缘垂直。

直卷边：

\[
\theta_t=\theta_b=90^\circ
\]

若支持斜卷边，则：

\[
0^\circ<\theta_t,\theta_b<180^\circ
\]

卷边长度应沿卷边自身方向测量。

---

# 12. 圆角体系

Sigma 型钢的中心层通常包含 8 个折弯位置：

1. 上卷边—上翼缘；
2. 上翼缘—上外腹板；
3. 上外腹板—上斜过渡；
4. 上斜过渡—中央腹板；
5. 中央腹板—下斜过渡；
6. 下斜过渡—下外腹板；
7. 下外腹板—下翼缘；
8. 下翼缘—下卷边。

基础模式可以统一使用：

\[
R_i
\]

高级模式允许每个折弯单独覆盖：

```text
R1 ... R8
```

---

## 12.1 中心层半径

若用户输入的是内圆角：

\[
R_i
\]

则中心层半径：

\[
\boxed{
R_c=R_i+\frac{t}{2}
}
\]

外圆角半径：

\[
\boxed{
R_o=R_i+t
}
\]

---

## 12.2 圆心角

每个圆弧的圆心角不应作为独立输入参数。

它由相邻两段直线方向自动确定。

设折弯处两段中心线的方向变化角为：

\[
\Delta
\]

则相切圆弧的圆心角为：

\[
\boxed{
|\Delta|
}
\]

因此统一采用：

> **两条理论直线 + Fillet 半径**

即可。

---

# 13. 推荐几何构造流程

## Step 1：建立理论中心折线

按固定语义顺序建立：

```text
TopLip
TopFlange
UpperOuterWeb
UpperTransition
CenterWeb
LowerTransition
LowerOuterWeb
BottomFlange
BottomLip
```

---

## Step 2：在各理论交点添加中心层 Fillet

每个折弯使用：

\[
R_c=R_i+\frac{t}{2}
\]

或对应的局部中心层半径。

---

## Step 3：生成完整中心路径

得到由：

- 直线；
- 圆弧；

组成的连续 \(G^1\) 中心层路径。

---

## Step 4：向两侧 Offset

沿中心路径法向分别偏移：

\[
+\frac{t}{2}
\]

和：

\[
-\frac{t}{2}
\]

得到板带两侧边界。

---

## Step 5：封闭两个自由端

连接：

- 上卷边自由端两侧边界；
- 下卷边自由端两侧边界。

形成完整闭合材料区域。

---

## Step 6：轮廓检查

检查：

- 连续性；
- 闭合性；
- 自交；
- 小边；
- 反向圆弧；
- Offset 失败；
- 卷边与腹板干涉。

---

# 14. 为什么应使用中心层模型

Sigma 型钢属于典型等厚冷弯板件。

使用中心层模型有以下优点：

- 板厚只有一个参数；
- 内外圆角自动满足厚度关系；
- 各斜面和卷边方向一致；
- 易于生成展开长度；
- 易于建立各面语义；
- 易于处理厂家不同 Sigma 变体；
- 与 Z、C、帽型等冷弯型材共用内核。

因此推荐：

\[
\boxed{
\text{中心层折线/圆弧}
+
\text{等厚 Offset}
}
\]

而不是分别手工拼外轮廓和内轮廓。

---

# 15. 参数尺寸基准必须明确

厂家图纸中的：

- WEB；
- FLANGE；
- LIP；
- \(a\)；
- \(b\)；
- 总高；
- 净高；

不一定采用相同测量基准。

可能分别使用：

- 外尺寸；
- 中心层尺寸；
- 理论尖角尺寸；
- 切点间尺寸；
- 净空尺寸。

因此数据库中建议同时保存：

```text
dimensionBasis
sourceDrawing
manufacturer
series
revision
```

推荐枚举：

```cpp
enum class DimensionBasis
{
    OuterDimensions,
    CenterlineDimensions,
    TheoreticalIntersection,
    ManufacturerDrawing
};
```

从厂家规格表导入时，不应仅凭参数名称猜测测量方式。

---

# 16. 退化关系

Sigma 模板可以自然退化成其他截面。

## 16.1 退化为卷边 C 型钢

若：

\[
e=0
\]

则中央腹板与上下外腹板共线。

截面退化为：

\[
\boxed{
\text{卷边 C 型钢}
}
\]

---

## 16.2 退化为普通 C 型钢

若：

\[
e=0
\]

并且：

\[
C_t=C_b=0
\]

则退化为普通 C 型钢。

---

## 16.3 无卷边 Sigma

若：

\[
C_t=C_b=0
\]

但：

\[
e\neq0
\]

则得到无卷边 Sigma 变体。

---

## 16.4 不对称 Sigma

允许：

\[
B_t\neq B_b
\]

\[
C_t\neq C_b
\]

\[
a_t\neq a_b
\]

\[
q_t\neq q_b
\]

这仍然可以使用同一模板。

若中央腹板不再竖直，或上下偏移量不同，则建议转入通用自定义开口轮廓，而不是继续增加模板参数。

---

# 17. 合法性检查

## 17.1 基础尺寸

必须满足：

\[
H>t>0
\]

\[
B_t>0,\quad B_b>0
\]

\[
C_t\ge0,\quad C_b\ge0
\]

\[
R_i\ge0
\]

\[
a_t\ge0,\quad a_b\ge0
\]

\[
q_t>0,\quad q_b>0
\]

---

## 17.2 中央腹板高度

\[
\boxed{
h_c=H-t-a_t-a_b-q_t-q_b>0
}
\]

---

## 17.3 每段直线必须有剩余长度

对中心层某折弯，设相邻直线方向变化角为：

\[
\Delta_j
\]

中心层圆角半径为：

\[
R_{c,j}
\]

理论交点到切点的占用长度为：

\[
\boxed{
d_j=R_{c,j}\tan\left(\frac{|\Delta_j|}{2}\right)
}
\]

每一段理论直线的原始长度必须大于两端圆角切点占用长度之和。

否则会出现：

- 负直线段；
- 相邻圆角重叠；
- Fillet 失败。

---

## 17.4 Offset 后不得自交

必须检查：

- 上下卷边是否碰到斜腹板；
- 卷边是否穿过中央腹板；
- 两侧 Offset 是否翻转；
- 小半径处是否形成自交；
- 局部材料宽度是否小于板厚。

---

## 17.5 开口净空

几何合法不代表设备可加工。

还应单独计算：

- 截面开口宽度；
- 反入区深度；
- 割头进入空间；
- 喷嘴到相邻面的安全距离；
- 卡盘或支撑轮干涉。

这些属于 CAM/设备约束，不应混入基础截面几何参数。

---

# 18. CAM 面语义

Sigma 型钢不能按“上、下、左、右四个面”处理。

推荐沿截面中心路径建立明确语义：

```cpp
enum class SigmaFace
{
    TopLip,
    TopFlange,
    UpperOuterWeb,
    UpperTransition,
    CenterWeb,
    LowerTransition,
    LowerOuterWeb,
    BottomFlange,
    BottomLip
};
```

圆角面可单独编号：

```text
Bend01 ... Bend08
```

这样加工特征可以准确绑定到：

- 翼缘；
- 外腹板；
- 斜过渡面；
- 中央腹板；
- 卷边。

---

# 19. 展开坐标

建议对中心层建立弧长参数：

\[
s
\]

沿型材轴向使用：

\[
z
\]

于是薄壁截面的展开参数空间可表示为：

\[
\boxed{
(s,z)
}
\]

其中：

- \(s\)：沿截面中心路径的累计弧长；
- \(z\)：沿型材轴向的位置。

这样可统一描述：

- 孔；
- 槽；
- 重复孔组；
- 标记；
- 端部轮廓；
- 跨面特征。

但最终切割轨迹仍必须映射回真实三维面。

---

# 20. 特征放置建议

加工特征应保存：

```text
hostFace
localUV
orientation
throughMode
normalDirection
```

不建议只保存：

```text
离左边多少
离底边多少
```

原因是 Sigma 截面存在：

- 多个竖直面；
- 多个斜面；
- 反入区；
- 卷边；
- 多处相似但不共面的腹板。

---

# 21. 激光切割中的特殊风险

## 21.1 反入区域遮挡

卷边和中央腹板偏移会形成局部反入区。

需要判断：

- 割头能否正对目标面；
- 激光是否会穿过目标面后打到另一层材料；
- 喷嘴是否会撞到卷边；
- 切渣是否落到相邻面。

---

## 21.2 斜过渡面

UpperTransition 和 LowerTransition 是独立加工面。

不能把孔槽简单投影到外腹板或中央腹板。

---

## 21.3 圆角跨面加工

默认情况下，普通二维孔槽应限制在单个平面内。

若轮廓跨越折弯圆角，应明确采用：

- 三维曲面切割；
- 展开后重新映射；
- 分面切割；
- 禁止跨弯。

---

## 21.4 镜像和装夹方向

同一 Sigma 截面可能：

- 开口朝左或朝右；
- 卷边朝内或朝外；
- 上下翻转；
- 前后镜像。

必须保存实际截面姿态，不能只保存参数值。

---

# 22. 推荐数据结构

## 22.1 Sigma 模板参数

```cpp
enum class DimensionBasis
{
    OuterDimensions,
    CenterlineDimensions,
    TheoreticalIntersection,
    ManufacturerDrawing
};

struct SigmaSectionParams
{
    // 用户/规格尺寸
    double overallHeight;          // H
    double topFlangeWidth;         // Bt
    double bottomFlangeWidth;      // Bb
    double topLipLength;           // Ct
    double bottomLipLength;        // Cb

    // 腹板 Sigma 形状
    double topOuterWebHeight;      // at
    double bottomOuterWebHeight;   // ab
    double upperTransitionRise;    // qt
    double lowerTransitionRise;    // qb
    double centerWebOffset;        // e

    // 板带
    double thickness;              // t
    double defaultInnerRadius;     // Ri

    // 卷边
    double topLipAngle;
    double bottomLipAngle;

    // 尺寸来源
    DimensionBasis dimensionBasis;

    // 姿态
    bool mirrorX;
    bool mirrorY;
};
```

---

## 22.2 高级局部圆角

```cpp
struct SigmaBendRadii
{
    double topLip;
    double topFlangeWeb;
    double upperOuterTransition;
    double upperTransitionCenter;
    double lowerCenterTransition;
    double lowerTransitionOuter;
    double bottomWebFlange;
    double bottomLip;
};
```

基础 UI 使用统一 \(R_i\)。

厂家图纸或高级模式才启用局部覆盖。

---

## 22.3 统一通用轮廓

Sigma 模板最终应转换为：

```cpp
struct OpenThinWallProfile
{
    CompositeCurve2D centerPath;
    double thickness;

    std::vector<SegmentSemantic> segmentSemantics;
    std::vector<BendSemantic> bendSemantics;

    Matrix2D localFrame;
};
```

后续几何、特征映射、碰撞和三维拉伸都只使用这一统一结构。

---

# 23. 推荐 UI

## 23.1 简化模式

```text
Sigma 型钢

总高度 H
翼缘宽度 B
卷边长度 C
板厚 t
折弯半径 R

中央腹板偏移 e
外腹板直段 a
斜过渡竖向高度 q

卷边角度 θ
```

默认对称：

```text
Bt = Bb
Ct = Cb
at = ab
qt = qb
```

---

## 23.2 高级模式

展开：

```text
上翼缘 Bt
下翼缘 Bb

上卷边 Ct
下卷边 Cb

上外腹板 at
下外腹板 ab

上过渡 qt
下过渡 qb

中央腹板偏移 e

上卷边角 θt
下卷边角 θb

R1 ... R8
```

---

## 23.3 厂家规格模式

```text
厂家
系列
规格
版本
```

选择规格后直接读取厂家图纸轮廓。

此时参数主要用于：

- 检索；
- 显示；
- 理论校核。

实际 CAM 轮廓优先使用厂家 CAD 或标准库完整中心路径。

---

# 24. 不建议的实现方式

## 24.1 不建议写死单一 Sigma

不同厂家 Sigma 差异较大。

不能假定所有 Sigma 都满足同一组固定比例。

---

## 24.2 不建议按多个矩形拼接

Sigma 是一张等厚板带连续折弯形成的。

按多个矩形布尔拼接容易产生：

- 错误圆角；
- 局部厚度错误；
- 面语义丢失；
- Offset 不一致。

---

## 24.3 不建议手工分别构造内外轮廓

优先采用中心层路径 + Offset。

---

## 24.4 不建议把冲孔当成截面几何

Sigma 产品上的：

- 圆孔；
- 方孔；
- 服务孔；
- 翼缘缺口；

属于沿长度方向的加工特征，不属于基础截面。

---

## 24.5 不建议把 Sigma 强制升级成一级几何内核

建议：

```text
一级能力：复杂等厚开口型材
快捷模板：Sigma
```

这样更利于长期扩展。

---

# 25. 与 C、Z、帽钢的关系

## C 型钢

```text
────────
│
│
────────
```

腹板基本共线。

## Z 型钢

```text
        ────────
        │
        │
────────
```

上下翼缘向相反方向。

## 帽钢

```text
───┘          └───
   │          │
   └──────────┘
```

具有两个腹板和顶部帽面。

## Sigma 型钢

```text
────────
   ╲
    │
   ╱
────────
```

上下翼缘通常同向，但腹板带附加偏移加强折弯。

因此 Sigma 不能简单归入 Z 或帽钢。

但从几何内核看，它仍属于：

\[
\boxed{
\text{等厚复杂开口冷弯型材}
}
\]

---

# 26. 最终推荐

Sigma 型钢建议采用两层设计：

## 产品层

```text
复杂开口型材
└─ Sigma 预设模板
```

## 几何层

\[
\boxed{
\text{中心层折线/圆弧}
+
\text{统一板厚}
+
\text{双向 Offset}
}
\]

推荐模板参数：

\[
\boxed{
H,\ B_t,\ B_b,\ C_t,\ C_b,\ t,\ R_i,\ e,\ a_t,\ a_b,\ q_t,\ q_b,\theta_t,\theta_b
}
\]

对称模式可简化为：

\[
\boxed{
H,\ B,\ C,\ t,\ R_i,\ e,\ a,\ q,\theta
}
\]

CAM 中必须进一步支持：

- 9 类平面语义；
- 8 类折弯面；
- 截面弧长坐标；
- 镜像和装夹方向；
- 反入区碰撞；
- 斜面特征映射；
- 厂家真实轮廓导入。

最终原则是：

> **Sigma 型钢值得提供快捷模板，但其底层应落在“任意等厚开口型材”能力上，而不是建立一套孤立的专用几何系统。**

---

# 参考资料

1. BSF Steel — Sigma Profiles  
   https://www.bsfsteel.co.uk/sigma-profile

2. voestalpine Sadef — Steel Profile Solutions  
   https://www.voestalpine.com/sadef/en

3. voestalpine Sadef — Downloads / Building and custom roll-forming资料  
   https://www.voestalpine.com/sadef/en/Infocenter/Downloads
