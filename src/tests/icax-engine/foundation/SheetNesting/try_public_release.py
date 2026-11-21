"""Independent bounded geometry experiment for blocker-guided local releases.

Only new research outputs are written. A successful result still needs native
validation before being treated as a production solver result.
"""
from pathlib import Path
import argparse, copy, importlib.util, itertools, json, math, sys, time

ROOT=Path(__file__).resolve().parents[5]


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--summary',required=True);ap.add_argument('--diagnosis',required=True)
    ap.add_argument('--output',required=True);ap.add_argument('--packages',required=True)
    ap.add_argument('--cases',default='albano,dagli,fu,jakobs1,shapes2')
    ap.add_argument('--seconds',type=float,default=25.);ap.add_argument('--beam',type=int,default=12)
    args=ap.parse_args();sys.path.insert(0,str(Path(args.packages).resolve()))
    import numpy as np
    import shapely
    from shapely.geometry import box,LineString
    from shapely.affinity import translate
    from shapely.strtree import STRtree
    spec=importlib.util.spec_from_file_location('evidence',Path(__file__).with_name('run_public_beam.py'))
    ev=importlib.util.module_from_spec(spec);spec.loader.exec_module(ev)
    summary=json.loads(Path(args.summary).read_text(encoding='utf8'));diagnosis=Path(args.diagnosis).resolve()
    output=Path(args.output).resolve();output.mkdir(parents=True,exist_ok=True)
    if (output/'summary.json').exists():raise ValueError('Refusing to overwrite completed experiment')
    rows=[]
    for row in summary['rows']:
        name=row['case'];trial=row['selected']
        if name not in args.cases.split(',') or trial['audit']['complete']:continue
        started=time.perf_counter();deadline=started+args.seconds
        p=trial['problem'];r=trial['result'];parts={v['id']:v for v in p['parts']};W,H=p['stocks'][0]['width'],p['stocks'][0]['height'];tol=p['tolerance']
        poses=r['layouts'][0]['placements'];polys=[ev.geometry(parts[x['part']],x) for x in poses]
        info=json.loads((diagnosis/(name+'.json')).read_text(encoding='utf8'))
        rotations={};triangles={};pairs={};directions={};queries=0;nodes=0;attempts=[];best=None
        def expired():return time.perf_counter()>=deadline
        def shape(identity,angle):
            key=(identity,angle)
            if key not in rotations:
                g=ev.geometry(parts[identity],{'x':0,'y':0,'angle':angle});rotations[key]=g
                triangles[key]=np.asarray([list(t.exterior.coords)[:3] for t in shapely.constrained_delaunay_triangles(g).geoms])
            return rotations[key]
        for identity,part in parts.items():
            unique=[];keys=[]
            for angle in part['allowedAngles']:
                g=shape(identity,angle);b=g.bounds;normalized=shapely.normalize(translate(g,xoff=-b[0],yoff=-b[1]))
                if not any(normalized.equals_exact(k,tol*.1) for k in keys):keys.append(normalized);unique.append(angle)
            directions[identity]=unique
        def pair(a,aa,b,ba):
            key=(a,aa,b,ba)
            if key not in pairs:
                shape(a,aa);shape(b,ba);x=triangles[(a,aa)];y=triangles[(b,ba)]
                sums=(x[:,None,:,None,:]-y[None,:,None,:,:]).reshape(-1,9,2)
                pairs[key]=shapely.union_all(shapely.convex_hull(shapely.multipoints(sums)))
            return pairs[key]
        def find(item,fixed,end,full=False):
            nonlocal queries
            queries+=1;fits=[];original=item
            actual=[ev.geometry(parts[x['part']],x) for x in fixed];eroded=[g.buffer(-tol*.5,join_style=2) for g in actual];tree=STRtree(actual)
            for angle in directions[item['part']]:
                if time.perf_counter()>=end:return fits
                moving=shape(item['part'],angle);b=moving.bounds;lx,ly=-b[0],-b[1];rx,ty=W-b[2],H-b[3]
                if rx<lx-tol or ty<ly-tol:continue
                domain=box(lx,ly,max(lx,rx),max(ly,ty));regions=[];boundaries=[domain.boundary]
                for x in fixed:
                    if time.perf_counter()>=end:return fits
                    region=translate(pair(x['part'],x['angle'],item['part'],angle),xoff=x['x'],yoff=x['y']);regions.append(region)
                    if full:
                        clipped=region.boundary.intersection(domain)
                        if not clipped.is_empty:boundaries.append(clipped)
                free=domain.difference(shapely.union_all(regions));raw=shapely.get_coordinates(free).tolist()
                for g in shapely.get_parts(free):
                    if g.area>0:
                        q=g.representative_point();raw.append([q.x,q.y])
                # Source-parallel contact lines retain zero-width channels.
                if full:
                    moving_coords=list(moving.exterior.coords)[:-1];lines={}
                    for stationary in actual:
                        coords=list(stationary.exterior.coords)[:-1]
                        for j,a in enumerate(coords):
                            c=coords[(j+1)%len(coords)];ax,ay=c[0]-a[0],c[1]-a[1];al=math.hypot(ax,ay)
                            if not al:continue
                            dx,dy=ax/al,ay/al
                            if dx < -1e-12 or (abs(dx)<=1e-12 and dy<0):dx,dy=-dx,-dy
                            for k,bp in enumerate(moving_coords):
                                d=moving_coords[(k+1)%len(moving_coords)];bx,by=d[0]-bp[0],d[1]-bp[1];bl=math.hypot(bx,by)
                                if not bl or abs(ax*by-ay*bx)>1e-10*al*bl:continue
                                ends=[(a[0]-bp[0],a[1]-bp[1]),(a[0]-d[0],a[1]-d[1]),(c[0]-bp[0],c[1]-bp[1]),(c[0]-d[0],c[1]-d[1])]
                                lo=min(x*dx+y*dy for x,y in ends);hi=max(x*dx+y*dy for x,y in ends);offset=-dy*ends[0][0]+dx*ends[0][1]
                                key=(round(dx*1e8),round(dy*1e8),round(offset/max(tol*.1,1e-9)))
                                rec=lines.setdefault(key,[dx,dy,offset,[]]);rec[3].append((lo,hi))
                    for dx,dy,offset,intervals in lines.values():
                        intervals.sort();merged=[]
                        for lo,hi in intervals:
                            if merged and lo<=merged[-1][1]+tol*.1:merged[-1][1]=max(hi,merged[-1][1])
                            else:merged.append([lo,hi])
                        for lo,hi in merged:
                            line=LineString([(dx*lo-dy*offset,dy*lo+dx*offset),(dx*hi-dy*offset,dy*hi+dx*offset)]).intersection(domain)
                            if not line.is_empty:boundaries.append(line)
                    network=shapely.union_all(boundaries);raw+=shapely.get_coordinates(network).tolist()
                raw.extend([[lx,ly],[lx,ty],[rx,ly],[rx,ty],[original['x'],original['y']]])
                seen=set()
                raw.sort(key=lambda q:math.hypot(q[0]-original['x'],q[1]-original['y']))
                for x,y in raw:
                    if time.perf_counter()>=end:return fits
                    if x<lx-tol or y<ly-tol or x>rx+tol or y>ty+tol:continue
                    key=(round(x/(tol*.25)),round(y/(tol*.25)))
                    if key in seen:continue
                    seen.add(key);g=translate(moving,xoff=x,yoff=y);inner=g.buffer(-tol*.5,join_style=2)
                    if any(inner.intersection(eroded[int(i)]).area>tol*tol for i in tree.query(g)):continue
                    pose=copy.deepcopy(item);pose.update(x=float(x),y=float(y),angle=angle,group=0)
                    fits.append(pose)
                    if len(fits)>=args.beam*2:return fits
            return fits
        plans=[]
        for missing in info['missing']:
            candidates=missing['recommendedCandidates']
            # A same-spec full overlap merely swaps original identities.
            useful=[c for c in candidates if not (c['blockerCount']==1 and c['blockers'][0]['part']==missing['part'] and c['overlapArea']>.95*missing['materialAreaEach'])]
            for c in useful[:12]:
                if c['blockerCount']>3:continue
                used={v['instance'] for v in poses if v['part']==missing['part']};instance=next(i for i in range(parts[missing['part']]['quantity']) if i not in used)
                inserted={'part':missing['part'],'instance':instance,**c['pose'],'group':0,'gapFiller':False}
                original_group=set(c['blockerIndices']);near=[]
                for j,g in enumerate(polys):
                    if j in original_group:continue
                    distance=min(g.distance(polys[k]) for k in original_group) if original_group else 0.
                    near.append((distance,g.area,j))
                near.sort()
                variants=[original_group]
                if len(original_group)<3:
                    variants.extend(original_group|{j} for _,_,j in near[:4])
                if len(original_group)<2:
                    variants.extend(original_group|{a[2],b[2]} for a,b in itertools.combinations(near[:3],2))
                for group in variants:plans.append((inserted,sorted(group),c['overlapArea']))
        # Every target/blocker family gets execution opportunity before a wider
        # retry. Storage-order or symmetric-angle duplicates do not consume it.
        seen=set();unique=[]
        for inserted,group,overlap in plans:
            key=(inserted['part'],tuple(group),round(inserted['x']/tol),round(inserted['y']/tol),inserted['angle'])
            if key not in seen:seen.add(key);unique.append((inserted,group,overlap))
        plans=sorted(unique,key=lambda v:(len(v[1]),v[2]))
        # First individually test the actually conflicting originals, then the
        # contact-neighbour expansions. A greedy return is just an experiment.
        for full in (False,True):
            if best or expired():break
            for index,(inserted,group,overlap) in enumerate(plans):
                if best or expired():break
                remaining=max(1,len(plans)-index);quota=min(1.5,max(.08,(deadline-time.perf_counter())/remaining))
                end=min(deadline,time.perf_counter()+quota)
                fixed=[copy.deepcopy(x) for j,x in enumerate(poses) if j not in group];fixed.append(inserted)
                pending=[copy.deepcopy(poses[j]) for j in group]
                beam=[(fixed,pending,0.)];best_restored=0;visited=0
                while beam and time.perf_counter()<end:
                    next_nodes=[]
                    for board,queue,cost in beam:
                        if time.perf_counter()>=end:break
                        if not queue:best=board;break
                        choices=[]
                        for item in queue:
                            fits=find(item,board,end,full)
                            choices.append((len(fits),-shape(item['part'],item['angle']).area,item,fits))
                            if not fits:break
                        if not choices or choices[-1][0]==0:continue
                        _,_,item,fits=min(choices,key=lambda x:(x[0],x[1]));visited+=1;nodes+=1
                        for pose in fits[:args.beam]:
                            rest=[x for x in queue if (x['part'],x['instance'])!=(item['part'],item['instance'])]
                            movement=math.hypot(pose['x']-item['x'],pose['y']-item['y'])
                            next_nodes.append((board+[pose],rest,cost+movement));best_restored=max(best_restored,len(group)-len(rest))
                    if best:break
                    next_nodes.sort(key=lambda x:(len(x[1]),x[2]));beam=next_nodes[:args.beam]
                attempts.append({'inserted':inserted,'releaseIndices':group,'releasedOriginals':[poses[j] for j in group],
                    'fullContacts':full,'bestRestoredCount':best_restored,'neededRestoredCount':len(group),
                    'nodes':visited,'success':best is not None,'quotaSeconds':quota})
                if best:break
        candidate=None
        if best:
            candidate=copy.deepcopy(r);candidate['layouts'][0]['placements']=best
            counts={identity:sum(x['part']==identity for x in best) for identity in parts}
            candidate['remaining']=[{'part':identity,'quantity':part['quantity']-counts[identity]} for identity,part in parts.items() if counts[identity]<part['quantity']]
            candidate['placed']=len(best);candidate['complete']=not candidate['remaining'];candidate['state']=0 if candidate['complete'] else 1
            candidate['remnantMeasured']=False;candidate['regularRemnant']=0;candidate['remnantRects']=[]
            gs=[ev.geometry(parts[x['part']],x) for x in best]
            candidate['right']=max(g.bounds[2] for g in gs);candidate['top']=max(g.bounds[3] for g in gs)
            audit=ev.audit(name,p,candidate);assert audit['valid'],audit
            data={'case':name,'researchOnly':True,'nativeNotValidated':True,'secondsPerConstructor':0,'secondsPerPostprocessor':args.seconds,
                'problem':p,'runs':[{'constructor':'research.blocker-release','workAxis':'geometry','constructorWallSeconds':0,
                'postWallSeconds':time.perf_counter()-started,'accepted':True,'before':r,'candidate':candidate,'selected':candidate}]}
            (output/(name+'-candidate.json')).write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf8')
        report={'case':name,'wallSeconds':time.perf_counter()-started,'improved':best is not None,'nativeNotValidated':True,
            'beforePlaced':r['placed'],'afterPlaced':candidate['placed'] if candidate else r['placed'],
            'complete':candidate['complete'] if candidate else False,'queries':queries,'nodes':nodes,'nfpPairs':len(pairs),'attempts':attempts}
        (output/(name+'-experiment.json')).write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8');rows.append(report)
        print(name,'improved',report['improved'],'count',report['beforePlaced'],'->',report['afterPlaced'],
              'attempts',len(attempts),'queries',queries,'seconds',round(report['wallSeconds'],3),flush=True)
    (output/'summary.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf8')


if __name__=='__main__':main()
