"""Audit source hashes, full face coverage, emitted meshes, and colored STEP round trips."""
from concurrent.futures import ThreadPoolExecutor
from collections import Counter
import hashlib,json,math,subprocess
from pathlib import Path
from run_classify_units import DEST,ROOT,ENV,read,write

EXE=DEST.parent/'build/Release/audit_colored_units.exe'
def audit(row):
    folder=Path(row['folder']);result={'id':row['id'],'file':row['file'],'issues':[]}
    if row['status']!='processed':
        result['issues'].append(row.get('error','generation-failed'));return result
    for key in ['source','originalSource']:
        assert hashlib.sha256(Path(row[key]).read_bytes()).hexdigest()==row['sourceSha256'],(key,row['id'])
    result['sourceUnchanged']=True
    native=read(folder/'native.json');assert native['faceCount']==row['faces']
    assert [f['faceId'] for f in native['faces']]==list(range(1,native['faceCount']+1))
    expected={};expected_colors={};pending=0;meshes=0
    for f in native['faces']:
        if f['units']:
            for u in f['units']:
                name=f"F{f['faceId']:04d}_U{u['id']:03d}_"+('APPROX' if f['status']=='approximate' else 'EXACT')
                expected[name]=u['areaMm2'];expected_colors[name]=u['color'];assert (folder/u['file']).stat().st_size>0
                if f['status']=='approximate':assert u['sampledErrorMm']<=row['fitToleranceMm']
            assert f['status'] in ['exact','approximate'] and not f['reason']
        else:
            pending+=1;assert f['status']=='pending' and f['reason'];expected[f"F{f['faceId']:04d}_PENDING"]=f['sourceAreaMm2'];expected_colors[f"F{f['faceId']:04d}_PENDING"]=[.62,.65,.69];assert (folder/f['pendingBrep']).stat().st_size>0
        paths=[u['mesh'] for u in f['units']]+([f['pendingMesh']] if f.get('pendingMesh') else [])
        for path in paths:
            mesh=read(folder/path);assert mesh['vertices'] and mesh['triangles'],path
            assert all(len(p)==3 and all(math.isfinite(v) for v in p) for p in mesh['vertices'])
            assert all(len(t)==3 and min(t)>=0 and max(t)<len(mesh['vertices']) for t in mesh['triangles'])
            meshes+=1
    assert sum(len(f['units']) for f in native['faces'])==native['unitCount']==row['units']
    assert pending==native['pendingFaces']==row['pendingFaces']
    output=folder/'step-audit.json'
    if not output.exists() or output.stat().st_mtime<max((folder/'colored.step').stat().st_mtime,EXE.stat().st_mtime):
        p=subprocess.run([str(EXE),str(folder/'colored.step'),str(output)],env=ENV,capture_output=True,timeout=180)
        if p.returncode:
            result['issues'].append('STEP-read-failed: '+p.stderr.decode('utf-8',errors='replace')[-500:]);return result
    step=read(output);result.update(expectedFaces=len(expected),stepFaces=step['faces'],coloredFaces=step['coloredFaces'],meshes=meshes,invalidStepRoots=step['invalidRoots'])
    if step['faces']!=len(expected):result['issues'].append('STEP-face-count-mismatch')
    if step['coloredFaces']!=step['faces']:result['issues'].append('STEP-color-missing')
    found=set();max_delta=0;max_color_delta=0
    for entry in step['entries']:
        name=entry['name'];found.add(name)
        if name not in expected:result['issues'].append('STEP-name-missing: '+name);continue
        delta=abs(entry['areaMm2']-expected[name]);max_delta=max(max_delta,delta)
        if entry.get('color'):
            color_delta=max(abs(a-b) for a,b in zip(entry['color'],expected_colors[name]));max_color_delta=max(max_color_delta,color_delta)
            # STEP/OCCT color conversion may round normalized channels by about 3e-5.
            if color_delta>1e-4:result['issues'].append('STEP-color-mismatch: '+name)
        if delta>max(1e-6,expected[name]*1e-7):result['issues'].append(f'STEP-area-drift: {name} {delta:.9g}')
        if not entry['valid']:result['issues'].append('STEP-invalid: '+name)
    if found!=set(expected):result['issues'].append('STEP-label-set-mismatch')
    result['maxStepAreaDeltaMm2']=max_delta
    result['maxStepColorChannelDelta']=max_color_delta
    return result

def safe_audit(row):
    try:return audit(row)
    except Exception as e:return {'id':row['id'],'file':row['file'],'sourceUnchanged':False,'issues':[f'audit-failed: {type(e).__name__}: {e}']}

def main():
    rows=read(DEST/'manifest.json')
    sources=read(DEST/'source-manifest.json')
    assert {r['id'] for r in rows}=={r['id'] for r in sources},'Incomplete file inventory'
    assert {Path(r['originalSource']).name for r in sources}=={p.name for p in (ROOT/'TestData/Tube/Classify').glob('*.step')},'Classify file inventory changed'
    with ThreadPoolExecutor(max_workers=4) as pool:
        results=list(pool.map(safe_audit,rows))
    summary={'filesAudited':len(results),'sourceSnapshotsAndCurrentFilesUnchanged':all(r.get('sourceUnchanged',False) for r in results),'filesWithIssues':sum(bool(r['issues']) for r in results),'roundTripIssues':dict(Counter(i.split(':')[0] for r in results for i in r['issues'])),'results':results}
    write(DEST/'validation.json',summary)
    print(json.dumps({k:v for k,v in summary.items() if k!='results'},ensure_ascii=False,indent=2))
    for r in results:
        if r['issues']:print(json.dumps(r,ensure_ascii=False))
    if summary['filesWithIssues']:raise SystemExit(1)
if __name__=='__main__':main()
