"""Inventory the user's skin/side/internal definition on every original Classify face."""
import argparse
from collections import Counter,defaultdict
from concurrent.futures import ThreadPoolExecutor,as_completed
import csv
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

sys.stdout.reconfigure(encoding='utf-8')
ROOT=Path(__file__).resolve().parents[4]
BASE=ROOT/'output/tests/cut-unit-design-20261023'
OUT=BASE/'face-combinations'
EXE=BASE/'build/Release/face_boundary_probe.exe'
ENV=os.environ.copy();ENV['PATH']=str(ROOT/'src/third_party/opencascade/install/vs2022-x64/win64/vc14/bin')+os.pathsep+ENV['PATH']
BITS={'Skin':1,'Side':2,'Internal':4}
NAMES={1:'皮上',2:'侧边',3:'皮上+侧边',4:'内部',5:'皮上+内部',6:'侧边+内部',7:'皮上+侧边+内部'}

def probe(source,dest,face=None,timeout=180):
    before=hashlib.sha256(source.read_bytes()).hexdigest();start=time.perf_counter()
    command=[str(EXE),str(source),str(dest)]+([str(face)] if face else [])
    row={'source':str(source),'evidence':str(dest),'sha256Before':before}
    try:
        p=subprocess.run(command,env=ENV,capture_output=True,timeout=timeout)
        row.update(exitCode=p.returncode,seconds=round(time.perf_counter()-start,3),sourceUnchanged=before==hashlib.sha256(source.read_bytes()).hexdigest())
        if p.returncode:row.update(status='failed',error=p.stderr.decode('utf-8',errors='replace')[-2500:]);return row,None
        data=json.loads(dest.read_text(encoding='utf-8'))
        assert row['sourceUnchanged'];assert data['version']==4
        for f in data['faces']:
            for w in f['wires']:
                assert w['rawEdgeUses']==w['expectedEdgeUses']==sum(len(g['sourceUses']) for g in w['groups'])
                for g in w['groups']:
                    segments=g.get('roleSegments') or []
                    if segments:
                        assert [s['segmentId'] for s in segments]==list(range(1,len(segments)+1))
                        assert all(s['groupId']==g['groupId'] and s['kind'] in BITS for s in segments)
                        assert [e for s in segments for e in s['sourceUses']]==g['sourceUses'],'role segments changed original edge traversal'
        row['status']='diagnosed';return row,data
    except Exception as e:row.update(status='failed',error=str(e));return row,None

def physical(face):
    return [segment for w in face['wires'] for g in w['groups'] if not g['seam'] and not g['event']
            for segment in (g.get('roleSegments') or [g])]

def fixture_checks():
    fixtures=OUT/'fixtures';fixtures.mkdir(parents=True,exist_ok=True)
    subprocess.run([str(EXE),'--fixtures',str(fixtures)],env=ENV,capture_output=True,check=True)
    specifications=[('fragmented-partial',7),('fragmented-through',3),('only-side',2),('side-internal',6),('only-internal',4),('skin-internal',5),('curved-side',7),('void-is-not-internal',None)]
    checks=[]
    for name,expected in specifications:
        row,d=probe(fixtures/f'{name}.step',fixtures/f'{name}.json');assert d,row
        gs=physical(d['faces'][0]);kinds=Counter(g['kind'] for g in gs);mask=sum(BITS[k] for k in kinds if k in BITS)
        if expected is None:assert mask==0 and kinds['Unresolved']==3,(name,kinds)
        else:assert mask==expected and not kinds['Unresolved'],(name,kinds)
        if name=='curved-side':assert any(g['kind']=='Side' and not g['straight'] and g['chordSampledError']>.001 for g in gs)
        if name.startswith('fragmented'):assert any(g['kind']=='Side' and len(g['sourceUses'])==3 for g in gs)
        checks.append({'name':name,'expectedMask':expected,'actualRoles':dict(kinds),'passed':True})
    row,d=probe(BASE/'analytic-cases/bar-end.step',fixtures/'bar-end.json');assert d,row
    assert all(g['kind']=='Skin' for g in physical(d['faces'][0]));checks.append({'name':'bar-end','expectedMask':1,'passed':True})
    (OUT/'checks.json').write_text(json.dumps(checks,ensure_ascii=False,indent=2),encoding='utf-8')
    print(f'解析校验 {len(checks)}/{len(checks)} 通过，包括曲侧边与管腔排除。',flush=True)

