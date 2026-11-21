# Viewport

`Viewport` 是 H5 前端 SDK 内置的三维视口层。

`getGridState()` 返回实际显示网格的尺寸、分格、间距和起点。变换手柄可设置 `snap.translation`（`reference` 为对象原点，另含 `spacing`、`origin`、`gridEnabled`、`tolerancePx`）和 `snap.rotation`（`step`、`tolerance`，弧度）。吸附保持当前轴或平面约束，不把包围盒中心当作对象原点；`onChange.snap` 提供当前吸附结果。吸附标记固定约 14 CSS 像素，不参与拾取；Alt 按下与松开均即时更新当前拖动。默认不启用，产品自行选择规则。

它负责消费 View 快照，并把快照引用的资源池数据转换成具体前端 renderer 对象。产品页面只负责选择 View、创建 viewport，并把 View 快照交给 viewport。

Three.js 视口在前端本地维护相机。取景、标准视图、鼠标旋转和平移不创建后端相机对象，也不改变 View；View 只决定场景内容。

`constrainOrbit: false` 允许俯仰角连续越过两极，使用随子午线变化的相机 up 向量；默认保持受约束旋转。

`getCameraNavigationMode(event)` 可在指针按下时决定相机手势，返回 `orbit`、`pan` 或 `null`。未提供时使用右键旋转、中键平移；FiveAxisCAM 配置为中键旋转、Ctrl＋中键平移。手势开始后保持该模式，松开后重新识别按键。

`setCameraNavigationEnabled(false)` 禁止相机旋转、平移、滚轮缩放和空白双击适配，并终止已开始的相机手势；左键选择和变换手柄继续工作。显式设置视角、投影及自动适配的 API 不受此开关影响。业务工具负责启用和恢复，SDK 不判断产品工具名称。

`orbitAxisSnap: true` 开启旋转方向吸附：最初 4 个 CSS 像素识别方向，最近 24 像素内水平/竖直分量达到 3:1 时吸附，低于 1.5:1 时解除。过滤次方向的小幅抖动，明确转向后恢复，不补回被过滤的位移；每次松开、取消或切换工具重新识别。默认关闭，不改变其他产品的相机操作。

`setTransformPreview(transformId, localToWorld)` 设置临时显示变换（16 个有限数值，列主序），复用原几何和边线并同步拾取对象；`clearTransformPreview(transformId)` 恢复最新 View 变换。预览不改写资源或 View 载荷。产品负责在取消、切换工具/工程、版本冲突时清理，确认后经自己的 SDO 提交。删除变换或清空 View 时 SDK 自动清除对应预览。

`setTransformManipulator(options)` 显示中性的 XYZ 平移轴或旋转圆环。`mode` 为 `translate`/`rotate`，`space` 为 `world`/`local`；`position` 是世界坐标中心，`quaternion` 是物体方向。`onStart`/`onEnd` 返回当前轴，`onEnd` 的 `moved` 表示手势是否越过 3 CSS 像素的拖动阈值，以区分选轴与实际变换；`onChange` 返回从本次拖动起点累计的世界位移 `translation` 和弧度旋转量 `angle`；`onCancel` 通知取消当前手势。`onPosition` 提供中心在画布内的 CSS 像素位置供产品放置数值面板。产品自行维护草稿、预览和提交；SDK 不保存业务变换。`clearTransformManipulator()` 移除手柄。手柄优先接管指针，不同时触发选择或镜头旋转；Esc、窗口失焦、指针捕获丢失取消当前手势。

平移时 `planeTranslate: true` 启用 XY、XZ、YZ 平面手柄；回调的 `axis` 对应平面名称，`translation` 仍为世界位移。单向细轴、小箭头、完整旋转环与缩放方块的外观由 `transformGizmoAppearance.mjs` 管理，拾取区域独立加宽。默认 `sizePixels: 160` 表示一个几何单位的 CSS 像素大小，箭头长约 96 px、旋转环直径约 160 px，透视和正交一致。`worldAxisPixelSize` 和 `worldAxisOpacity` 可单独控制世界参考轴的显示大小与透明度，不改变世界坐标。

