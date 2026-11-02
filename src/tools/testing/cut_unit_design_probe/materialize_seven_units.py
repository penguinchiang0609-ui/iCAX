"""Generate exact ruled BREP units for the seven real examples selected in the inventory."""
from concurrent.futures import ThreadPoolExecutor,as_completed
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
from run_face_combinations import ROOT,OUT,ENV

sys.stdout.reconfigure(encoding='utf-8')
DEST=OUT.parent/'seven-cut-units'
EXE=OUT.parent/'build/Release/build_cut_unit_examples.exe'
COLORS={1:'198a68',3:'2a6edc',7:'cb6e18',5:'8552c7',4:'c1426b',6:'138b9a',2:'a27810'}
MODES={1:'projected',2:'fan',3:'strip',4:'strip',5:'periodic',6:'strip',7:'strip'}

def run(mask,choice):
    folder=DEST/f'combination-{mask}';folder.mkdir(parents=True,exist_ok=True)
    d=json.loads((OUT/'examples'/f'combination-{mask}.json').read_text(encoding='utf-8'))
    groups=[g for w in d['faces'][0]['wires'] for g in w['groups'] if not g['seam'] and not g['event']]
    if mask==5:
        a=next(g for g in groups if g['kind']=='Skin');b=next(g for g in groups if g['kind']=='Internal')
    else:
        i,j={1:(1,3),2:(1,0),3:(0,2),4:(1,3),6:(1,3),7:(2,0)}[mask];a,b=groups[i],groups[j]
    lines=[MODES[mask]]
    for group in [a,b]:
        lines.append(str(len(group['sourceUses'])))
        lines.extend(f"{u['edgeId']} {int(u['reversed'])}" for u in group['sourceUses'])
    config=folder/'rails.txt';config.write_text('\n'.join(lines)+'\n',encoding='utf-8')
    source=Path(choice['source']);before=hashlib.sha256(source.read_bytes()).hexdigest()
    proc=subprocess.run([str(EXE),str(source),str(choice['faceId']),str(config),str(folder),COLORS[mask]],env=ENV,capture_output=True,timeout=240)
    (folder/'native.log').write_bytes(proc.stdout+b'\n'+proc.stderr)
    result={'combination':mask,'source':str(source),'sourceFile':choice['file'],'faceId':choice['faceId'],
        'sourceSha256':before,'sourceUnchanged':before==hashlib.sha256(source.read_bytes()).hexdigest(),
        'color':'#'+COLORS[mask],'mode':MODES[mask],'folder':str(folder),'exitCode':proc.returncode}
    result['status']='passed' if proc.returncode==0 else 'failed'
    if proc.returncode==0:
        result['unit']=json.loads((folder/'unit-01.json').read_text(encoding='utf-8'))
        result['railSourceGroups']=[{'kind':g['kind'],'sourceUses':g['sourceUses']} for g in [a,b]]
    else:result['error']=proc.stderr.decode('utf-8',errors='replace')[-2000:]
    return result

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--only',type=int,nargs='+');args=parser.parse_args()
    masks=args.only or [1,3,7,5,4,6,2]
    DEST.mkdir(parents=True,exist_ok=True);s=json.loads((OUT/'summary.json').read_text(encoding='utf-8'));results=[]
    if args.only and (DEST/'manifest.json').exists():results=[r for r in json.loads((DEST/'manifest.json').read_text(encoding='utf-8')) if r['combination'] not in masks]
    with ThreadPoolExecutor(max_workers=3) as pool:
        for f in as_completed([pool.submit(run,m,s['examples'][str(m)][0]) for m in masks]):
            r=f.result();results.append(r);print(json.dumps({k:v for k,v in r.items() if k not in ['unit','railSourceGroups']},ensure_ascii=False),flush=True)
    results.sort(key=lambda r:[1,3,7,5,4,6,2].index(r['combination']))
    (DEST/'manifest.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
    if any(r['status']!='passed' for r in results):raise SystemExit(1)

if __name__=='__main__':main()
