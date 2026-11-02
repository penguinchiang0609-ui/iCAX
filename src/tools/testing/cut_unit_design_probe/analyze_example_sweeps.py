"""Construct explicit straight-line candidates on selected real faces, then check membership."""
import json
from pathlib import Path
import subprocess
import sys
import numpy as np
from run_face_combinations import OUT,EXE,ENV,probe

sys.stdout.reconfigure(encoding='utf-8')

def resample(poly,t):
    p=np.array(poly,float);lengths=np.linalg.norm(np.diff(p,axis=0),axis=1);s=np.r_[0,np.cumsum(lengths)]
    keep=np.r_[True,np.diff(s)>1e-12];p=p[keep];s=s[keep]
    return np.array([np.interp(t*s[-1],s,p[:,i]) for i in range(3)]).T

def paired(a,b):
    a=np.array(a,float);b=np.array(b,float)
    if np.linalg.norm(a[0]-b[-1])+np.linalg.norm(a[-1]-b[0])<np.linalg.norm(a[0]-b[0])+np.linalg.norm(a[-1]-b[-1]):b=b[::-1]
    ts=np.linspace(0,1,17);return np.stack([resample(a,ts),resample(b,ts)],axis=1)

def main():
    summary=json.loads((OUT/'summary.json').read_text(encoding='utf-8'));results=[]
    descriptions={1:'G4 原曲线沿固定方向投影到 G2，保留皮上原曲线；可形成一个变深度条带候选。',2:'从 G1/G2 的共同皮上端点向 G1 弧线作扇形母线；保留弓形面积，不拉直弧线。',3:'G1 与 G3 配对，G2/G4 为起止界限；一个常规条带。',4:'G2 与 G4 配对，得到一个内部条带；实际入口必须结合邻面。',5:'W2G1 皮上闭环与 W1G1 内部闭环配对，形成一个周期环带；闭合点只是参数缝。',6:'G2 与 G4 配对，G1/G3 为起止界限；几何上是一片条带，但没有正长度皮上入口轨道。',7:'G3 皮上边与 G1 内部边配对，G2/G4 为起止界限；一个条带。'}
    for mask in [1,2,3,4,5,6,7]:
        path=OUT/'examples'/f'combination-{mask}.json';d=json.loads(path.read_text(encoding='utf-8'));f=d['faces'][0]
        groups=[g for w in f['wires'] for g in w['groups'] if not g['seam'] and not g['event']]
        if mask==1:
            rail=np.array(groups[1]['polyline']);curve=np.array(groups[3]['polyline']);v=rail[-1]-rail[0];v=v/np.linalg.norm(v)
            selected=curve[np.linspace(0,len(curve)-1,17).round().astype(int)]
            projected=rail[0]+np.outer((selected-rail[0])@v,v);lines=np.stack([projected,selected],axis=1);form='strip-variable-depth'
        elif mask==2:
            curve=np.array(groups[0]['polyline']);selected=curve[np.linspace(1,len(curve)-1,17).round().astype(int)];lines=np.stack([np.repeat(curve[:1],len(selected),axis=0),selected],axis=1);form='fan-point-to-curve'
        elif mask==5:
            a=np.array(next(g for g in groups if g['kind']=='Skin')['polyline']);b=np.array(next(g for g in groups if g['kind']=='Internal')['polyline'])
            selected=a[np.linspace(0,len(a)-1,17).round().astype(int)];other=np.array([b[np.argmin(np.linalg.norm(b-p,axis=1))] for p in selected]);lines=np.stack([selected,other],axis=1);form='periodic-strip'
        else:
            i,j={3:(0,2),4:(1,3),6:(1,3),7:(2,0)}[mask];lines=paired(groups[i]['polyline'],groups[j]['polyline']);form='strip'
        choice=summary['examples'][str(mask)][0]
        plan={'form':form,'description':descriptions[mask],'displaySegments':lines.tolist(),'splitLines':[],'coverageClaim':'candidate boundary construction plus sampled membership only'}
        path.with_suffix('.plan.json').write_text(json.dumps(plan,ensure_ascii=False,indent=2),encoding='utf-8')
        segments=path.with_suffix('.segments.txt');segments.write_text(str(len(lines))+'\n'+'\n'.join(' '.join(f'{v:.16g}' for v in line.flatten()) for line in lines),encoding='utf-8')
        output=path.with_suffix('.ruling-check.json')
        subprocess.run([str(EXE),'--check-segments',choice['source'],str(choice['faceId']),str(segments),str(output)],env=ENV,capture_output=True,check=True,timeout=90)
        check=json.loads(output.read_text(encoding='utf-8'));results.append({'mask':mask,'file':d['file'],'faceId':f['faceId'],'form':form,'maxSampledDistanceMm':check['maxSampledDistanceMm'],'queryFailures':sum(x['queryFailures'] for x in check['lines']),'description':descriptions[mask]})
        print(json.dumps(results[-1],ensure_ascii=False),flush=True)
    (OUT/'ruling-checks.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
    rows=json.loads((OUT/'faces.json').read_text(encoding='utf-8'))
    for case,face,name in [(179,1,'skin-multiple-loops'),(158,9,'three-roles-eight-holes'),(43,3,'nonruled-four-groups')]:
        row=next(r for r in rows if r['caseId']==case and r['faceId']==face)
        result,data=probe(Path(row['source']),OUT/'examples'/f'{name}.json',face);assert data,result
        print(f"补充：{row['file']} F{face}，{row['wireCount']} 个 wire",flush=True)
        if name=='nonruled-four-groups':
            rails=[g for w in data['faces'][0]['wires'] for g in w['groups'] if g['kind']=='Skin'];lines=paired(rails[0]['polyline'],rails[1]['polyline'])
            p=OUT/'examples'/f'{name}.json';p.with_suffix('.plan.json').write_text(json.dumps({'displaySegments':lines.tolist(),'splitLines':[],'note':'initial arc-length pairing only; not an optimized approximation'},ensure_ascii=False,indent=2),encoding='utf-8')
            segments=p.with_suffix('.segments.txt');segments.write_text(str(len(lines))+'\n'+'\n'.join(' '.join(f'{v:.16g}' for v in line.flatten()) for line in lines),encoding='utf-8')
            result=p.with_suffix('.ruling-check.json');subprocess.run([str(EXE),'--check-segments',row['source'],str(face),str(segments),str(result)],env=ENV,capture_output=True,check=True,timeout=90)
            print('非直纹候选：'+result.read_text(encoding='utf-8')[-250:],flush=True)

if __name__=='__main__':main()
