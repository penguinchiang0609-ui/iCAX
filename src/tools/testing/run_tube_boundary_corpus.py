"""Run current Classify and boundary-core restoration for the complete tube corpus."""
from __future__ import annotations
import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import subprocess
import threading
import time
from datetime import datetime, timezone

REPO = Path(__file__).resolve().parents[3]
PY_ROOT = REPO / "output/tests/tube-boundary-core-20261030"
def read(path): return json.loads(Path(path).read_text(encoding="utf-8-sig"))
def digest(path): return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def write(path, value):
    path = Path(path); path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".writing")
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding="utf-8")
    temporary.replace(path)
def evidence(path): return {"path": str(path), "sha256": digest(path)}
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native", type=Path, default=PY_ROOT/"integrated/native")
    parser.add_argument("--output", type=Path, default=PY_ROOT/"corpus")
    parser.add_argument("--workers", type=int, default=4)
    parser.add_argument("--timeout", type=float, default=240)
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--models", default="")
    parser.add_argument("--aligned-from", type=Path)
    parser.add_argument("--failed-from", type=Path)
    args=parser.parse_args();out=args.output.resolve();native=args.native.resolve();out.mkdir(parents=True, exist_ok=True)
    exe=native/"TubeBoundaryCorpusProbe.exe"
    runtime={name:evidence(native/name) for name in [exe.name,"OpenCascadeResourceImport.dll","ExtrusionRecognition.dll"]}
    baseline_root=REPO/"output/tests/machining-edge-analysis-20261028"
    reports=[baseline_root/"full-span-baseline-summary.json",baseline_root/"repair-round1/summary.json",baseline_root/"repair-round2/summary.json"]
    previous={}
    for report in reports:
        for row in read(report)["rows"]: previous[row["group"]]=row
    source_report=REPO/"TestData/Tube/Reports/summary.json"
    parts=read(source_report)["parts"]
    aligned_run=args.aligned_from.resolve() if args.aligned_from else None
    aligned_rows={r['id']:r for r in read(aligned_run/'summary.json')['rows']} if aligned_run else {}
    aligned_checks=read(aligned_run/'aligned-checkpoints.json') if aligned_run else {}
    failed_ids={r['id'] for r in read(args.failed_from/'summary.json')['rows'] if r['status']!='passed'} if args.failed_from else None
    def model_id(part):
        return previous[part["group"]]["id"]
    selected=set(filter(None,args.models.split(',')))
    parts=[p for p in parts if (not selected or str(model_id(p)) in selected or p['group'] in selected) and (failed_ids is None or model_id(p) in failed_ids)]
    checkpoints=read(baseline_root/"verified-upstream-checkpoints.json")
    inventory=read(PY_ROOT/"corpus/cache-inventory.json") if (PY_ROOT/"corpus/cache-inventory.json").is_file() else {}
    lock=threading.Lock()
    result={"schema":1,"status":"running","scope":"New current-Classify boundary-core corpus; upstream Heal reused, current Align/Contour recomputed unless a certified exact checkpoint is selected", "startedAt":datetime.now(timezone.utc).isoformat(),"selected":len(parts),"runtime":runtime,"sourceReport":evidence(source_report),"runOrigins":[evidence(p) for p in reports],"rows":[]}
    if aligned_run:
        result['scope']='Failed-model repair: reuse baseline normalized neutral only; current exact Section, Classify and boundary Core are all recomputed'
        result['runOrigins'] += [evidence(aligned_run/'summary.json'),evidence(aligned_run/'aligned-checkpoints.json')]
    if args.resume and (out/"summary.json").exists():
        old=read(out/"summary.json")
        if old['runtime'] != runtime: raise RuntimeError("Cannot resume corpus with a different native producer")
        result['rows']=old['rows']; result['startedAt']=old['startedAt']
    completed={r['id'] for r in result['rows']}
    def save():
        result['rows'].sort(key=lambda r:r['id']);result['completed']=len(result['rows'])
        result['passed']=sum(r['status']=='passed' for r in result['rows']);result['failed']=result['completed']-result['passed']
        write(out/'summary.json',result)
        models=[]
        for row in result['rows']:
            model={'id':row['id'],'name':row['group'],'status':row['status'],'error':row.get('error'),'stage':row.get('stage'),
                   'dataFile':str(Path(row['resultFile']).relative_to(out).as_posix())}
            if row['status']=='passed':
                case=Path(row['resultFile']).parent
                model['coreFile']=str((case/'core.ictcad').relative_to(out).as_posix())
                model['wholeModelBrep']=str((case/'whole-model.brep').relative_to(out).as_posix())
            models.append(model)
        write(out/'manifest.json',{'schema':1,'kind':'tube-boundary-core-corpus','models':models,'coreContract':['Section','MachiningFaces'],
            'failedCasesHaveNoPublishedWholeModel':True})
    save()
    env=os.environ.copy();env['PATH']=os.pathsep.join([str(native),str(REPO/'src/x64/Debug'),str(REPO/'src/third_party/opencascade/install/vs2022-x64/win64/vc14/bind'),env.get('PATH','')])
    def run(part):
        case_id=model_id(part);case_dir=out/'models'/f'{case_id:03d}';case_dir.mkdir(parents=True,exist_ok=True)
        row={'id':case_id,'group':part['group'],'status':'failed','inputBRep':part['inputBRep'],'runtime':runtime,'resultFile':str(case_dir/'result.json')};began=time.monotonic()
        try:
            source=Path(part['inputBRep']);row['sourceSha256']=digest(source)
            prior=previous[part['group']]
            if prior['sourceSha256']!=row['sourceSha256']: raise RuntimeError('Latest neutral source no longer matches prior validated input')
            stage=read(prior['reportFile']);healed=Path(stage['healedBRep']) if 'healedBRep' in stage else None
            if healed is None:
                for report in reports:
                    found=next((r for r in read(report)['rows'] if r['group']==part['group']),None)
                    if found:
                        data=read(found['reportFile'])
                        if 'healedBRep' in data:healed=Path(data['healedBRep']);break
            if healed is None or not healed.is_file():raise RuntimeError('Validated upstream healed neutral is missing')
            row['healed']=evidence(healed);row['upstreamReport']=evidence(Path(prior['reportFile']))
            cache=None;aligned_input=None
            if aligned_run:
                baseline=aligned_rows[case_id]
                if baseline['sourceSha256']!=row['sourceSha256'] or not baseline['sourceUnchanged']:raise RuntimeError('Baseline normalized input belongs to a different original CAD')
                checkpoint=aligned_checks[str(case_id)]
                if digest(checkpoint['path'])!=checkpoint['sha256']:raise RuntimeError('Baseline normalized BRep changed after the completed run')
                aligned_input=Path(checkpoint['path']);row['alignedCheckpoint']=checkpoint
            certified=checkpoints.get(part['group']) if aligned_input is None else None
            if certified:
                if certified['sourceSha256'] != row['sourceSha256']:raise RuntimeError('Certified checkpoint source hash mismatch')
                for name,expected in certified['files'].items():
                    if digest(Path(certified['directory'])/name)!=expected:raise RuntimeError('Certified checkpoint bytes changed: '+name)
                for ref in certified.get('references',[]):
                    if digest(ref['path'])!=ref['sha256']:raise RuntimeError('Certified checkpoint provenance changed')
                cache=Path(certified['directory'])
                if digest(cache/'healed.icaxbrep')!=row['healed']['sha256']:raise RuntimeError('Certified checkpoint healed bytes differ from current upstream preparation')
                row['certifiedUpstreamCheckpoint']=certified
            else:
                for candidate in ([] if aligned_input else inventory.get(str(source.resolve()).lower(),[])):
                    candidate=Path(candidate)
                    try:
                        perf=read(candidate/'performance.json')
                        if perf.get('status')!='passed' or not perf.get('sourceNeutralUnchanged'):continue
                        if not all(perf['stages'][stage].get('passed') for stage in ['heal','align','contour']):continue
                        if digest(candidate/'healed.icaxbrep')!=row['healed']['sha256']:continue
                        cache=candidate;break
                    except (OSError,KeyError,ValueError):continue
            if aligned_input:
                mode='--aligned';argument=aligned_input;row['upstreamMode']='baseline-normalized-neutral; current-section-classify-core'
            elif cache:
                mode='--cache';argument=cache
                row['upstreamMode']='validated-aligned-and-exact-section-checkpoint'
                row['cache']={name:evidence(cache/name) for name in ['healed.icaxbrep','aligned.icaxbrep','section-curves.icaxbrep','section-domains.json','performance.json']}
            else:
                mode='--healed';argument=healed;row['upstreamMode']='validated-heal-then-current-align-and-contour'
            row['command']=[str(exe),mode,str(argument),str(case_dir)]
            with (case_dir/'native.log').open('wb') as log:
                try:
                    process=subprocess.run(row['command'],cwd=REPO/'src/x64/Debug',env=env,stdout=log,stderr=subprocess.STDOUT,timeout=args.timeout,creationflags=subprocess.CREATE_NO_WINDOW)
                    row['exitCode']=process.returncode;row['timedOut']=False
                except subprocess.TimeoutExpired:
                    row['exitCode']=None;row['timedOut']=True
            data=read(case_dir/'result.json') if (case_dir/'result.json').exists() else {}
            row['stage']=data.get('stage','input');row['analysis']=data
            row['status']='passed' if row.get('exitCode')==0 and data.get('status')=='passed' else 'failed'
            row['error']=f'Native case exceeded {args.timeout:g} seconds during {row["stage"]}' if row['timedOut'] else data.get('error')
            row['sourceUnchanged']=digest(source)==row['sourceSha256'] and digest(healed)==row['healed']['sha256']
            if not row['sourceUnchanged']:row['status']='failed';row['error']='Input/source neutral bytes changed during corpus run'
            if cache:
                for name,record in row['cache'].items():
                    if digest(record['path'])!=record['sha256']:raise RuntimeError('Upstream cached input changed during core run: '+name)
            if aligned_input and digest(aligned_input)!=row['alignedCheckpoint']['sha256']:raise RuntimeError('Baseline normalized source changed during repair')
            if (case_dir/'result.json').exists():
                (case_dir/'native-result.json').write_bytes((case_dir/'result.json').read_bytes())
            data['nativeStatus']=data.get('status');data['status']=row['status'];data['modelId']=case_id;data['originalName']=part['group']
            data['execution']={'exitCode':row.get('exitCode'),'timedOut':row.get('timedOut'),'error':row.get('error')}
            data['inputEvidence']={'source':{'path':str(source),'sha256':row['sourceSha256']},'healed':row['healed'],'cache':row.get('cache')}
            data['producerEvidence']=runtime
            write(case_dir/'result.json',data)
        except Exception as error:
            row['status']='failed';row['error']=str(error)
            file=case_dir/'result.json';data=read(file) if file.exists() else {}
            data.update({'status':'failed','modelId':case_id,'originalName':part['group'],'error':str(error),
                'stage':data.get('stage','input-validation'),'producerEvidence':runtime})
            row['stage']=data['stage'];write(file,data)
        row['seconds']=time.monotonic()-began
        with lock:
            result['rows'].append(row);save()
            print(f"{result['completed']}/{result['selected']} case {case_id} {row['status']} {row.get('stage')} {row.get('error') or ''}",flush=True)
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as pool:
        list(pool.map(run,[p for p in parts if model_id(p) not in completed]))
    result['status']='passed' if not result['failed'] else 'completed-with-failures';result['completedAt']=datetime.now(timezone.utc).isoformat();save()
    return 0 if not result['failed'] else 1
if __name__=='__main__':raise SystemExit(main())