def face_row(item,data,face):
    gs=physical(face);roles=Counter(g['kind'] for g in gs);mask=sum(BITS[k] for k in roles if k in BITS)
    issues=Counter(g['reason'] for g in gs if g['kind']=='Unresolved')
    complete=not issues and bool(gs)
    signature=' | '.join(' '.join(('缝' if g['seam'] else '点' if g['event'] else {'Skin':'皮','Side':'侧','Internal':'内','Unresolved':'?'}[g['kind']])+('○' if g['closed'] else '') for g in w['groups']) for w in face['wires'])
    return {'caseId':item['id'],'file':data['file'],'faceId':face['faceId'],'surface':face['surface'],'area':face['area'],'complete':complete,'mask':mask,'combination':NAMES.get(mask,'无有效角色'),'wireCount':len(face['wires']),'groupCount':len(gs),'skin':roles['Skin'],'side':roles['Side'],'internal':roles['Internal'],'curvedSides':sum(g['kind']=='Side' and not g['straight'] for g in gs),'issues':dict(issues),'wireSignature':signature,'source':item['source']}

def rank(row):
    expected={1:4,2:3,3:4,4:4,5:2,6:3,7:4}[row['mask']]
    return (row['area']<1,abs(row['groupCount']-expected),row['surface']!='plane',row['wireCount'] if row['mask']!=5 else abs(row['wireCount']-2),row['file'],row['faceId'])

def summarize(manifest,rows):
    complete=[r for r in rows if r['complete']];incomplete=[r for r in rows if not r['complete']]
    counts=Counter(r['mask'] for r in complete);partial=Counter(r['mask'] for r in incomplete);issues=Counter()
    for r in incomplete:issues.update(r['issues'])
    examples={str(m):sorted([r for r in complete if r['mask']==m],key=rank)[:8] for m in range(1,8)}
    summary={'rule':'whole group on skin -> Skin; open group endpoint(s) on skin and body inside section material -> Side; whole group inside material -> Internal; straightness does not affect role',
        'files':len(manifest),'filesDiagnosed':sum(r['status']=='diagnosed' for r in manifest),'faces':len(rows),'completeFaces':len(complete),'unresolvedFaces':len(incomplete),'completeCounts':{NAMES[m]:counts[m] for m in range(1,8)},'partialKnownMasks':{NAMES[m]:partial[m] for m in range(1,8)},'unresolvedReasons':dict(issues),'curvedSideGroups':sum(r['curvedSides'] for r in rows),'multiWireCompleteFaces':sum(r['wireCount']>1 for r in complete),'examples':examples,'failures':[r for r in manifest if r['status']!='diagnosed'],'scope':'sampled boundary classification with native section-domain queries; not certified toolpath generation'}
    (OUT/'summary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding='utf-8')
    (OUT/'faces.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf-8')
    if rows:
        with (OUT/'faces.csv').open('w',encoding='utf-8-sig',newline='') as stream:
            writer=csv.DictWriter(stream,fieldnames=list(rows[0]));writer.writeheader();writer.writerows(rows)
    print(json.dumps({k:v for k,v in summary.items() if k!='examples'},ensure_ascii=False,indent=2),flush=True)
    return summary

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--limit',type=int,default=0);parser.add_argument('--reuse',action='store_true');parser.add_argument('--export',action='store_true');args=parser.parse_args()
    OUT.mkdir(parents=True,exist_ok=True)
    source_manifest=json.loads((BASE/'manifest.json').read_text(encoding='utf-8'))
    if args.limit:source_manifest=source_manifest[:args.limit]
    if args.reuse:
        manifest=json.loads((OUT/'manifest.json').read_text(encoding='utf-8'));rows=[]
        for item in manifest:
            if item['status']=='diagnosed':
                d=json.loads(Path(item['evidence']).read_text(encoding='utf-8'));rows.extend(face_row(item,d,f) for f in d['faces'])
    else:
        fixture_checks();manifest=[];rows=[]
        def task(item):
            r,d=probe(Path(item['source']),OUT/'cases'/f"{item['id']:03d}.json");r['id']=item['id'];return r,d
        with ThreadPoolExecutor(max_workers=4) as pool:
            for future in as_completed([pool.submit(task,i) for i in source_manifest]):
                item,d=future.result();manifest.append(item)
                if d:rows.extend(face_row(item,d,f) for f in d['faces'])
                if len(manifest)%20==0 or not d:print(f"{len(manifest)}/{len(source_manifest)} {item['status']} {Path(item['source']).name}",flush=True)
                (OUT/'manifest.json').write_text(json.dumps(sorted(manifest,key=lambda r:r['id']),ensure_ascii=False,indent=2),encoding='utf-8')
        rows.sort(key=lambda r:(r['caseId'],r['faceId']))
    summary=summarize(manifest,rows)
    if args.export:
        for mask,options in summary['examples'].items():
            if not options:continue
            choice=options[0];target=OUT/'examples'/f"combination-{mask}.json";r,d=probe(Path(choice['source']),target,choice['faceId']);assert d,r
            print(f"导出组合 {mask}: {choice['file']} F{choice['faceId']}",flush=True)

if __name__=='__main__':main()