旋转环前半圈明亮、后半圈淡显，只拾取朝向观察者的部分。`activeAxis` 保留产品当前轴的高亮；`screenRotate: true` 增加始终面向视线的外圈，回调 `axis: E`。`onStart` 同时提供单位世界向量 `direction`，业务将其保存为本次视图旋转轴；之后调整观察视角不重新解释已经输入的角度。XYZ 环仍遵守 `space`，E 始终绕捕获的视线轴旋转。

`showTransformSpaceToggle: true` 显示世界/局部切换，初始值由 `transformSpace` 指定，`onTransformSpaceChange(space)` 由产品响应并转换草稿；`setTransformSpace(space)` 同步显示偏好。此选项也影响无操作手柄时的选中物体坐标轴方向。`showProjectionToggle: true` 显示透视/正交切换，仅改变视口相机。

坐标系与操作支点独立：世界基准为固定场景原点及 XYZ 方向；物体坐标由该物体的 local-to-world 变换定义。操作手柄的 `position` 只是编辑支点，不能用来重定义世界/物体原点。世界模式的轴方向使用单位方向，局部模式的轴方向来自物体方向。镜头、取景和投影仅改变观察方式；左下角方向标始终显示世界方向。

矩形选择由使用方显式启用：`getRectangleSelectionKey()` 返回当前可选状态的稳定键，返回 `null`/`undefined` 表示当前操作不允许框选。左键拖动超过阈值后显示选框，松开调用 `onRectangleSelect(rectangle, event)`；矩形坐标为浏览器 client 坐标，`mode` 为从左向右的 `contain` 或从右向左的 `crossing`。键变化、Esc、失去指针捕获、窗口失焦或禁用拾取都会取消当前框选。未拖动仍按单击拾取处理，右键旋转、中键平移和空白双击取景保持独立。共享工作台通过 `getAreaViewportRectangleSelectionKey` 和 `handleAreaViewportRectangleSelect` 转发这两个钩子。

`meshFacesMatchingRectangle` 按原始显示网格的面三角形范围进行面框选，支持索引网格、对象变换和相机裁剪。相交模式裁剪填充三角形，因此选框完全落在大面的内部也能选中。此查询包含背面；业务层提供经原生校验的面引用，并自行处理追加、移除及过期响应。

`rectangleSelection.mjs` 提供中性的折线与选框命中计算，处理对象变换、透视、正交和裁剪面；具体对象身份、追加选择及批量操作由产品负责。

键鼠和触控输入由 viewport 本地处理，不进入后端。碰撞 PDO 只保留给机床碰撞等高频计算结果，不能携带场景渲染数据，也不能绕过 View 决定对象可见性。

## 边界

- `View`：唯一的场景投影和可见性契约；一个 View 可以组合多个 EntityView Source，快照包含对象属性及资源 URL。
- `Resources`：几何与材质的版本化数据通道。
- `ThreeRenderViewport`：H5 默认三维视口实现，使用 SDK 内置 Three.js。
- 产品 `webpage`：只负责布局、SDO 调用和把 viewport 放进页面。

几何资源不携带运行时 bounds。Viewport 从 position buffer 和对象 transform 计算包围盒/包围球，用于取景、裁剪和显示缓存。

显示图片与文字位图使用通用 `ICRT` FlatBuffer 纹理资源（[schema](../../../iCAX-Plugins/render/RenderData/Schema/RenderTextureResource.fbs)），保存 MIME、像素尺寸与编码图片。`ICRM.base_color_texture_url` 引用该显示资源，`renderTexture.mjs` 通过原有 ResourceClient 的 FlatBuffer GET 读取并解码，不暴露后台 `resource.binary`。几何使用已有 ICRG UV 网格，照明关闭位读取 `material_flags` 的 bit0。

纹理与几何一起等待后再提交 View，并重验请求代际、项目归属和视口销毁状态。过期贴图及时释放；撤销重新访问旧 URL 可重新加载。未使用的 GPU 纹理和 ImageBitmap 在快路径、完整刷新及场景断开时释放，图片变化不重建画布或侧栏。

Viewport 中的对象 ID 直接来自 View 行的 Entity ID。前端可以把它原样回传用于拾取或交互反馈。

Three.js 是 H5 前端内置选择，不进入 Engine/Framework，也不进入具体产品业务插件。以后如果需要 WebGPU、Babylon、Qt 或 WPF，只要提供同样的 viewport 实现即可。

## 目录结构

