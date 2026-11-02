"""Run face-first grouping diagnostics without changing source BRep files."""
import argparse
from collections import Counter
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

sys.stdout.reconfigure(encoding='utf-8')
root=Path(__file__).resolve().parents[4]
base=root/'output/tests/cut-unit-design-20261023'
out=base/'face-boundaries'
exe=base/'build/Release/face_boundary_probe.exe'
env=os.environ.copy()
env['PATH']=str(root/'src/third_party/opencascade/install/vs2022-x64/win64/vc14/bin')+os.pathsep+env['PATH']

def run(source,dest):
    before=hashlib.sha256(source.read_bytes()).hexdigest()
    start=time.perf_counter()
    p=subprocess.run([str(exe),str(source),str(dest)],env=env,capture_output=True,timeout=120)
    row={'source':str(source),'evidence':str(dest),'sha256Before':before,'seconds':round(time.perf_counter()-start,3),'exitCode':p.returncode,'sourceUnchanged':before==hashlib.sha256(source.read_bytes()).hexdigest()}
    if p.returncode:
        row.update(status='failed',stderr=p.stderr.decode('utf-8',errors='replace')[-2000:])
        return row,None
    data=json.loads(dest.read_text(encoding='utf-8'))
    assert row['sourceUnchanged']
    assert all(sum(len(g['sourceUses']) for g in w['groups'])==w['rawEdgeUses'] for f in data['faces'] for w in f['wires'])
    row['status']='diagnosed'
    return row,data

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--limit',type=int,default=0);args=parser.parse_args()
    out.mkdir(parents=True,exist_ok=True)
    fixtures=out/'fixtures'
    subprocess.run([str(exe),'--fixtures',str(fixtures)],env=env,capture_output=True,check=True)
    checks=[]
    for name,kinds in [('fragmented-partial',{'Skin':1,'Side':2,'Internal':1}),('fragmented-through',{'Skin':2,'Side':2})]:
        row,data=run(fixtures/f'{name}.step',fixtures/f'{name}.json')
        assert data is not None,row
        wire=data['faces'][0]['wires'][0]
        assert wire['rawEdgeUses']==8 and len(wire['groups'])==4
        assert Counter(g['kind'] for g in wire['groups'])==kinds
        side=max((g for g in wire['groups'] if g['kind']=='Side'),key=lambda g:len(g['sourceUses']))
        assert len(side['sourceUses'])==3
        checks.append({'case':name,'rawEdges':8,'logicEdges':4,'roles':kinds,'threeFragmentSideFound':True,'passed':True})
    row,data=run(base/'analytic-cases/bar-end.step',fixtures/'bar-end.json')
    assert data is not None,row
    groups=data['faces'][0]['wires'][0]['groups']
    assert len(groups)==4 and all(g['kind']=='Skin' for g in groups)
    checks.append({'case':'bar-end','roles':{'Skin':4},'sideCount':0,'passed':True})
    (out/'checks.json').write_text(json.dumps(checks,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps({'fixtureChecks':checks},ensure_ascii=False),flush=True)

    source_manifest=json.loads((base/'manifest.json').read_text(encoding='utf-8'))
    if args.limit:source_manifest=source_manifest[:args.limit]
    manifest=[];totals=Counter();roles=Counter();examples=[];traversal_issues=[]
    def task(item):
        row,data=run(Path(item['source']),out/'cases'/f"{item['id']:03}.json")
        row['id']=item['id'];return row,data
    with ThreadPoolExecutor(max_workers=4) as pool:
        jobs=[pool.submit(task,item) for item in source_manifest]
        for future in as_completed(jobs):
            row,data=future.result();manifest.append(row)
            if data:
                totals['files']+=1
                for f in data['faces']:
                    totals['faces']+=1
                    for w in f['wires']:
                        totals['wires']+=1;totals['rawEdgeUses']+=w['rawEdgeUses'];totals['logicEdges']+=len(w['groups'])
                        if w['expectedEdgeUses']!=w['rawEdgeUses']:traversal_issues.append({'file':data['file'],'faceId':f['faceId'],'wireId':w['wireId'],'expected':w['expectedEdgeUses'],'visited':w['rawEdgeUses']})
                        for g in w['groups']:
                            roles[g['kind']]+=1
                            n=len(g['sourceUses'])
                            if n>1:
                                totals['mergedGroups']+=1
                                if g['kind']=='Side':totals['mergedSideGroups']+=1
                                examples.append({'file':data['file'],'caseId':row['id'],'faceId':f['faceId'],'wireId':w['wireId'],'groupId':g['groupId'],'kind':g['kind'],'fragments':n,'straight':g['straight'],'startOnSkin':g['startOnSkin'],'endOnSkin':g['endOnSkin'],'sourceEdges':[e['edgeId'] for e in g['sourceUses']]})
            if len(manifest)%30==0 or row['status']!='diagnosed':print(f"{len(manifest)}/{len(source_manifest)} {row['status']} {Path(row['source']).name}",flush=True)
    manifest.sort(key=lambda r:r['id'])
    examples.sort(key=lambda r:r['fragments'],reverse=True)
    summary={'totals':dict(totals),'diagnosticRoles':dict(roles),'failed':[r for r in manifest if r['status']!='diagnosed'],'traversalIssues':traversal_issues,'largestGroups':examples[:12],'largestSideGroups':[e for e in examples if e['kind']=='Side'][:12],'largestCurvedGroups':[e for e in examples if not e['straight']][:12],'scope':'G0/G1 grouping then whole-group classification; sampled skin evidence; not production classification acceptance'}
    (out/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
    (out/'summary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps({k:v for k,v in summary.items() if not k.startswith('largest')},ensure_ascii=False,indent=2),flush=True)

if __name__=='__main__':main()
