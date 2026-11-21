"""Fresh original-input construction, paired compaction, independent GEOS audit and plots.

The target widths preserve prior feasible-run comparison dimensions. They are
not a list of proved optima and the wide controls are not public-target passes.
"""
from pathlib import Path
import argparse,concurrent.futures,hashlib,html,json,math,subprocess,sys,time

ROOT=Path(__file__).resolve().parents[5]
TARGETS={'shapes0':62.,'albano':10381.591826489177,'mao':1856.5312532230084,'dighe2':100.,
    'dagli':59.32515212981744,'dighe1':100.20741924741924,'fu':31.571428571428573,
    'jakobs1':11.633333333333333,'jakobs2':24.2,'marques':79.04355400696869,
    'shapes1':56.5,'shapes2':26.1,'shirts':65.22513749651905,'swim':6368.356450919711,'trousers':248.52366199082581}

def digest(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--output',required=True);parser.add_argument('--build',required=True)
    parser.add_argument('--packages');parser.add_argument('--cases',default='shapes0,shapes1,fu,mao,shirts,shapes2')
    parser.add_argument('--strategies',default='rules.contact-large,rules.composite-units')
    parser.add_argument('--seconds',type=float,default=1);parser.add_argument('--post-seconds',type=float,default=4)
    parser.add_argument('--workers',type=int,default=3);parser.add_argument('--refill',action='store_true');parser.add_argument('--rotated',action='store_true')
    parser.add_argument('--wide-controls',action='store_true');args=parser.parse_args()
    if args.packages:sys.path.insert(0,str(Path(args.packages).resolve()))
    from shapely.geometry import Polygon,box
    from PIL import Image,ImageDraw,ImageFont
    source=ROOT/'TestData/Plane/Nesting/umetani-isp';output=Path(args.output).resolve();output.mkdir(parents=True,exist_ok=True)
    exe=Path(args.build).resolve()/'Release/SheetNestingPublicBench.exe';assert exe.is_file()
    tracked=[exe,Path(__file__).resolve(),ROOT/'src/iCAX-Engine/foundation/SheetNesting/NestingNfpCompaction.cpp',
        ROOT/'src/iCAX-Engine/foundation/SheetNesting/SheetNestingCompaction.h',
        ROOT/'src/iCAX-Engine/foundation/SheetNesting/NestingNfp.cpp',
        ROOT/'src/iCAX-Engine/foundation/SheetNesting/NestingDomainRepair.cpp',
        ROOT/'src/tests/icax-engine/foundation/SheetNesting/SheetNestingPublicBench.cpp']
    hashes={str(p.relative_to(ROOT)):digest(p) for p in tracked}
    (output/'input-sources.json').write_text(json.dumps({'tracked':hashes,'originalCorpus':json.loads((source/'source-manifest.json').read_text()),
        'targetWidthMeaning':'Historical feasible-run widths, not proved optimal strip lengths. Wide controls separately labelled.'},indent=2),encoding='utf8')
    def geometry(part,pose):
        angle=pose['angle']%360
        if angle==0:c,s=1.,0.
        elif angle==90:c,s=0.,1.
        elif angle==180:c,s=-1.,0.
        elif angle==270:c,s=0.,-1.
        else:c,s=math.cos(math.radians(angle)),math.sin(math.radians(angle))
        return Polygon([(c*x-s*y+pose['x'],s*x+c*y+pose['y']) for x,y in part['geometry']['outer']])
    def audit(p,result):
        parts={part['id']:part for part in p['parts']};used={key:set() for key in parts};errors=[];warnings=[];tol=p['tolerance'];world=[]
        for layout in result['layouts']:
            if layout['stock']!='sheet' or layout['instance']!=0:errors.append('Unexpected stock identity')
            if layout['width']!=p['stocks'][0]['width'] or layout['height']!=p['stocks'][0]['height']:errors.append('Changed stock size')
            current=[]
            for pose in layout['placements']:
                part=parts[pose['part']]
                if pose['instance'] in used[part['id']] or not 0<=pose['instance']<part['quantity']:errors.append('Duplicate/invalid original identity')
                used[part['id']].add(pose['instance'])
                if not any(abs((pose['angle']-a+180)%360-180)<=1e-8 for a in part['allowedAngles']):errors.append('Disallowed orientation')
                poly=geometry(part,pose);b=poly.bounds
                if not poly.is_valid:errors.append('Invalid original polygon')
                if b[0]<-tol or b[1]<-tol or b[2]>layout['width']+tol or b[3]>layout['height']+tol:errors.append('Outside original stock')
                for other in current:
                    overlap=poly.intersection(other).area
                    if overlap>0:
                        interior=poly.buffer(-tol*.5,join_style=2).intersection(other.buffer(-tol*.5,join_style=2)).area
                        if interior>tol*tol:errors.append(f'Material overlap {overlap:.12g}')
                        else:warnings.append({'nearContactArea':overlap})
                current.append(poly)
            world.append(current)
        remaining={row['part']:row['quantity'] for row in result['remaining']}
        for key,part in parts.items():
            if len(used[key])+remaining.get(key,0)!=part['quantity']:errors.append('Original quantity not conserved')
        for i,certificate in enumerate(result.get('remnantRects',[])):
            if certificate['verified'] and certificate['area']>0:
                q=box(certificate['x'],certificate['y'],certificate['x']+certificate['width'],certificate['y']+certificate['height'])
                if abs(q.area-certificate['area'])>tol*max(1,q.length):errors.append('Incorrect remnant rectangle area')
                if any(q.buffer(-tol*.5,join_style=2).intersection(g.buffer(-tol*.5,join_style=2)).area>tol*tol for g in world[i]):errors.append('Claimed remnant contains original material')
        placed=sum(len(v) for v in used.values());needed=sum(x['quantity'] for x in p['parts'])
        return {'valid':not errors,'complete':not errors and placed==needed,'placed':placed,'required':needed,
            'warnings':warnings,'errors':errors,'linearTolerance':tol}
    tasks=[]
    for name in args.cases.split(','):
        for frame in (['original','rot90'] if args.rotated else ['original']):tasks.append((name,TARGETS[name],frame,'target'))
    if args.wide_controls:
        for name,width in [('shapes0',80.),('fu',40.),('mao',2300.),('shirts',80.)]:tasks.append((name,width,'original','wide-control'))
    def execute(task):
        name,width,frame,kind=task;tag=f'{name}-{kind}-{frame}';path=output/(tag+'.json')
        if not path.exists():
            command=[str(exe),str(source/(name+'.dat')),format(width,'.17g'),str(path),str(args.seconds),'500000',args.strategies,str(args.post_seconds),frame]
            if args.refill:command.append('refill')
            process=subprocess.run(command,capture_output=True,text=True,timeout=600)
            (output/(tag+'.log')).write_text(process.stdout+'\n'+process.stderr,encoding='utf8')
            if process.returncode:raise RuntimeError(tag+': '+process.stderr)
        data=json.loads(path.read_text());rows=[]
        for i,trial in enumerate(data['runs']):
            checks={key:audit(data['problem'],trial[key]) for key in ('before','candidate','selected')}
            for key,check in checks.items():
                if trial[key]['valid'] and not check['valid']:raise RuntimeError(f'{tag} {key}: {check["errors"]}')
            row={'case':name,'kind':kind,'frame':frame,'index':i,'constructor':trial['constructor'],'axis':trial['workAxis'],
                'accepted':trial['accepted'],'beforePlaced':checks['before']['placed'],'afterPlaced':checks['selected']['placed'],
                'complete':checks['selected']['complete'],'rightBefore':trial['before']['right'],'rightAfter':trial['selected']['right'],
                'remnantBefore':trial['before']['regularRemnant'],'remnantAfter':trial['selected']['regularRemnant'],
                'postSeconds':trial['postWallSeconds'],'audit':checks,'source':path.name}
            rows.append(row)
        (output/(tag+'-audit.json')).write_text(json.dumps(rows,indent=2),encoding='utf8')
        print(tag,'valid',sum(r['audit']['selected']['valid'] for r in rows),'accepted',sum(r['accepted'] for r in rows),
            'best count',max(r['afterPlaced'] for r in rows),flush=True)
        return data,rows
    results=[]
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as pool:
        pending=[pool.submit(execute,t) for t in tasks]
        for future in concurrent.futures.as_completed(pending):results.append(future.result())
    rows=[row for data,group in results for row in group]
    font=lambda n:ImageFont.truetype('C:/Windows/Fonts/msyh.ttc',n)
    palette=['#75a7c4','#bc8499','#bb8b62','#7f7a9a','#a3b265','#77a29b','#b77eae','#89a4ab','#c9ae69','#9b976e']
    def render(data,trial,path):
        p=data['problem'];a,b=trial['before'],trial['selected'];W=p['stocks'][0]['width'];H=p['stocks'][0]['height'];scale=min(600/W,540/H)
        panel=660;height=int(H*scale+160);im=Image.new('RGB',(panel*2,height),'white');draw=ImageDraw.Draw(im);parts={v['id']:v for v in p['parts']}
        for side,r in enumerate((a,b)):
            ox=side*panel+25;oy=H*scale+95
            draw.text((ox,12),f'{"构造结果" if side==0 else "压紧后选择结果"}：{r["placed"]} / {sum(v["quantity"] for v in p["parts"])} 件',font=font(20),fill='#263a4b')
            draw.text((ox,44),f'右边界 {r["right"]:.5g} · 规整余料 {r["regularRemnant"]:.6g}',font=font(15),fill='#476072')
            pixel=lambda x,y:(ox+x*scale,oy-y*scale)
            draw.rectangle((ox,95,ox+W*scale,oy),fill='#f4f5f7',outline='#263a4b',width=2)
            for layout in r['layouts']:
                for pose in layout['placements']:
                    g=geometry(parts[pose['part']],pose);draw.polygon([pixel(x,y) for x,y in g.exterior.coords],fill=palette[int(pose['part'][1:])%len(palette)],outline='#425363',width=1)
            for cert in r.get('remnantRects',[]):
                if cert['verified'] and cert['area']>0:
                    x,y,w,h=(cert[k] for k in ('x','y','width','height'));draw.rectangle((*pixel(x,y+h),*pixel(x+w,y)),outline='#14804a',width=3)
            draw.text((ox,oy+15),f'原板 {W:.7g} × {H:g}；绿色框为已验证的规整余料',font=font(13),fill='#476072')
        im.save(path)
    sections=[]
    for data,group in sorted(results,key=lambda pair:pair[1][0]['source']):
        useful=[r for r in group if r['accepted']]
        if not useful:continue
        best=max(useful,key=lambda r:(r['afterPlaced']-r['beforePlaced'],r['complete'],r['remnantAfter']-r['remnantBefore']))
        trial=data['runs'][best['index']];image_name=best['source'].replace('.json','-comparison.png');render(data,trial,output/image_name)
        warnings=len(best['audit']['selected']['warnings'])
        sections.append(f'<section><h2>{html.escape(best["case"])} · {best["kind"]} · {best["frame"]}</h2><p>{html.escape(best["constructor"])}，压紧轴 {best["axis"]}；新增耗时 {best["postSeconds"]:.3f}s；独立核验容差内接触告警 {warnings} 条。</p><a href="{image_name}"><img src="{image_name}"></a><p><a href="{best["source"]}">全部原件、坐标、预算与诊断</a> · <a href="{best["source"].replace(".json","-audit.json")}">独立核验</a></p></section>')
    summary={'rows':rows,'independentValid':sum(r['audit']['selected']['valid'] for r in rows),'trials':len(rows),
        'accepted':sum(r['accepted'] for r in rows),'newlyCompleted':sum(r['complete'] and not r['audit']['before']['complete'] for r in rows),
        'notes':['Paired trials use exactly the same constructor output. Each axis is an independent postprocessing configuration.',
            'No current external-solver timing comparison. Wide controls do not count as completing the historical tight target.']}
    (output/'summary.json').write_text(json.dumps(summary,indent=2),encoding='utf8')
    table=''.join(f'<tr><td>{r["case"]}</td><td>{r["kind"]}</td><td>{r["constructor"]} / {r["axis"]}</td><td>{r["beforePlaced"]} → {r["afterPlaced"]}</td><td>{r["remnantBefore"]:.6g} → {r["remnantAfter"]:.6g}</td><td>{r["postSeconds"]:.3f}s</td></tr>' for r in rows if r['accepted'])
    (output/'index.html').write_text('<!doctype html><html lang="zh-CN"><meta charset="utf-8"><title>原件滑动、组合压紧与补入实测</title><style>body{font:16px/1.6 "Segoe UI","Microsoft YaHei",sans-serif;max-width:1400px;margin:auto;padding:24px;background:#f2f5f7;color:#243746}section{background:white;border:1px solid #dbe2e8;padding:20px;margin:24px 0}img{width:100%}table{border-collapse:collapse;width:100%;background:white}td,th{border:1px solid #ccd6df;padding:8px;text-align:left}a{color:#1261a2}</style><h1>原件滑动、组合压紧与补入实测</h1><p>每组前后使用完全相同的原件和构造布局。沿真实边线滑动，必要时移动相互卡住的小组合；补入模式仍使用原始需求。候选统一按原件合法性、件数、板数及规整余料比较。宽板控制组用于检查压紧能力，不算紧目标通过。</p>'+f'<p>{len(rows)} 个独立后处理配置；独立核验合法 {summary["independentValid"]}，接受 {summary["accepted"]}，新增完整 {summary["newlyCompleted"]} 个配置。</p>'+'<table><tr><th>案例</th><th>板材组</th><th>构造 / 压紧轴</th><th>件数</th><th>规整余料面积</th><th>新增耗时</th></tr>'+table+'</table>'+''.join(sections)+'<p><a href="summary.json">全部配置结果</a> · <a href="input-sources.json">原始来源与源码/程序校验</a></p></html>',encoding='utf8')
    assert hashes=={str(p.relative_to(ROOT)):digest(p) for p in tracked},'Source or executable changed during the paired run.'
    (output/'manifest.json').write_text(json.dumps({str(p.relative_to(output)):digest(p) for p in output.iterdir() if p.is_file() and p.name!='manifest.json'},indent=2),encoding='utf8')
    print(json.dumps({k:v for k,v in summary.items() if k!='rows'},indent=2),flush=True)

if __name__=='__main__':main()
