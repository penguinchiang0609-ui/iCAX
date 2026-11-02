"""Render original example boundaries and optional proposed ruling segments."""
import json
from pathlib import Path
import numpy as np
from PIL import Image,ImageDraw,ImageFont

ROOT=Path(__file__).resolve().parents[4]
OUT=ROOT/'output/tests/cut-unit-design-20261023/face-combinations'
FONT='C:/Windows/Fonts/msyh.ttc'
def font(size):return ImageFont.truetype(FONT,size)
COLORS={'Skin':'#15956b','Side':'#de7e1a','Internal':'#326ace','Unresolved':'#bb3561'}
NAMES={1:'皮上',2:'侧边',3:'皮上＋侧边',4:'内部',5:'皮上＋内部',6:'侧边＋内部',7:'皮上＋侧边＋内部'}

def renderer(points,box,basis=None):
    p=np.asarray(points,float);center=p.mean(axis=0)
    if basis is None:_,_,v=np.linalg.svd(p-center,full_matrices=False);basis=v[:2].T
    q=(p-center)@basis;lo=q.min(axis=0);hi=q.max(axis=0);mid=(lo+hi)/2
    x,y,X,Y=box;scale=min((X-x)/max(hi[0]-lo[0],1e-8),(Y-y)/max(hi[1]-lo[1],1e-8))
    def xy(a):
        b=(np.asarray(a,float)-center)@basis
        return ((x+X)/2+(b[0]-mid[0])*scale,(y+Y)/2-(b[1]-mid[1])*scale)
    return xy

def dashed(draw,a,b,color,width=2):
    a=np.array(a);b=np.array(b);length=np.linalg.norm(b-a)
    for t in np.arange(0,length,14):
        x=a+(b-a)*t/max(length,1e-9);y=a+(b-a)*min(t+7,length)/max(length,1e-9)
        draw.line([tuple(x),tuple(y)],fill=color,width=width)

def draw(mask,path=None,label=None):
    path=path or OUT/'examples'/f'combination-{mask}.json';d=json.loads(path.read_text(encoding='utf-8'));f=d['faces'][0]
    mesh=json.loads(path.with_suffix('.mesh.json').read_text(encoding='utf-8'))
    groups=[(w['wireId'],g) for w in f['wires'] for g in w['groups']]
    points=[p for _,g in groups for p in g['polyline']]
    im=Image.new('RGB',(1440,940),'#f4f7fb');dr=ImageDraw.Draw(im)
    dr.text((35,24),label or f"{mask}   {NAMES[mask]}",font=font(31),fill='#172c46')
    dr.text((35,73),f"{d['file']}    原始 F{f['faceId']}",font=font(21),fill='#3e526c')
    dr.text((35,108),f"{f['surface']}   面积 {f['area']:.6g} mm²   {len(f['wires'])} 个 wire",font=font(19),fill='#627087')
    dr.rounded_rectangle((25,150,945,765),radius=14,fill='white',outline='#dce4ef',width=2)
    dr.rounded_rectangle((965,150,1415,765),radius=14,fill='white',outline='#dce4ef',width=2)
    dr.text((45,166),'Face 边界正投影',font=font(21),fill='#344c69')
    dr.text((985,166),'截面投影：边与皮的位置',font=font(19),fill='#344c69')
    xy=renderer(points,(95,225,870,705))
    for triangle in mesh['triangles']:
        dr.polygon([xy(mesh['vertices'][i]) for i in triangle],fill='#edf2f9')
    planpath=path.with_suffix('.plan.json')
    if planpath.exists():
        plan=json.loads(planpath.read_text(encoding='utf-8'))
        for a,b in plan['displaySegments']:dashed(dr,xy(a),xy(b),'#b1bdcf',2)
        for a,b in plan.get('splitLines',[]):dashed(dr,xy(a),xy(b),'#ab3bb0',3)
    # The right view retains the source section's physical scale.
    pxy=renderer(points+[p for e in d['profile'] for p in e],(995,235,1380,690),np.array([[0,0],[1,0],[0,1]],float))
    for edge in d['profile']:dr.line([pxy(p) for p in edge],fill='#c3ccd9',width=3)
    for wi,g in groups:
        color='#9eacbc' if g['seam'] or g['event'] else COLORS[g['kind']]
        ps=[xy(p) for p in g['polyline']]
        if len(ps)>1:
            dr.line(ps,fill=color,width=5)
            dr.line([pxy(p) for p in g['polyline']],fill=color,width=3)
            pos=ps[len(ps)//2];label=f"W{wi}G{g['groupId']}"
            # Alternate label offsets to separate concentric-ring labels.
            dx,dy=(10,8) if wi%2 else (10,-29)
            textsize=dr.textbbox((0,0),label,font=font(18));lx=max(40,min(pos[0]+dx,915-textsize[2]));ly=max(207,min(pos[1]+dy,730))
            if len(groups)<=10 or g['groupId']==1:
                dr.rectangle((lx-3,ly-1,lx+textsize[2]+3,ly+25),fill='white')
                dr.text((lx,ly),label,font=font(18),fill=color)
            if not g['closed']:
                for p in [g['start'],g['end']]:
                    x,y=xy(p);dr.ellipse((x-3,y-3,x+3,y+3),fill=color)
    dr.text((40,787),'绿色：皮上边     橙色：侧边     蓝色：内部边     灰虚线：候选母线',font=font(21),fill='#3f5067')
    signatures=[]
    for w in f['wires']:
        signatures.append('W'+str(w['wireId'])+': '+' → '.join({'Skin':'皮','Side':'侧','Internal':'内','Unresolved':'?'}[g['kind']]+str(g['groupId'])+('（闭合）' if g['closed'] else '') for g in w['groups'] if not g['seam'] and not g['event']))
    signature='  |  '.join(signatures)
    if len(signature)>100:signature=f'{len(groups)} 个逻辑组；完整顺序见对应 JSON。'
    dr.text((40,830),signature,font=font(20),fill='#3f5067')
    dr.text((40,874),'颜色为逻辑边与皮的关系；母线尚未确定实际入射方向及工艺。',font=font(19),fill='#6b788d')
    target=path.with_suffix('.png');im.save(target);return target

def main():
    thumbnails=[]
    for mask in [1,3,7,5,4,6,2]:
        if (OUT/'examples'/f'combination-{mask}.json').exists():
            p=draw(mask);im=Image.open(p);im.thumbnail((720,470));thumbnails.append(im)
    sheet=Image.new('RGB',(1440,470*((len(thumbnails)+1)//2)),'#f4f7fb')
    for i,im in enumerate(thumbnails):sheet.paste(im,((i%2)*720,(i//2)*470))
    sheet.save(OUT/'组合总览.png')
    for name,mask,title in [('skin-multiple-loops',1,'同为全皮上：四个闭环'),('three-roles-eight-holes',7,'同为三类组合：八个内部孔'),('nonruled-four-groups',3,'同为皮上＋侧边：样条曲面需检查拟合')]:
        p=OUT/'examples'/f'{name}.json'
        if p.exists():draw(mask,p,title)

if __name__=='__main__':main()
