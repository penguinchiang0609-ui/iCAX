"""Regenerate only files with complete planar two-loop faces, preserving the full run index."""
from concurrent.futures import ThreadPoolExecutor,as_completed
import json
import run_classify_units as r

items=r.read(r.DEST/'source-manifest.json');ids=[];candidates=[]
for item in items:
    data=r.read(r.DEST/'boundaries'/f"{item['id']:03d}.json")
    faces=[f for f in data['faces'] if f['surface']=='plane' and len(f['wires'])==2 and all(g['kind']!='Unresolved' for w in f['wires'] for g in w['groups'] if not g['seam'] and not g['event'])]
    if faces:ids.append(item['id']);candidates.extend((item['id'],f['faceId']) for f in faces)
print(json.dumps({'files':ids,'candidateFaces':len(candidates)}),flush=True)
results=[]
with ThreadPoolExecutor(max_workers=4) as pool:
    for task in as_completed([pool.submit(r.run,item,False,.01) for item in items if item['id'] in ids]):
        result,_=task.result();results.append(result);print(json.dumps(result,ensure_ascii=False),flush=True)
r.write(r.DEST/'radial-refresh.json',results)
assert all(row['status']=='processed' for row in results)
