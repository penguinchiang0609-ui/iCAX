"""Read-only original-geometry cavity diagnosis for saved native nesting outputs.

This research helper builds pair NFPs from constrained triangle Minkowski sums.
It nodes their boundaries with true parallel-edge contact lines, then validates
sampled insertion poses against the original polygon material. Reported blocker
minima are observed candidate minima, not a global continuous optimality proof.
"""
from pathlib import Path
import argparse, hashlib, importlib.util, json, math, sys, time

ROOT=Path(__file__).resolve().parents[5]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--summary',required=True)
    parser.add_argument('--output',required=True)
    parser.add_argument('--packages',required=True)
    parser.add_argument('--cases',default='albano,dagli,fu,jakobs1,shapes2,jakobs2,shirts,trousers')
    parser.add_argument('--max-verify',type=int,default=8000)
    parser.add_argument('--candidate-cap',type=int,default=60000)
    args=parser.parse_args()
    sys.path.insert(0,str(Path(args.packages).resolve()))
    import numpy as np
    import shapely
    from shapely.geometry import Polygon,LineString,Point,box
    from shapely.affinity import translate
    from shapely.strtree import STRtree
    from PIL import Image,ImageDraw,ImageFont
    spec=importlib.util.spec_from_file_location('evidence',Path(__file__).with_name('run_public_beam.py'))
    ev=importlib.util.module_from_spec(spec);spec.loader.exec_module(ev)
    summary_path=Path(args.summary).resolve();summary=json.loads(summary_path.read_text(encoding='utf8'))
    output=Path(args.output).resolve();output.mkdir(parents=True,exist_ok=True)
    if (output/'summary.json').exists():raise ValueError('Refusing to overwrite completed diagnosis')
    selected_cases=set(args.cases.split(','));all_rows=[]

    def decompose(poly):
        triangles=list(shapely.constrained_delaunay_triangles(poly).geoms)
        if abs(sum(t.area for t in triangles)-poly.area)>max(1.,poly.area)*1e-10:
            raise ValueError('Constrained decomposition changed source material')
        return np.asarray([list(t.exterior.coords)[:3] for t in triangles],dtype=float)

    def append_coords(target,g):
        if not g.is_empty:
            target.extend(shapely.get_coordinates(g).tolist())

    def render(name,trial,missing,choices,path):
        problem=trial['problem'];W,H=problem['stocks'][0]['width'],problem['stocks'][0]['height']
        scale=min(1100/W,760/H);head=115;pad=30
        im=Image.new('RGB',(int(W*scale+2*pad),int(H*scale+head+90)),'white');draw=ImageDraw.Draw(im)
        font=lambda n:ImageFont.truetype('C:/Windows/Fonts/msyh.ttc',n)
        draw.text((pad,12),f'{name} · 待插入 {missing["part"]} · 已观察最少阻挡 {choices["blockerCount"]} 件',font=font(23),fill='#243746')
        draw.text((pad,47),'橙色虚拟件保持原轮廓；红色为发生材料冲突的原件。此图是诊断候选，尚未构成完整排样。',font=font(16),fill='#774330')
        px=lambda x,y:(pad+x*scale,head+(H-y)*scale)
        draw.rectangle((pad,head,pad+W*scale,head+H*scale),fill='#f4f5f7',outline='#243746',width=2)
        blockers=set(choices['blockerIndices'])
        for i,pose in enumerate(trial['result']['layouts'][0]['placements']):
            poly=ev.geometry(next(p for p in problem['parts'] if p['id']==pose['part']),pose)
            draw.polygon([px(x,y) for x,y in poly.exterior.coords],fill='#ce7582' if i in blockers else '#b5c7d3',outline='#526979',width=1)
            if i in blockers:
                c=poly.representative_point();draw.text(px(c.x,c.y),f'{pose["part"]}:{pose["instance"]}',font=font(13),fill='#641f2b',anchor='mm')
        part=next(p for p in problem['parts'] if p['id']==missing['part']);g=ev.geometry(part,choices['pose'])
        coords=[px(x,y) for x,y in g.exterior.coords];draw.line(coords,fill='#e07916',width=5)
        draw.text((pad,head+H*scale+16),f'角度 {choices["pose"]["angle"]:g}°；原始材料交叠面积 {choices["overlapArea"]:.7g}',font=font(17),fill='#774330')
        im.save(path)

    for row in summary['rows']:
        name=row['case'];trial=row['selected'];check=trial['audit']
        if name not in selected_cases or check['complete']:continue
        started=time.perf_counter();problem=trial['problem'];result=trial['result'];tol=problem['tolerance']
        assert ev.audit(name,problem,result)['valid']
        W,H=problem['stocks'][0]['width'],problem['stocks'][0]['height'];assert len(result['layouts'])==1
        parts={p['id']:p for p in problem['parts']};poses=result['layouts'][0]['placements']
        placed=[ev.geometry(parts[p['part']],p) for p in poses]
        eroded=[p.buffer(-tol*.5,join_style=2) for p in placed];tree=STRtree(placed)
        rotated={};triangles={};nfp_cache={}
        def shape(identity,angle):
            key=(identity,angle%360)
            if key not in rotated:
                rotated[key]=ev.geometry(parts[identity],{'x':0,'y':0,'angle':angle})
                triangles[key]=decompose(rotated[key])
            return rotated[key]
        def pair_nfp(stationary,angle,moving,moving_angle):
            key=(stationary,angle%360,moving,moving_angle%360)
            if key not in nfp_cache:
                shape(stationary,angle);shape(moving,moving_angle)
                a=triangles[key[:2]];b=triangles[key[2:]]
                sums=(a[:,None,:,None,:]-b[None,:,None,:,:]).reshape(-1,9,2)
                hulls=shapely.convex_hull(shapely.multipoints(sums))
                nfp_cache[key]=shapely.union_all(hulls)
            return nfp_cache[key]

        missing_rows=[]
        for missing in result['remaining']:
            identity=missing['part'];part=parts[identity];source_poly=shape(identity,part['allowedAngles'][0])
            candidates=[];angle_metrics=[]
            for angle in part['allowedAngles']:
                moving=shape(identity,angle);b=moving.bounds
                lx,ly=-b[0],-b[1];rx,ty=W-b[2],H-b[3]
                if rx<lx-tol or ty<ly-tol:continue
                domain=box(lx,ly,max(lx,rx),max(ly,ty));boundaries=[domain.boundary];regions=[];raw=[]
                # Coalesce contact lines across every stationary original.
                lines={};moving_coords=list(moving.exterior.coords)[:-1]
                for i,pose in enumerate(poses):
                    nfp=translate(pair_nfp(pose['part'],pose['angle'],identity,angle),xoff=pose['x'],yoff=pose['y'])
                    regions.append(nfp)
                    clipped=nfp.boundary.intersection(domain)
                    if not clipped.is_empty:boundaries.append(clipped)
                    stationary_coords=list(placed[i].exterior.coords)[:-1]
                    for j,p in enumerate(stationary_coords):
                        q=stationary_coords[(j+1)%len(stationary_coords)]
                        ax,ay=q[0]-p[0],q[1]-p[1];al=math.hypot(ax,ay)
                        if not al:continue
                        dx,dy=ax/al,ay/al
                        if dx < -1e-12 or (abs(dx)<=1e-12 and dy<0):dx,dy=-dx,-dy
                        for k,r in enumerate(moving_coords):
                            s=moving_coords[(k+1)%len(moving_coords)]
                            bx,by=s[0]-r[0],s[1]-r[1];bl=math.hypot(bx,by)
                            if not bl or abs(ax*by-ay*bx)>1e-10*al*bl:continue
                            endpoints=[(p[0]-r[0],p[1]-r[1]),(p[0]-s[0],p[1]-s[1]),
                                       (q[0]-r[0],q[1]-r[1]),(q[0]-s[0],q[1]-s[1])]
                            lo=min(x*dx+y*dy for x,y in endpoints);hi=max(x*dx+y*dy for x,y in endpoints)
                            offset=-dy*endpoints[0][0]+dx*endpoints[0][1]
                            key=(round(dx*1e9),round(dy*1e9),round(offset/max(tol*.1,1e-9)))
                            line=lines.setdefault(key,[dx,dy,offset,[]]);line[3].append((lo,hi))
                contact_count=0
                for dx,dy,offset,intervals in lines.values():
                    intervals.sort();merged=[]
                    for lo,hi in intervals:
                        if merged and lo<=merged[-1][1]+tol*.1:merged[-1][1]=max(hi,merged[-1][1])
                        else:merged.append([lo,hi])
                    for lo,hi in merged:
                        line=LineString([(dx*lo-dy*offset,dy*lo+dx*offset),(dx*hi-dy*offset,dy*hi+dx*offset)]).intersection(domain)
                        if not line.is_empty:boundaries.append(line);contact_count+=1
                network=shapely.union_all(boundaries);append_coords(raw,network)
                # Node midpoints preserve narrow feasible intervals as well as vertices.
                for g in shapely.get_parts(network):
                    if g.geom_type=='LineString':
                        coords=list(g.coords)
                        for a,c in zip(coords,coords[1:]):raw.append([(a[0]+c[0])*.5,(a[1]+c[1])*.5])
                free=domain.difference(shapely.union_all(regions))
                for region in shapely.get_parts(free):
                    if region.area>0:
                        c=region.representative_point();raw.append([c.x,c.y])
                unit=max(tol*.25,1e-8);coords=np.asarray(raw,dtype=float)
                if len(coords)==0:continue
                valid=(coords[:,0]>=lx-tol)&(coords[:,0]<=rx+tol)&(coords[:,1]>=ly-tol)&(coords[:,1]<=ty+tol)
                coords=coords[valid];rounded=np.round(coords/unit).astype(np.int64)
                _,indices=np.unique(rounded,axis=0,return_index=True);coords=coords[np.sort(indices)]
                total_candidates=len(coords);truncated=total_candidates>args.candidate_cap
                if truncated:coords=coords[np.linspace(0,total_candidates-1,args.candidate_cap,dtype=int)]
                preliminary=np.zeros(len(coords),dtype=np.int16)
                for region in regions:
                    preliminary+=shapely.contains_xy(region.buffer(-tol*.4,join_style=2),coords[:,0],coords[:,1])
                order=np.lexsort((coords[:,1],coords[:,0],preliminary))
                # Examine all lowest-depth candidates first. Higher apparent
                # NFP depths may still contain original touch-only channels.
                examined=order[:args.max_verify]
                for index in examined:
                    x,y=coords[index];pose={'x':float(x),'y':float(y),'angle':angle}
                    poly=translate(moving,xoff=x,yoff=y);inside=poly.bounds
                    if inside[0]<-tol or inside[1]<-tol or inside[2]>W+tol or inside[3]>H+tol:continue
                    nearby=tree.query(poly);inner=poly.buffer(-tol*.5,join_style=2)
                    blockers=[];overlap=0.
                    for obstacle in nearby:
                        obstacle=int(obstacle)
                        area=inner.intersection(eroded[obstacle]).area
                        if area>tol*tol:
                            blockers.append(obstacle);overlap+=poly.intersection(placed[obstacle]).area
                    blockers.sort()
                    candidates.append({'pose':pose,'blockerCount':len(blockers),'blockerIndices':blockers,
                        'blockers':[{'part':poses[i]['part'],'instance':poses[i]['instance'],'index':i,'pose':poses[i],
                                     'materialArea':placed[i].area} for i in blockers],
                        'overlapArea':overlap,'nfpInteriorCount':int(preliminary[index])})
                angle_metrics.append({'angle':angle,'width':b[2]-b[0],'height':b[3]-b[1],
                    'positiveFreeTranslationArea':free.area,'contactLines':contact_count,
                    'networkCandidates':total_candidates,'retainedCandidates':len(coords),'verifiedCandidates':len(examined),
                    'candidatesTruncated':truncated or len(examined)<len(coords)})
            candidates.sort(key=lambda c:(c['blockerCount'],c['overlapArea'],
                sum(b['materialArea'] for b in c['blockers']),c['pose']['x'],c['pose']['y']))
            distinct=[];seen=set()
            for c in candidates:
                key=(tuple(c['blockerIndices']),c['pose']['angle'])
                if key not in seen:seen.add(key);distinct.append(c)
                if len(distinct)>=32:break
            item={'part':identity,'quantity':missing['quantity'],'materialAreaEach':source_poly.area,
                'allowedAngles':part['allowedAngles'],'angleMetrics':angle_metrics,
                'observedMinimumBlockers':distinct[0]['blockerCount'] if distinct else None,
                'recommendedCandidates':distinct,'unprovenGlobalMinimum':True}
            missing_rows.append(item)
            if distinct:
                image=f'{name}-{identity}-best-blockers.png';render(name,trial,missing,distinct[0],output/image);item['image']=image
        report={'case':name,'originalProblem':problem,'selectedSource':trial['source'],'sourceRun':trial['run'],'sourceStage':trial['stage'],
            'sourceSha256':ev.digest(trial['source']),'originalAudit':ev.audit(name,problem,result),
            'stockArea':W*H,'placedMaterialArea':sum(p.area for p in placed),
            'emptyMaterialAreaBeforeInsertion':W*H-sum(p.area for p in placed),
            'completeDemandMaterialArea':sum(ev.geometry(p,{'x':0,'y':0,'angle':0}).area*p['quantity'] for p in problem['parts']),
            'missing':missing_rows,'wallSeconds':time.perf_counter()-started,'nfpPairsBuilt':len(nfp_cache),
            'notes':['Blocker counts are observed over explicitly recorded original-geometry candidates; continuous global minimality is not claimed.',
                     'Orange diagnosis shapes can overlap blockers and are not feasible complete nesting outputs.',
                     'The current board/placements, original contours and allowed orientations were not changed.']}
        (output/(name+'.json')).write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
        print(name,'minimum blockers',[(m['part'],m['observedMinimumBlockers']) for m in missing_rows],
            'seconds',round(report['wallSeconds'],3),'NFPpairs',len(nfp_cache),flush=True)
        all_rows.append(report)
    (output/'summary.json').write_text(json.dumps({'cases':all_rows,'inputSummary':str(summary_path),
        'inputSummarySha256':ev.digest(summary_path),'scriptSha256':ev.digest(__file__)},ensure_ascii=False,indent=2),encoding='utf8')
    sections=[]
    for r in all_rows:
        for m in r['missing']:
            if 'image' not in m:continue
            sections.append(f'<section><h2>{r["case"]} / {m["part"]}</h2><p>待排 {m["quantity"]} 件，每件材料面积 {m["materialAreaEach"]:.7g}；当前空白材料面积 {r["emptyMaterialAreaBeforeInsertion"]:.7g}。已观察最少阻挡 {m["observedMinimumBlockers"]} 件，未声称连续全局最小。</p><img src="{m["image"]}"><p><a href="{r["case"]}.json">原件、落点、阻挡身份和所有角度候选</a></p></section>')
    (output/'index.html').write_text('<!doctype html><html lang="zh-CN"><meta charset="utf-8"><title>缺件阻挡与释放候选诊断</title><style>body{font:16px/1.6 "Microsoft YaHei",sans-serif;max-width:1300px;margin:auto;padding:24px;background:#eef3f6}section{background:white;padding:20px;margin:24px 0}img{max-width:100%}</style><h1>缺件阻挡与释放候选诊断</h1><p>图中橙色件为诊断用虚拟插入件，会与红色原件重叠。这些图用于选择需要释放的邻居，还不是完整排样结果。</p>'+''.join(sections),encoding='utf8')
    (output/'manifest.json').write_text(json.dumps({str(p.relative_to(output)):ev.digest(p) for p in output.iterdir() if p.is_file() and p.name!='manifest.json'},indent=2),encoding='utf8')


if __name__=='__main__':main()
