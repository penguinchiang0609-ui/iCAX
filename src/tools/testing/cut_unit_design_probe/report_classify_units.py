"""Write the review index from completed generation and validation data."""
from run_classify_units import DEST,read
s=read(DEST/'summary.json');v=read(DEST/'validation.json')
assert s['processedFiles']==s['files']==v['filesAudited'] and not s['failures'] and not v['filesWithIssues']
names={'sweep-interval-not-single':'母线多区间或有效采样不足','sweep-clip-failed':'扫掠区域裁剪失败','approximation-over-tolerance':'近似采样误差超过 0.01 mm','partition-area-mismatch':'分割面积未通过核对','boundary-unresolved':'逻辑边组的皮上／侧边／内部关系未决','no-supported-ruling-direction':'未找到当前支持的直母线方向','kernel: Geom_TrimmedCurve::parameters out of range':'裁剪曲线参数越界','zero-area':'源面面积为零','unit-export: mesh-failed':'单元无法三角化，保留原面 BREP'}
lines=['# Classify 全量切割单元检查','',
    '[打开 F2 逻辑边复核页面](http://127.0.0.1:8767/#case=4&face=2) · [F2 逐处判断](f2-logical-proof/README.md) · [逐面清单](faces.csv) · [统计](summary.json) · [回读核验](validation.json)','',
    '**本轮按逻辑边规则复核的目标仅为 001-E-33-3#1.step 的 F2：44 条源边 → 12 条直线＋4 条圆弧 → 1 个无侧边单元，16 条皮上切割边，新增面内分界 0 条。直线与相切圆弧不合并，圆弧自身碎段合并。其余全库旧几何分片没有因此获得切割单元语义验收。**','',
    f"本轮以当前 Classify 文件建立独立副本，处理 {s['files']} 个文件、{s['faces']:,} 张原面，生成 {s['unitCount']:,} 个切割单元候选；{s['splitFaces']} 张原面拆成多个单元。所有原文件及本轮副本的哈希均通过核对。原面编号对应本轮副本的 OCCT face 遍历顺序。",'',
    f"精确曲面候选 {s['statuses']['exact']:,} 面；近似候选 {s['statuses']['approximate']:,} 面；待处理 {s['statuses']['pending']:,} 面。待处理面没有伪造单元；保留原始 BREP 和具体原因。",'',
    '使用：在左侧搜索或窄窗口顶部下拉框选择文件。点选画布或右侧面列表查看原面 F 编号、单元 U 编号、边界组合、环数、面积、采样偏差与原因。可只看选中面，也可逐个单元单独检查。颜色表示实际区域，灰色表示待处理。原面边界角色与采样母线默认隐藏，可分别开启。母线不代表拆分边界或真实加工刀路。', '',
    '每个文件目录中的 `colored.step` 是带名称和颜色的全件输出；`units/Fxxxx_Uxxx.brep` 为独立单元；`pending/Fxxxx.brep` 为未处理原面；`native.json` 保留结果与采样，`view.js` 为查看数据；`inputs` 保存本轮完整源模型，`boundaries` 保留逻辑边组证据。BREP 本身不携带颜色。', '',
    '可直接检查的例子：','',
    '- [001-E-22-2#1：F3 为完整单个环带，修正原来的四块分割](http://127.0.0.1:8767/#case=3&face=3) · [彩色 STEP](cases/003/colored.step)',
    '- [01-Profil：F5 → 3 个单元](http://127.0.0.1:8767/#case=18&face=5) · [彩色 STEP](cases/018/colored.step)',
    '- [057：F10 为近似候选](http://127.0.0.1:8767/#case=72&face=10)；[F1 因偏差过大待处理](http://127.0.0.1:8767/#case=72&face=1)',
    '- [H93 左吸能盒：F86 三角化失败](http://127.0.0.1:8767/#case=178&face=86) · [原面 BREP](cases/178/pending/F0086.brep)', '',
    '| 待处理原因 | 面数 |','|---|---:|']
for key,count in sorted(s['pendingReasons'].items(),key=lambda p:-p[1]):lines.append(f'| {names.get(key,key)} | {count} |')
lines+=['',f"核验覆盖全部 {v['filesAudited']} 个文件：原文件与副本哈希、原面遍历覆盖、BREP 文件与网格索引、近似阈值、彩色 STEP 回读面数、逐单元名称、颜色、BREP 有效性和面积均通过。最大 STEP 回读单元面积差 {max(r.get('maxStepAreaDeltaMm2',0) for r in v['results']):.9g} mm²，验收阈值 max(1e-6 mm², 单元面积×1e-7)。归一化 RGB 回读容差为 1e-4，最大差 {max(r.get('maxStepColorChannelDelta',0) for r in v['results']):.9g}。",'',
    '边界位置／皮上容差 0.001 mm，G1 分组切向容差 0.001 rad，截面材料域容差 1e-7 mm。Side 按完整逻辑边组的端点接皮、其余在材料域内定义，不强制直线。', '',
    '当前是几何检查候选：精确曲面表示保留原支撑曲面及分区，区间判定仍用有限采样；近似只保证本轮采样偏差不超过 0.01 mm，不构成连续误差上界。未完成生产级连续覆盖证书、摆放与可达性、半切透工艺、刀路连接、NC 或机床验收。', '',
    '浏览器实查覆盖：三单元原面隔离查看、画布点选 F/U 对应、近似候选放大、待处理原因显示。完整原生 CAD 查看器逐件人工验收尚未进行。', '',
    '本地服务关闭后，可从此目录运行 Python `http.server` 并打开页面；`index.html` 使用模块资源，需要通过本地 HTTP 访问。']
(DEST/'README.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
print(DEST/'README.md')
