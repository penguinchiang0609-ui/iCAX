"""Run bounded read-only geometry probes; keep source hashes and per-file evidence."""
import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

def digest(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--root',type=Path,default=Path.cwd())
    parser.add_argument('--limit',type=int,default=0)
    parser.add_argument('--workers',type=int,default=4)
    parser.add_argument('--retry-failed',action='store_true')
    args=parser.parse_args()
    root=args.root.resolve()
    out=root/'output/tests/cut-unit-design-20261023'
    out.mkdir(parents=True,exist_ok=True)
    files=sorted((root/'TestData/Tube/Classify').glob('*.step'),key=lambda p:p.name.casefold())
    if args.limit: files=files[:args.limit]
    env=os.environ.copy()
    env['PATH']=str(root/'src/third_party/opencascade/install/vs2022-x64/win64/vc14/bin')+os.pathsep+env['PATH']
    exe=out/'build/Release/cut_unit_design_probe.exe'
    previous=json.loads((out/'manifest.json').read_text(encoding='utf-8')) if args.retry_failed else []
    jobs=[(r['id'],Path(r['source'])) for r in previous if r['status']!='passed'] if args.retry_failed else list(enumerate(files,1))
    manifest=list(previous)
    def run(item):
        index,path=item
        dest=out/'cases'/f'{index:03d}.json'
        dest.parent.mkdir(exist_ok=True)
        before=digest(path)
        row={'id':index,'source':str(path),'sha256Before':before,'evidence':str(dest)}
        start=time.perf_counter()
        try:
            proc=subprocess.run([str(exe),str(path),str(dest)],env=env,capture_output=True,timeout=100)
            row['exitCode']=proc.returncode
            row['stderr']=proc.stderr.decode('utf-8',errors='replace')[-2500:]
            if proc.returncode==0:
                data=json.loads(dest.read_text(encoding='utf-8'))
                row['faces']=data['sewnFaceCount']
                row['status']='passed'
            else:row['status']='failed'
        except Exception as e:
            row['status']='failed';row['error']=str(e)
        row['seconds']=round(time.perf_counter()-start,3)
        row['sourceUnchanged']=before==digest(path)
        return row
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as pool:
        for row in pool.map(run,jobs):
            old=next((r for r in manifest if r['id']==row['id']),None)
            if old:
                row['priorAttempts']=old.get('priorAttempts',[])+[{k:v for k,v in old.items() if k!='priorAttempts'}]
                manifest=[r for r in manifest if r['id']!=row['id']]
            manifest.append(row)
            manifest.sort(key=lambda r:r['id'])
            if len(manifest)%20==0 or row['status']!='passed':
                print(f"{len(manifest)}/{len(files)} {row['status']} {Path(row['source']).name} {row['seconds']}s",flush=True)
            (out/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps({'files':len(manifest),'passed':sum(r['status']=='passed' for r in manifest),'sourceUnchanged':all(r['sourceUnchanged'] for r in manifest)},ensure_ascii=False),flush=True)

if __name__=='__main__':main()
