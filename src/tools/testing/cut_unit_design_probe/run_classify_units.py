"""Run every Classify input and prepare portable per-file inspection data."""
import argparse
from concurrent.futures import ThreadPoolExecutor,as_completed
from collections import Counter
import csv,hashlib,json,shutil,subprocess,sys,time
from pathlib import Path
from run_face_combinations import ROOT,OUT,ENV,physical,BITS,NAMES,probe

sys.stdout.reconfigure(encoding='utf-8')
DEST=OUT.parent/'classify-cut-units'
EXE=OUT.parent/'build/Release/batch_cut_units.exe'

def read(path):return json.loads(path.read_text(encoding='utf-8'))
def write(path,obj):path.write_text(json.dumps(obj,ensure_ascii=False,separators=(',',':')),encoding='utf-8')

def controls(data,path):
    lines=[str(len(data['faces']))]
    for f in data['faces']:
        groups=[(w['wireId'],g) for w in f['wires'] for g in w['groups'] if not g['seam'] and not g['event']]
        roles={g['kind'] for _,g in groups};mask=sum(BITS.get(k,0) for k in roles);complete=bool(groups) and 'Unresolved' not in roles
        lines.append(f"{f['faceId']} {int(complete)} {mask} {len(groups)}")
        for wire,g in groups:
            lines.append(f"{wire} {BITS.get(g['kind'],0)} {int(g['closed'])} {int(g['straight'])} {len(g['sourceUses'])}")
            lines.extend(f"{e['edgeId']} {int(e['reversed'])}" for e in g['sourceUses'])
    path.write_text('\n'.join(lines)+'\n',encoding='utf-8')

def reduced_polyline(points):
    # Source grouping joins are retained, while the drawing uses fewer samples per source edge.
    if len(points)<=20:return points
    ids=set(range(0,len(points),6));ids.update(range(0,len(points),49));ids.add(len(points)-1)
    return [[round(v,8) for v in points[i]] for i in sorted(ids)]

def package_case(item,folder,data):
    native=read(folder/'native.json');byid={f['faceId']:f for f in data['faces']};records=[]
    for f in native['faces']:
        src=byid[f['faceId']];f['groups']=[]
        for w in src['wires']:
            for g in w['groups']:
                if g['seam'] or g['event']:continue
                f['groups'].append({'wire':w['wireId'],'id':g['groupId'],'role':g['kind'],'closed':g['closed'],'straight':g['straight'],
                    'reason':g['reason'],'sourceEdges':[e['edgeId'] for e in g['sourceUses']],'points':reduced_polyline(g['polyline'])})
        roles=Counter(g['role'] for g in f['groups']);f['roles']=dict(roles);mask=sum(BITS.get(k,0) for k in roles);f['combination']=NAMES.get(mask,'未分类')
        f['wireCount']=len(src['wires']);f['boundaryComplete']='Unresolved' not in roles
        for unit in f['units']:
            unit['geometry']=read(folder/unit['mesh']);unit['geometry']['edges']=[reduced_polyline(p) for p in unit['geometry']['edges']]
        if f.get('pendingMesh'):f['geometry']=read(folder/f['pendingMesh']);f['geometry']['edges']=[reduced_polyline(p) for p in f['geometry']['edges']]
        records.append({'caseId':item['id'],'file':data['file'],'faceId':f['faceId'],'surface':f['surface'],'combination':f['combination'],'status':f['status'],
            'unitCount':len(f['units']),'reason':f['reason'],'areaMm2':f['sourceAreaMm2'],'sampledErrorMm':max((u['sampledErrorMm'] for u in f['units']),default=f['bestSampledErrorMm'])})
    native['sourceFile']=data['file'];native['caseId']=item['id'];native['profile']=[reduced_polyline(p) for p in data['profile']]
    payload=json.dumps(native,ensure_ascii=False,separators=(',',':'));(folder/'view.js').write_text('window.receiveCase('+payload+');\n',encoding='utf-8')
    return records,native

def prune_obsolete_outputs(folder,native):
    # This output folder belongs to this snapshot. Retire only superseded generated face files.
    assert folder.resolve().is_relative_to(DEST.resolve())
    keep=set()
    for f in native['faces']:
        for u in f['units']:keep.update([u['file'],u['mesh']])
        keep.update(f[k] for k in ['pendingBrep','pendingMesh'] if f.get(k))
    for sub in ['units','pending']:
        for path in (folder/sub).glob('F*'):
            if path.is_file() and (path.name.endswith('.brep') or path.name.endswith('.mesh.json')) and path.relative_to(folder).as_posix() not in keep:path.unlink()

