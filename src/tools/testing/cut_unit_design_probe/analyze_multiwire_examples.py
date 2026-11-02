"""Show sampled cross-sections splitting into multiple material intervals on real faces."""
import json
import sys
from pathlib import Path
import numpy as np
sys.stdout.reconfigure(encoding='utf-8')

ROOT=Path(__file__).resolve().parents[4]
OUT=ROOT/'output/tests/cut-unit-design-20261023/face-combinations'

results=[]
for name in ['skin-multiple-loops','three-roles-eight-holes']:
    path=OUT/'examples'/f'{name}.json';d=json.loads(path.read_text(encoding='utf-8'));f=d['faces'][0]
    loops=[]
    for w in f['wires']:
        points=np.array([p for g in w['groups'] for p in g['polyline']],float)
        keep=np.r_[True,np.linalg.norm(np.diff(points,axis=0),axis=1)>1e-8];points=points[keep]
        if np.linalg.norm(points[0]-points[-1])>1e-8:points=np.vstack([points,points[0]])
        loops.append(points)
    pts=np.vstack(loops);center=pts.mean(axis=0);_,_,v=np.linalg.svd(pts-center,full_matrices=False);basis=v[:2].T
    polygons=[(p-center)@basis for p in loops];q=np.vstack(polygons);lo=q.min(axis=0);hi=q.max(axis=0)
    best=[];odd=0
    for x in np.linspace(lo[0],hi[0],501)[1:-1]:
        hits=[]
        for poly in polygons:
            for a,b in zip(poly[:-1],poly[1:]):
                if (a[0]<=x<b[0]) or (b[0]<=x<a[0]):hits.append(a[1]+(b[1]-a[1])*(x-a[0])/(b[0]-a[0]))
        hits.sort()
        if len(hits)%2:odd+=1;continue
        intervals=[(a,b) for a,b in zip(hits[::2],hits[1::2]) if b-a>1e-7]
        if len(intervals)>len(best):best=intervals;station=float(x)
    segments=[]
    for a,b in best:segments.append([(center+basis@np.array([station,a])).tolist(),(center+basis@np.array([station,b])).tolist()])
    plan={'displaySegments':segments,'splitLines':[],'note':'These are disjoint valid intervals at one station, not a final decomposition.'}
    path.with_suffix('.plan.json').write_text(json.dumps(plan,ensure_ascii=False,indent=2),encoding='utf-8')
    results.append({'file':d['file'],'faceId':f['faceId'],'wireCount':len(loops),'testedStations':499,'oddIntersectionStations':odd,'maxIntervalsAtOneStation':len(best),'exampleIntervals3d':segments,'method':'PCA plane, polyline boundary intersections; diagnostic only'})
(OUT/'multiwire-checks.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps([{k:v for k,v in r.items() if k!='exampleIntervals3d'} for r in results],ensure_ascii=False,indent=2))
