"""Rebuild one selected face by its classified logical boundary, with per-join reasons."""
import argparse,hashlib,html,json,math,shutil,subprocess
from pathlib import Path
import run_classify_units as r

def norm(v):return math.sqrt(sum(x*x for x in v))
def sub(a,b):return [x-y for x,y in zip(a,b)]
def cross(a,b):return [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]]
def primitive(g):
    if g['straight']:return {'kind':'line','name':'直线'}
    pts=g['polyline'];a,b,c=pts[0],pts[len(pts)//2],pts[-1];v,w=sub(b,a),sub(c,a);n=cross(v,w);denom=2*norm(n)**2
    if denom<=1e-20:return {'kind':'curve','name':'曲线'}
    v2=norm(v)**2;w2=norm(w)**2;p=cross(w,n);q=cross(n,v);center=[a[i]+(v2*p[i]+w2*q[i])/denom for i in range(3)];radius=norm(sub(a,center));error=max(abs(norm(sub(point,center))-radius) for point in pts)
    return {'kind':'arc' if error<=.001 else 'curve','name':'圆弧' if error<=.001 else '曲线','center':center,'radiusMm':radius,'maxSampledCircleErrorMm':error}
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--case',type=int,required=True);ap.add_argument('--face',type=int,required=True);a=ap.parse_args()
    item=next(i for i in r.read(r.DEST/'source-manifest.json') if i['id']==a.case)
    folder=r.DEST/'cases'/f'{a.case:03d}';proof=r.DEST/f'f{a.face}-logical-proof';proof.mkdir(exist_ok=True)
    data=r.read(r.DEST/'boundaries'/f'{a.case:03d}.json');source=Path(item['source']);sha=hashlib.sha256(source.read_bytes()).hexdigest();assert sha==item['sha256Before']
    result,fresh=r.probe(source,proof/'fresh-boundaries.json',a.face);assert fresh,result
    original=next(f for f in data['faces'] if f['faceId']==a.face);freshface=next(f for f in fresh['faces'] if f['faceId']==a.face)
    signature=lambda f:[(w['wireId'],sorted((e['edgeId'],e['reversed']) for g in w['groups'] for e in g['sourceUses'])) for w in f['wires']]
    assert signature(original)==signature(freshface),'Source edge uses changed'
    data['faces']=[freshface if f['faceId']==a.face else f for f in data['faces']];data['groupingReviewFaces']=[a.face];original=freshface
    r.write(r.DEST/'boundaries'/f'{a.case:03d}.json',data)
    control=folder/'controls.txt';r.controls(data,control)
    with (proof/'native-check.log').open('wb') as log:
        subprocess.run([str(r.EXE.parent/'logical_boundary_tests.exe'),str(source),str(control),str(a.face),str(proof)],env=r.ENV,stdout=log,stderr=log,check=True)
    generated=proof/'generated'
    with (proof/'generate.log').open('wb') as log:
        subprocess.run([str(r.EXE),str(source),str(control),str(generated),'.01',str(a.face)],env=r.ENV,stdout=log,stderr=log,check=True)
    reviewed=r.read(generated/'native.json')['faces'][0];assert reviewed['faceId']==a.face and len(reviewed['units'])==1 and reviewed['units'][0]['method']=='logical-skin-loop-planar'
    assert len(original['wires'])==1
    groups=[g for g in original['wires'][0]['groups'] if not g['seam'] and not g['event']];assert all(g['kind']=='Skin' for g in groups)
    uses=[e['edgeId'] for g in groups for e in g['sourceUses']];assert len(uses)==len(set(uses))==original['wires'][0]['rawEdgeUses']
    joins=[]
    for i,g in enumerate(groups):
        nxt=groups[(i+1)%len(groups)];v=g['endTangent'];w=nxt['startTangent'];angle=math.degrees(math.acos(max(-1,min(1,sum(x*y for x,y in zip(v,w))/norm(v)/norm(w)))))
        gap=norm(sub(g['end'],nxt['start']));assert gap<=.001
        changed=primitive(g)['kind']!=primitive(nxt)['kind']
        geometric_reason='直线与圆弧／曲线形态切换，相切仍分为两个逻辑边' if changed else '切向不连续的转角处分为两个逻辑边'
        joins.append({'fromGroup':g['groupId'],'toGroup':nxt['groupId'],'gapMm':gap,'turnAngleDeg':angle,'splitRegion':False,
            'groupBreakReason':geometric_reason,'reason':geometric_reason+'；两侧都是皮上切割边，没有侧边、内部终止边或内环要求在面内新增分界。'})
    analysis={'rule':'single-planar-skin-boundary-loop','logicalGroupCount':len(groups),'rawEdgeUses':len(uses),'cutEdgeCount':len(groups),'sideEdgeCount':0,'internalEdgeCount':0,'newSplitCount':0,
        'cutEdges':[{'wire':1,'group':g['groupId'],'role':'Skin','geometry':primitive(g),'sourceUses':g['sourceUses']} for g in groups],
        'sideEdges':[],'joins':joins,'conclusion':'一个原始平面、一个闭合皮上边界：保留为一个无侧边切割单元；逻辑边转角不自动切开面。',
        'placementState':'未选择入射方向、母线配对或刀路；这些留待摆放阶段。','evidence':f'f{a.face}-logical-proof/README.md','diagram':f'f{a.face}-logical-proof/logical-groups.svg'}
    reviewed['boundaryAnalysis']=analysis;reviewed['units'][0]['cutEdges']=analysis['cutEdges'];reviewed['units'][0]['sideEdges']=[];reviewed['units'][0]['sweepState']='deferred-until-placement'
    native=r.read(folder/'native.json');old=next(f for f in native['faces'] if f['faceId']==a.face)
    if not (proof/'previous-native-face.json').exists():r.write(proof/'previous-native-face.json',old)
    native['faces']=[reviewed if f['faceId']==a.face else f for f in native['faces']]
    native.update(unitCount=sum(len(f['units']) for f in native['faces']),acceptedFaces=sum(bool(f['units']) for f in native['faces']),pendingFaces=sum(not f['units'] for f in native['faces']))
    for unit in reviewed['units']:
        for key in ['file','mesh']:shutil.copy2(generated/unit[key],folder/unit[key])
    assembly=[]
    for f in native['faces']:
        for u in f['units']:
            name=f"F{f['faceId']:04d}_U{u['id']:03d}_"+('APPROX' if f['status']=='approximate' else 'EXACT')
            assembly.append((folder/u['file'],name,u['color']))
        if not f['units']:assembly.append((folder/f['pendingBrep'],f"F{f['faceId']:04d}_PENDING",[.62,.65,.69]))
    assembly_file=proof/'assembly.txt';assembly_file.write_text(str(len(assembly))+'\n'+'\n'.join(json.dumps(str(p.resolve().as_posix()),ensure_ascii=False)+' '+json.dumps(name)+' '+' '.join(str(v) for v in color) for p,name,color in assembly)+'\n',encoding='utf-8')
    with (proof/'assemble.log').open('wb') as log:subprocess.run([str(r.EXE),'--assemble',str(assembly_file),str(folder/'colored.step')],env=r.ENV,stdout=log,stderr=log,check=True)
    r.write(folder/'native.json',native);r.package_case(item,folder,data);r.prune_obsolete_outputs(folder,native)
    assert hashlib.sha256(source.read_bytes()).hexdigest()==sha
    assert hashlib.sha256(Path(item['originalSource']).read_bytes()).hexdigest()==sha
    r.write(proof/'boundary-analysis.json',analysis)
    # Diagram projects the actual original edge polylines; no invented seams.
    allpoints=[p for g in groups for p in g['polyline']];spans=[max(p[i] for p in allpoints)-min(p[i] for p in allpoints) for i in range(3)];axes=sorted(range(3),key=lambda i:spans[i],reverse=True)[:2];axes.sort()
    lo=[min(p[i] for p in allpoints) for i in axes];hi=[max(p[i] for p in allpoints) for i in axes];scale=500/max(hi[i]-lo[i] for i in range(2));center=[(lo[i]+hi[i])/2 for i in range(2)]
    def xy(p):return (450+(p[axes[0]]-center[0])*scale,360-(p[axes[1]]-center[1])*scale)
    chain=[p for g in groups for p in g['polyline']];path='M '+' L '.join(f'{x:.5f},{y:.5f}' for x,y in map(xy,chain))+' Z'
    svg=['<svg xmlns="http://www.w3.org/2000/svg" width="900" height="780" viewBox="0 0 900 780"><rect width="900" height="780" fill="#f7f9fc"/><g font-family="Microsoft YaHei, sans-serif">',f'<text x="40" y="42" font-size="25" fill="#18334c">原面 F{a.face}：1 个无侧边单元 · {len(groups)} 条皮上切割边</text>',f'<path d="{path}" fill="#d2e9f7" stroke="#14829f" stroke-width="3"/>']
    for g in groups:
        points=g['polyline'];lengths=[0.]
        for p,q in zip(points,points[1:]):lengths.append(lengths[-1]+norm(sub(q,p)))
        anchor=points[next(i for i,x in enumerate(lengths) if x>=lengths[-1]/2)];x,y=xy(anchor);dx,dy=x-450,y-360;size=max(math.hypot(dx,dy),1);x+=dx/size*28;y+=dy/size*28
        svg.append(f'<text x="{x:.3f}" y="{y:.3f}" text-anchor="middle" dominant-baseline="middle" font-size="22" font-weight="bold" fill="#17364b">G{g["groupId"]}</text>')
    svg+=[f'<text x="40" y="685" font-size="20" fill="#18334c">{len(uses)} 条原始 edge → {len(groups)} 个逻辑边组 → 1 个切割单元；新增分界线 0 条。</text>','<text x="40" y="725" font-size="18" fill="#53677c">直线与相切圆弧分为两个逻辑边；G 编号不表示把面拆开。</text></g></svg>']
    (proof/'logical-groups.svg').write_text('\n'.join(svg),encoding='utf-8')
    lines=[f'# {data["file"]} · F{a.face} 逻辑边复核','',f'结论：**1 个无侧边切割单元，{len(groups)} 条皮上切割边，新增分界 0 条。** 原来的 9 个 UV 分片撤回。', '', '[查看模型](http://127.0.0.1:8767/#case='+str(a.case)+'&face='+str(a.face)+') · [逻辑边标号图](logical-groups.svg) · [单元 BREP](generated/'+reviewed['units'][0]['file']+') · [彩色单元 STEP](generated/colored.step)','',
        '依据：先按几何种类和 G0/G1 分组，直线与相切圆弧仍分成两个逻辑边，同一直线／圆弧自身的碎段才合并。原面为平面，只有一个闭合外环；所有完整逻辑边都在截面皮上，优先归皮上边。没有侧边、内部终止边或内孔。允许无侧边、保留多条切割边的单元，因此逻辑边组分开不等于切开面。没有指定 UV 方向，也未根据水平、竖直或视觉上的翼缘／腹板强行拆面。','',
        '| 逻辑组 | 原始 Edge | 形状 | 角色／用途 |','|---|---|---|---|']
    for g in groups:lines.append(f'| G{g["groupId"]} | '+', '.join(str(e['edgeId']) for e in g['sourceUses'])+' | '+primitive(g)['name']+' | 皮上／切割边 |')
    lines+=['','每处逻辑边界的决定：','','| 边组交界 | 转角 | 端点间隙 mm | 是否切开面／原因 |','|---|---:|---:|---|']
    for j in joins:lines.append(f'| G{j["fromGroup"]} → G{j["toGroup"]} | {j["turnAngleDeg"]:.6f}° | {j["gapMm"]:.9g} | 不拆；{j["reason"]} |')
    validation=r.read(proof/'logical-validation.json');lines+=['',f'几何核对：面积 {validation["areaMm2"]:.12g} mm²；单元减原面、原面减单元的布尔差面积均为 0；BREP 回读有效。四种平面 UV 旋转及一次世界坐标刚体旋转均得到同一单元结论；侧边、内部边、边界未决、带孔区域不进入该规则。','',
        '**范围：本次只按此规则重做选中的 F'+str(a.face)+'。同文件其他面及全库旧分片没有在本次获得逻辑边验收。**','',analysis['placementState']+'本轮没有宣称一个固定母线方向能够无事件地连续扫完整张工字形面，也不以此替代后续刀路可行性检查。']
    (proof/'README.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    print(json.dumps({'case':a.case,'face':a.face,'groups':len(groups),'rawEdges':len(uses),'units':1,'newSplitCount':0,'sourceUnchanged':True,'proof':str(proof)},ensure_ascii=False))
if __name__=='__main__':main()
