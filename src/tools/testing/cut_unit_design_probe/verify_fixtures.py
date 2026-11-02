"""Analytic local checks for the boundary rule; no whole-toolpath success claims."""
import json,os,subprocess,sys
from pathlib import Path
sys.stdout.reconfigure(encoding='utf-8')
root=Path(__file__).resolve().parents[4]
out=root/'output/tests/cut-unit-design-20261023'
env=os.environ.copy()
env['PATH']=str(root/'src/third_party/opencascade/install/vs2022-x64/win64/vc14/bin')+os.pathsep+env['PATH']
directory=out/'analytic-cases'
subprocess.run([str(out/'build/Release/cut_unit_design_fixtures.exe'),str(directory)],env=env,capture_output=True,check=True)
checks=[]
for name,expected_faces,expected_positive,expected_negative in [('x-bevel',2,1,0),('k-bevel',3,2,0),('blind-with-floor',3,0,2),('blind-without-floor',2,0,0),('bar-end',1,0,0)]:
    target=directory/f'{name}.json'
    subprocess.run([str(out/'build/Release/cut_unit_design_probe.exe'),str(directory/f'{name}.step'),str(target)],env=env,capture_output=True,check=True,timeout=60)
    d=json.loads(target.read_text(encoding='utf-8'))
    relations=[r for e in d['edges'] for r in e['relations'] if r['a']!=r['b']]
    positive=sum(bool(r['angles']) and min(r['angles'])>0.001 for r in relations)
    negative=sum(bool(r['angles']) and max(r['angles'])<-.001 for r in relations)
    row={'case':name,'faces':d['sewnFaceCount'],'convexPairs':positive,'concavePairs':negative}
    assert (d['sewnFaceCount'],positive,negative)==(expected_faces,expected_positive,expected_negative),row
    assert abs(d['originalArea']-d['sewnArea'])<1e-7
    if name=='blind-without-floor':
        internal=[e for e in d['edges'] if e['profileDistanceMax']>0.001 and e['profileSamplesOn']==2]
        assert len(internal)==2
        assert all(abs(e['profileDistanceMax']-4)<1e-7 for e in internal)
        bottom_z=sum(e['samples'][len(e['samples'])//2][2] for e in internal)/len(internal)
        profile_z=[p[2] for edge in d['profile'] for p in edge]
        remaining=bottom_z-min(profile_z)
        depth=max(profile_z)-bottom_z
        assert abs(remaining-6)<1e-7 and abs(depth-4)<1e-7
        row['partialDepthTerminalEdges']=len(internal);row['floorRequired']=False
        row['remainingAlongBeamMm']=remaining;row['machiningDepthMm']=depth;row['beamDirection']=[0,0,-1]
    if name=='bar-end':
        assert len(d['edges'])==4 and all(e['profileSamplesOn']==e['sampleCount'] for e in d['edges'])
        row['allFourEdgesOnProfile']=True
    row['passed']=True;checks.append(row)
(out/'analytic-checks.json').write_text(json.dumps(checks,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(checks,ensure_ascii=False,indent=2))
