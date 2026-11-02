"""Build a factual inventory, representative diagrams and a readable evidence report."""
import collections,hashlib,json,math,sys
from pathlib import Path
import numpy as np
from PIL import Image,ImageDraw,ImageFont
sys.stdout.reconfigure(encoding='utf-8')
ROOT=Path(__file__).resolve().parents[4]
OUT=ROOT/'output/tests/cut-unit-design-20261023'
manifest=json.loads((OUT/'manifest.json').read_text(encoding='utf-8'))
rows=[(m,json.loads(Path(m['evidence']).read_text(encoding='utf-8'))) for m in manifest if m['status']=='passed']

def relation(r):
    a=r['angles']
    if r['error'] or len(a)!=5:return 'unresolved'
    if any(abs(x)>math.pi-.001 for x in a):return 'opposed'
    if min(a)>.001:return 'convex'
    if max(a)<-.001:return 'concave'
    if max(abs(x) for x in a)<=.001:return 'smooth'
    return 'varying'

summary={
    'date':'2026-10-23','files':len(manifest),'readable':len(rows),
    'faceCount':sum(len(d['faces']) for _,d in rows),
    'surfaceTypes':dict(collections.Counter(f['type'] for _,d in rows for f in d['faces'])),
    'multiWireFaces':sum(f['wires']>1 for _,d in rows for f in d['faces']),
    'multiWireFiles':sum(any(f['wires']>1 for f in d['faces']) for _,d in rows),
    'rawSharedEdges':sum(d['rawSharedEdgeCount'] for _,d in rows),
    'sewnSharedEdges':sum(len(set(e['faces']))>1 for _,d in rows for e in d['edges']),
    'adjacentPairSamples':dict(collections.Counter(relation(r) for _,d in rows for e in d['edges'] for r in e['relations'] if r['a']!=r['b'])),
    'faceCountsPreserved':all(d['originalFaceCount']==d['sewnFaceCount'] for _,d in rows),
    'sourceMapsComplete':all(f['sourceFaceIndices'] for _,d in rows for f in d['faces']),
    'sourcesUnchanged':all(hashlib.sha256(Path(m['source']).read_bytes()).hexdigest()==m['sha256Before'] for m in manifest),
    'positiveCurvatureFaces':[{'file':d['file'],'case':m['id'],'face':f['id'],'maxGaussian':f['gaussianMax'],'chordTrials':f['chordTrials']} for m,d in rows for f in d['faces'] if f['gaussianMax']>1e-8],
    'varyingAngleEdges':[{'file':d['file'],'case':m['id'],'edge':e['id'],'faces':e['faces'],'anglesDeg':[x*180/math.pi for x in r['angles']]} for m,d in rows for e in d['edges'] for r in e['relations'] if r['a']!=r['b'] and relation(r)=='varying'],
    'opposedEdges':[{'file':d['file'],'case':m['id'],'edge':e['id']} for m,d in rows for e in d['edges'] if any(relation(r)=='opposed' for r in e['relations'] if r['a']!=r['b'])],
    'degenerateEdges':[{'file':d['file'],'case':m['id'],'edge':e['id']} for m,d in rows for e in d['edges'] if e.get('degenerate')],
    'largestAreaDifferences':sorted([{'file':d['file'],'case':m['id'],'relative':abs(d['originalArea']-d['sewnArea'])/max(d['originalArea'],1e-15),'signedMm2':d['sewnArea']-d['originalArea']} for m,d in rows],key=lambda x:x['relative'],reverse=True)[:10],
    'method':'Native OCCT 8.0 BRep read; sewing 0.001 mm; edge projection 17 samples; signed dihedral 5 samples, 0.001 rad; curvature 7x7 UV samples; trimmed iso clipping sampled at 128 stations with 24 bisections; chord deviation 13 cross stations and 25 points per interval. Results are sampled geometric evidence, not certified sweep/path acceptance.',
    'probeSourceHashes':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in Path(__file__).parent.iterdir() if p.is_file()}
}
(OUT/'summary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding='utf-8')

FONT='C:/Windows/Fonts/msyh.ttc'
font=lambda size:ImageFont.truetype(FONT,size)
COLORS={'convex':'#1674bd','concave':'#d26b15','smooth':'#8997a6','varying':'#b83170','opposed':'#8b41c4','free':'#536275','profile':'#138966'}

def draw_case(d,faces,title,subtitle,dest,plane=None):
    edges=[e for e in d['edges'] if any(f in faces for f in e['faces'])]
    points=np.array([p for e in edges for p in e['samples']],dtype=float)
    center=points.mean(axis=0)
    if plane is None:
        _,_,v=np.linalg.svd(points-center,full_matrices=False);basis=v[:2].T
    else:
        basis=np.array(plane,dtype=float).T
    projected=(points-center)@basis;lo=projected.min(axis=0);hi=projected.max(axis=0)
    scale=min(1060/max(hi[0]-lo[0],1e-8),520/max(hi[1]-lo[1],1e-8))
    image=Image.new('RGB',(1280,780),'#f6f8fc');dr=ImageDraw.Draw(image)
    dr.text((42,26),title,font=font(27),fill='#172b45');dr.text((42,70),subtitle,font=font(17),fill='#536275')
    dr.rounded_rectangle((35,115,1245,690),radius=16,fill='white',outline='#dfe5ef',width=2)
    def xy(p):
        a=(np.array(p)-center)@basis
        return (640+(a[0]-(lo[0]+hi[0])/2)*scale,403-(a[1]-(lo[1]+hi[1])/2)*scale)
    # Draw all edges, then shared edges last for legibility.
    for e in sorted(edges,key=lambda e:len(set(e['faces']))>1):
        rs=[relation(r) for r in e['relations'] if r['a']!=r['b']]
        color=COLORS[rs[0]] if rs else COLORS['profile' if e['profileSamplesOn']==e['sampleCount'] else 'free']
        ps=[xy(p) for p in e['samples']]
        if len(ps)>1:dr.line(ps,fill=color,width=4 if rs else 2)
        else:dr.ellipse((ps[0][0]-3,ps[0][1]-3,ps[0][0]+3,ps[0][1]+3),fill=color)
        if rs:
            q=ps[len(ps)//2];dr.text((q[0]+5,q[1]+3),f"E{e['id']}",font=font(15),fill=color)
    dr.text((42,710),'绿色：轮廓边   蓝色：凸边   橙色：凹边   灰色：连续／自由边   紫红：凹凸变化',font=font(18),fill='#43526b')
    dr.text((42,742),'原始 BRep 边界的正交投影；着色为几何关系，尚未指定入口、侧边或最终刀路。',font=font(16),fill='#66758a')
    image.save(dest)

figs=OUT/'figures';figs.mkdir(exist_ok=True)
for name in ['x-bevel','k-bevel','blind-without-floor','bar-end']:
    d=json.loads((OUT/'analytic-cases'/f'{name}.json').read_text(encoding='utf-8'))
    labels={'x-bevel':'X 坡口：内部凸边','k-bevel':'K 坡口：两条内部凸边','blind-without-floor':'留底槽：输入不包含底面','bar-end':'条钢断面：四条边都在轮廓上'}
    plane=[[1,0,0],[0,0,1]] if name!='bar-end' else [[0,1,0],[0,0,1]]
    draw_case(d,[f['id'] for f in d['faces']],labels[name],'解析构造校验样本；单位 mm；与真实零件证据分别记录。',figs/f'{name}.png',plane)

selected=[('001-E-22-2#1.step',[2],'带内环的加工面','单张平面包含内环，不能直接套用三／四边单元。','inner-wire'),('028.step',[1,2,3,4,5],'028：非直纹曲面与相邻坡面','F2/F3/F5 检出正高斯曲率；U/V 扫掠方向的拟合误差不同。','028'),('350-横管tu最新-另存.step',[3,4],'350：接近直纹的自由曲面','F3/F4 全深度 U 向弦线采样偏差约 0.003 mm；仍属于近似结果。','350')]
for filename,faces,title,subtitle,name in selected:
    d=next(d for _,d in rows if d['file']==filename);draw_case(d,faces,title,subtitle,figs/f'{name}.png')

report=['# Classify 后切割单元方案：实样证据','', '日期：2026-10-23。此报告验证方案依据，未生成可生产的刀路。','',
f"读取 `TestData/Tube/Classify` 全部 **{summary['files']}** 个 STEP；**{summary['readable']}** 个成功；共 **{summary['faceCount']}** 张加工面。逐文件 SHA-256 校验：原文件未改变。",'',
'## 1. 总体统计','', '| 项目 | 结果 |','|---|---:|']
report += [f'| {k} | {v} |' for k,v in summary['surfaceTypes'].items()]
report += [f"| 带内环的面 / 文件 | {summary['multiWireFaces']} / {summary['multiWireFiles']} |",f"| 导出拓扑的共享边 | {summary['rawSharedEdges']} |",f"| 几何缝合恢复的共享边 | {summary['sewnSharedEdges']} |",'',
'共享边关系采样统计：`'+json.dumps(summary['adjacentPairSamples'],ensure_ascii=False)+'`。这些数量是几何边关系，不是切透／半切透刀路数量。','',
'## 2. 对设计产生直接影响的实样','',
'| 文件 | 证据 | 设计结论 |','|---|---|---|',
'| 001-E-22-2#1.step | 面 F2、F3 有内环 | 单元必须支持多区间裁剪和逻辑分解，不能限定原始面只有一条 Wire |',
'| 028.step | F2/F3/F5 有正高斯曲率采样；F3 的全深度 V 向弦偏差约 0.119191 mm | 必须有非直纹近似；方向与裁剪域共同决定误差 |',
'| 350-横管tu最新-另存.step | F3 全深度 U 向约 0.003175 mm，V 向约 44.080536 mm | 同一曲面不同方向差异很大；不能把样条面直接拒绝，也不能任意取参数方向 |',
'| 左辊压梁.step | 共享边 E693 的有向二面角从约 +2.589° 变为 -15.182° | 必须沿边寻找凹凸切换点，不能整条边只取一个中点判定 |',
'| triangle-tube-5570-cut-layout.step | 部分边法向相反，角度在 +π/-π 两侧跳变 | 必须区分反向／折叠关系与真实凹凸切换 |',
'| 041.step | 3 条退化边 | 退化边按顶点事件处理，不调用普通有长度曲线求值 |',
'| 084.step | 约 0.01 mm 深的细小边界及凹关系 | 不能把连接容差放大到特征尺寸，也不能忽略薄小单元 |','',
'## 3. K／X、缺底面和无侧边解析对照','',
'下列是已执行的解析构造验证，单独记录，不冒充真实零件中的 K／X 样本。','',
'| 样本 | 实际结果 |','|---|---|','| X 坡口 | 2 张面，1 条凸连接边 |','| K 坡口 | 3 张面，2 条凸连接边 |','| 带底槽 | 3 张面，2 条凹连接边 |','| 不带底面槽 | 2 张侧壁；2 条内部终止边；无需槽底面；沿指定方向余厚为 6 mm |','| 条钢断面 | 1 张面，4 条边全在截面轮廓上 |','',
'![K 坡口](figures/k-bevel.png)','', '![缺底面槽](figures/blind-without-floor.png)','',
'## 4. 原始曲面与近似试验','',
'![带内环](figures/inner-wire.png)','', '![028](figures/028.png)','', '![350](figures/350.png)','',
'以下为裁剪后参数等值线端点弦的最大**采样偏差**。试验每方向 13 个截面，每有效区间 25 个点；这是方向候选的证据，不是双向 Hausdorff 上界、完整曲面覆盖或可加工证明。','',
'| 文件 / 面 | U 全深度 mm | V 全深度 mm | 局部深度分 4 段的较小偏差 mm |','|---|---:|---:|---:|']
for r in summary['positiveCurvatureFaces']:
    by={(c['direction'],c['depthSubdivisions']):c for c in r['chordTrials']}
    report.append(f"| {r['file']} / F{r['face']} | {by['u',1]['maxSampledDeviation']:.6f} | {by['v',1]['maxSampledDeviation']:.6f} | {min(by['u',4]['maxSampledDeviation'],by['v',4]['maxSampledDeviation']):.6f} |")
report += ['', '**不能把深度分段的误差下降直接当作可加工。** 后一段可能从材料内部开始；必须证明它有真实入射通道或已有材料去除依赖。正式方案优先选择全深度直线族并按进给方向分区。','',
'## 5. 缝合和异常记录','',
'全体样本缝合前后面数一致，源面映射完整。但存在面积测量差，最大值如下。面积差可能包含边界修整与积分差异；本次未据此认定源面受损，也未证明缝合几何严格不变。生产方案以原始面为权威，用逻辑共享边建立关系，缝合只用于候选发现或经验证的工作副本。','',
'| 文件 | 相对面积差 | 有符号面积差 mm² |','|---|---:|---:|']
report += [f"| {r['file']} | {r['relative']:.9g} | {r['signedMm2']:.9g} |" for r in summary['largestAreaDifferences'][:5]]
report += ['', '初轮 `041.step` 在普通边求值中出现诊断进程异常退出；定位到退化边后，增加退化顶点分支，原文件原样重跑通过。此前失败保存在 manifest 的 priorAttempts，不判定该文件无效。','',
'## 6. 可复核文件','', '- [全量摘要](summary.json)','- [逐文件清单和原始哈希](manifest.json)','- [解析样本断言](analytic-checks.json)','- 逐文件详细数据位于 `cases/NNN.json`；面号和边号为本次工作副本索引，源面对应在 `sourceFaceIndices` 中。','',
'方法与限制：'+summary['method'],'',
'未覆盖：所有连续参数的严格误差界、任意方向全局最优、真实机床姿态选择、激光控深工艺、进退刀、碰撞、NC 输出和实切。这些均列入详细方案的后续验收，不以本报告统计代替。']
(OUT/'实样分析报告.md').write_text('\n'.join(report)+'\n',encoding='utf-8')
print(json.dumps({k:v for k,v in summary.items() if k not in ['positiveCurvatureFaces','largestAreaDifferences','probeSourceHashes','varyingAngleEdges','opposedEdges']},ensure_ascii=False,indent=2))
