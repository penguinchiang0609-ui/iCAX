"""Refresh the complete index after focused reruns without rebuilding unchanged meshes."""
from collections import Counter
import csv,json
from run_classify_units import DEST,read,write,BITS,NAMES

sources=read(DEST/'source-manifest.json')
old_rows={(r['caseId'],r['faceId']):r for r in read(DEST/'faces.json')}
changed=set(read(DEST/'source-refresh.json')['ids']) if (DEST/'source-refresh.json').exists() else set()
rows=[];manifest=[]
for source in sources:
    case_id=source['id'];folder=DEST/'cases'/f'{case_id:03d}'
    native=read(folder/'native.json');assert native['faceCount']==len(native['faces'])
    boundaries=None
    for f in native['faces']:
        old=old_rows.get((case_id,f['faceId']))
        if old is None or case_id in changed:
            if boundaries is None:boundaries={b['faceId']:b for b in read(DEST/'boundaries'/f'{case_id:03d}.json')['faces']}
            groups=[g for w in boundaries[f['faceId']]['wires'] for g in w['groups'] if not g['seam'] and not g['event']]
            combination=NAMES.get(sum(BITS.get(k,0) for k in {g['kind'] for g in groups}),'未分类')
        else:combination=old['combination']
        rows.append({'caseId':case_id,'file':source['file'],'faceId':f['faceId'],'surface':f['surface'],'combination':combination,'status':f['status'],
            'unitCount':len(f['units']),'reason':f['reason'],'areaMm2':f['sourceAreaMm2'],'sampledErrorMm':max((u['sampledErrorMm'] for u in f['units']),default=f['bestSampledErrorMm'])})
    manifest.append({'id':case_id,'file':source['file'],'source':source['source'],'originalSource':source['originalSource'],'sourceSha256':source['sha256Before'],
        'folder':str(folder),'fitToleranceMm':.01,'status':'processed','faces':native['faceCount'],'units':native['unitCount'],
        'exactFaces':sum(f['status']=='exact' for f in native['faces']),'approximateFaces':sum(f['status']=='approximate' for f in native['faces']),
        'pendingFaces':native['pendingFaces'],'splitFaces':sum(len(f['units'])>1 for f in native['faces'])})
rows.sort(key=lambda r:(r['caseId'],r['faceId']))
summary={'files':len(manifest),'processedFiles':len(manifest),'faces':len(rows),'statuses':dict(Counter(r['status'] for r in rows)),
    'unitCount':sum(r['unitCount'] for r in rows),'splitFaces':sum(r['unitCount']>1 for r in rows),
    'pendingReasons':dict(Counter(r['reason'] for r in rows if r['status']=='pending')),'fitToleranceMm':.01,'failures':[]}
for name,data in [('manifest.json',manifest),('faces.json',rows),('summary.json',summary)]:write(DEST/name,data)
with (DEST/'faces.csv').open('w',encoding='utf-8-sig',newline='') as stream:
    writer=csv.DictWriter(stream,fieldnames=list(rows[0]));writer.writeheader();writer.writerows(rows)
(DEST/'index-data.js').write_text('window.CASE_INDEX='+json.dumps(manifest,ensure_ascii=False)+';\nwindow.RUN_SUMMARY='+json.dumps(summary,ensure_ascii=False)+';\n',encoding='utf-8')
print(json.dumps(summary,ensure_ascii=False,indent=2))