def run(item,reuse,tolerance):
    folder=DEST/'cases'/f"{item['id']:03d}";folder.mkdir(parents=True,exist_ok=True);source=Path(item['source']);sha=hashlib.sha256(source.read_bytes()).hexdigest()
    assert sha==item['sha256Before'],f"Source changed: {source}"
    data=read(Path(item['evidence']));control=folder/'controls.txt';controls(data,control);start=time.perf_counter()
    result={'id':item['id'],'file':data['file'],'source':str(source),'originalSource':item.get('originalSource',str(source)),'sourceSha256':sha,'folder':str(folder),'fitToleranceMm':tolerance}
    try:
        if not(reuse and (folder/'native.json').exists() and (folder/'colored.step').exists()):
            with (folder/'native.log').open('wb') as log:
                p=subprocess.run([str(EXE),str(source),str(control),str(folder),str(tolerance)],env=ENV,stdout=log,stderr=log,timeout=480)
            if p.returncode:raise RuntimeError(f"native exit {p.returncode}")
        rows,native=package_case(item,folder,data)
        prune_obsolete_outputs(folder,native)
        assert len(rows)==len(data['faces']) and sum(r['unitCount'] for r in rows)==native['unitCount']
        assert hashlib.sha256(source.read_bytes()).hexdigest()==sha
        result.update(status='processed',faces=native['faceCount'],units=native['unitCount'],exactFaces=sum(r['status']=='exact' for r in rows),
            approximateFaces=sum(r['status']=='approximate' for r in rows),pendingFaces=native['pendingFaces'],splitFaces=sum(r['unitCount']>1 for r in rows),seconds=round(time.perf_counter()-start,2))
        return result,rows
    except Exception as e:
        result.update(status='failed',error=str(e),seconds=round(time.perf_counter()-start,2));return result,[]

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--only',type=int,nargs='+');ap.add_argument('--reuse',action='store_true');ap.add_argument('--prepare',action='store_true');ap.add_argument('--workers',type=int,default=4);ap.add_argument('--tolerance',type=float,default=.01);args=ap.parse_args()
    DEST.mkdir(parents=True,exist_ok=True)
    if args.prepare:
        (DEST/'inputs').mkdir(exist_ok=True);(DEST/'boundaries').mkdir(exist_ok=True)
        def prepare(pair):
            number,source=pair;snapshot=DEST/'inputs'/f'{number:03d}.step'
            raw=source.read_bytes();sha=hashlib.sha256(raw).hexdigest();snapshot.write_bytes(raw)
            assert hashlib.sha256(source.read_bytes()).hexdigest()==sha,f'Source was modified while taking snapshot: {source}'
            result,data=probe(snapshot,DEST/'boundaries'/f'{number:03d}.json');assert data,result
            data['file']=source.name;write(Path(result['evidence']),data);result.update(id=number,originalSource=str(source),file=source.name)
            print(f"boundary {number:03d} faces={len(data['faces'])} {source.name}",flush=True);return result
        sources=sorted((ROOT/'TestData/Tube/Classify').glob('*.step'),key=lambda p:p.name)
        with ThreadPoolExecutor(max_workers=args.workers) as pool:prepared=list(pool.map(prepare,enumerate(sources,1)))
        write(DEST/'source-manifest.json',prepared);print(f'Fresh boundary inventory: {len(prepared)} files',flush=True);return
    items=read(DEST/'source-manifest.json')
    if args.only:items=[r for r in items if r['id'] in args.only]
    results=[];rows=[]
    with ThreadPoolExecutor(max_workers=args.workers) as pool:
        for f in as_completed([pool.submit(run,item,args.reuse,args.tolerance) for item in items]):
            result,face_rows=f.result();results.append(result);rows.extend(face_rows)
            print(json.dumps(result,ensure_ascii=False),flush=True)
            write(DEST/'manifest.json',sorted(results,key=lambda r:r['id']))
    rows.sort(key=lambda r:(r['caseId'],r['faceId']));write(DEST/'faces.json',rows)
    if rows:
        with (DEST/'faces.csv').open('w',encoding='utf-8-sig',newline='') as stream:
            w=csv.DictWriter(stream,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
    summary={'files':len(results),'processedFiles':sum(r['status']=='processed' for r in results),'faces':len(rows),'statuses':dict(Counter(r['status'] for r in rows)),
        'unitCount':sum(r['unitCount'] for r in rows),'splitFaces':sum(r['unitCount']>1 for r in rows),'pendingReasons':dict(Counter(r['reason'] for r in rows if r['status']=='pending')),
        'fitToleranceMm':args.tolerance,'failures':[r for r in results if r['status']!='processed']}
    write(DEST/'summary.json',summary);print(json.dumps(summary,ensure_ascii=False,indent=2))
    (DEST/'index-data.js').write_text('window.CASE_INDEX='+json.dumps(sorted(results,key=lambda r:r['id']),ensure_ascii=False)+';\nwindow.RUN_SUMMARY='+json.dumps(summary,ensure_ascii=False)+';\n',encoding='utf-8')
    if summary['failures']:raise SystemExit(1)

if __name__=='__main__':main()