- `renderResource.mjs`：资源池几何和材质格式的解析与加载。
- `threeViewport.mjs`：基于 Three.js 的 View viewport。
- `transformManipulator.mjs`：封装内置 TransformControls，负责中性手柄显示、命中和手势生命周期。
- `surfaceProximity.mjs`：按 CSS 像素容差查找附近可见网格表面，支持正交/透视和对象变换；只返回显示三角命中，精确 CAD 基准仍须交给几何服务解析。
- `index.mjs`：Viewport 模块导出。

## 显示模式

启用 `showDisplayModeControl: true` 显示三种模式下拉框，通过 `displayMode` 设置默认模式：
`shaded`（着色）、`shadedEdges`（着色+线框）、`wireframe`（线框）。控件默认在右下角，FiveAxisCAM 的布局将其放在视口左上角；世界/局部切换在顶部居中，透视/正交切换在底部居中。不再提供曲面拼接边复选框。`showSurfaceEdges` 仍是视口 API 的显示选项。

显示偏好由每个 `ThreeRenderViewport` 实例独立持有，调用 `setDisplayMode(mode)`、`setShowSurfaceEdges(visible)` 即时切换。
重新应用 View 数据快照或挂载同一视口不会重置开关；新建视口使用默认值。产品只提供初始选项。

CAD 导入通过中性 `IBRepDisplayService` 按原始曲线和参数区间采样（默认弦差 0.05 个源长度单位），按相邻曲面法向区分特征边与平滑拼接边。
渲染适配层把这些中性数据写入 ICRG 的 `edge_positions`、`edge_ranges`；保存后的显示资源可独立重开，不依赖原 STEP 文件。
CAD 线框使用这些边，不从三角网格猜测拓扑；没有 CAD 边数据的普通网格资源使用自身网格线框。

模式切换仅更新视口材质、边线，不调用原生接口、不修改 View 数据或工程、不重建侧栏。网格几何、焦点、选区、滚动与拾取保留，切割线等独立叠加层不受影响。
未声明显示模式的产品维持各实例的 `meshEdges` 设置。

`showWorldAxes: false` 隐藏场景原点的大坐标轴，左下角独立方向标保持显示。
`worldAxisLabels: true` 为世界原点添加 XYZ 箭头与标签，随视图缩放保持可读大小；世界轴不跟随工件或世界/局部工具切换。`worldAxisLength` 是构建基准长度。`gridSize`、`gridDivisions` 控制世界 XY 平面的网格范围与分格，网格不参与工件拾取和适合窗口计算。FiveAxisCAM 启用世界轴和 2000 mm / 40 格的工作台；新导入工件由原生事务按 CAD 包围盒中心设置初始摆放，原始几何及已保存的摆放不改变。
`blankDoubleClickFitEnabled: true` 使用既有空白双击适配逻辑，模型、标注和视口控件不作为空白；FiveAxisCAM 启用该交互并移除适合窗口按钮。
`immediateBlankPick: true` 使空白单击在鼠标松开时立即调用 `onPick`，不等待双击判断；第二次点击仍可触发适合窗口。FiveAxisCAM 启用此选项，第一次空白点击即取消刀路选择。未启用时，空白单击等待 500 毫秒以避免双击改变选择。

`hoverDetail: 'surface'` 在表面及空白区域的鼠标移动时均通知 `onHover`，支持产品在轮廓外侧按像素距离吸附。离开画布、拖动导航和关闭拾取仍清除悬停。

`orbitPivotPicking: true` 在旋转手势按下时，用指针附近的九条射线采样可见模型网格；`orbitPivotRadiusPx` 指定 CSS 像素半径（默认 6）。每条射线仅取最近表面，命中点的算术中心作为本次固定旋转中心，不采样网格、坐标轴或操作手柄。未命中时由 `getOrbitFallbackCenter()` 返回业务对象的世界坐标中心；未声明回调的通用视口使用可见场景中心。无对象则保留当前观察中心。

`captureOrbitPivot(event)` 供共享视图导航控件在按下时捕获中心；省略事件则采样画布中央。`setOrbitCameraState(start, angles, pivot)` 将相机位置和观察目标共同绕此中心旋转，因此按下不重新取景，旋转保持该点的屏幕位置和相机距离。中心在松开、取消或失焦后释放，不在移动中重新检测；不开启该选项的视口继续绕原观察目标旋转。相机交互不修改工程数据。
