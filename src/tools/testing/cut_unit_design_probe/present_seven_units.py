"""Render the generated units from their read-back meshes, index the BREP files and zip them."""
import hashlib
import json
from pathlib import Path
import zipfile
import numpy as np
from PIL import Image,ImageDraw,ImageFont,ImageColor
from materialize_seven_units import DEST,OUT
from draw_face_combinations import renderer

FONT='C:/Windows/Fonts/msyh.ttc'
NAMES={1:'皮上',3:'皮上＋侧边',7:'皮上＋侧边＋内部',5:'皮上＋内部',4:'内部',6:'侧边＋内部',2:'侧边'}
FORMS={'projected':'变深度条带','strip':'条带','periodic':'周期环带','fan':'端点退化扇形'}
SHORT={1:'02-05-052.step · F1',3:'电子表扶手管 · F35',7:'008.step · F107',5:'76.step · F11',4:'008.step · F11',6:'C-7-2 HW150x150 · F1',2:'AB52 小边管右 · F31'}

def font(size):return ImageFont.truetype(FONT,size)

def tint(color,alpha):
    rgb=ImageColor.getrgb(color);return tuple(round(255+(v-255)*alpha) for v in rgb)

def projection(source,box):
    points=np.array([p for w in source['faces'][0]['wires'] for g in w['groups'] for p in g['polyline']]);return renderer(points,box)

def paint_unit(draw,mesh,xy,color,lines):
    for tri in mesh['triangles']:draw.polygon([xy(mesh['vertices'][i]) for i in tri],fill=tint(color,.26))
    for a,b in lines:draw.line([xy(a),xy(b)],fill=tint(color,.72),width=2)
    for edge in mesh['edges']:
        if len(edge)>1:draw.line([xy(p) for p in edge],fill=color,width=3)

def draw_case(row):
    mask=row['combination'];folder=Path(row['folder']);source=json.loads((OUT/'examples'/f'combination-{mask}.json').read_text(encoding='utf-8'))
    source_mesh=json.loads((OUT/'examples'/f'combination-{mask}.mesh.json').read_text(encoding='utf-8'))
    mesh=json.loads((folder/'unit-01.mesh.json').read_text(encoding='utf-8'));u=row['unit']
    im=Image.new('RGB',(1600,980),'#f7f9fc');dr=ImageDraw.Draw(im)
    dr.text((40,26),f"{NAMES[mask]}   ·   1 张 face → 1 个切割单元",font=font(32),fill='#19334d')
    dr.text((40,80),f"{row['sourceFile']}   原始 F{row['faceId']}",font=font(22),fill='#53677c')
    for box in [(30,140,780,820),(820,140,1570,820)]:dr.rounded_rectangle(box,radius=12,fill='white',outline='#dde4ed',width=2)
    dr.text((54,158),'原始 face',font=font(22),fill='#53677c')
    dr.text((844,158),f"U01  {FORMS[row['mode']]}   ·   独立 BREP",font=font(22),fill=row['color'])
    axy=projection(source,(95,250,715,745));bxy=projection(source,(885,250,1505,745))
    for tri in source_mesh['triangles']:dr.polygon([axy(source_mesh['vertices'][i]) for i in tri],fill='#e5e9ef')
    for w in source['faces'][0]['wires']:
        for g in w['groups']:
            if len(g['polyline'])>1:dr.line([axy(p) for p in g['polyline']],fill='#6e7f91',width=3)
    paint_unit(dr,mesh,bxy,row['color'],u['motherLines'])
    dr.text((45,849),f"源面面积 {u['originalAreaMm2']:.9g} mm²     单元面积 {u['unitAreaMm2']:.9g} mm²",font=font(23),fill='#344d65')
    dr.text((45,891),'色块＝单元区域；细线＝真实扫掠面的直母线。两图同方向、同比例。',font=font(22),fill='#53677c')
    dr.text((45,934),'保留原模型坐标；本轮未选择摆放、入射方向或半切透工艺。',font=font(19),fill='#677b8f')
    im.save(folder/'comparison.png')
    # The overview uses the same read-back mesh without cropping the underlying geometry.
    card=Image.new('RGB',(780,490),'white');cd=ImageDraw.Draw(card)
    cd.text((22,15),f"{NAMES[mask]}  ·  U01",font=font(25),fill=row['color'])
    cd.text((22,56),SHORT[mask],font=font(18),fill='#52677a')
    xy=projection(source,(65,125,715,392));paint_unit(cd,mesh,xy,row['color'],u['motherLines'])
    cd.text((22,435),f"1 个单元  ·  {FORMS[row['mode']]}  ·  {u['unitAreaMm2']:.6g} mm²",font=font(21),fill='#334d65')
    return card

def main():
    rows=json.loads((DEST/'manifest.json').read_text(encoding='utf-8'));assert len(rows)==7 and all(r['status']=='passed' for r in rows)
    sheet=Image.new('RGB',(1620,2130),'#f3f6fa');dr=ImageDraw.Draw(sheet)
    dr.text((30,18),'七个实样 · 实际生成的切割单元',font=font(31),fill='#19334d')
    dr.text((30,65),'共 7 个单元｜颜色区分单元｜细线表示直母线｜各例独立适配比例',font=font(21),fill='#53677c')
    for i,r in enumerate(rows):sheet.paste(draw_case(r),(20+(i%2)*800,115+(i//2)*500))
    sheet.save(DEST/'切割单元总览.png')
    lines=['# 七个实样的切割单元 BREP','','本轮将七个实样的轨道配对实际构造成直母线曲面，再生成并导出单元 face。七例各得到一个单元，共 7 个。没有人为增加分割线。原始坐标保留。',
        '', '每个 `unit-01.brep` 是生成的独立单元，保留直母线参数化；`source-face.brep` 是原面备查。BREP 不携带显示颜色，对应的 `units-colored.step` 使用已经通过双向覆盖检查的原始平面裁剪区域保存颜色，验证了重新导入后的面积、面数和颜色。这样避免小弓形扇面两个相邻退化参数边在 STEP 回读时造成面积缩水；并未改变单元 BREP。图片由输出 BREP 重新读取后的网格绘制。',
        '', '[查看彩色总览](切割单元总览.png)', '', '| 组合 | 来源 | 单元数 | 独立单元 | 彩色模型 | 原面/单元对照 |', '|---|---|---:|---|---|---|']
    for r in rows:
        folder=f"combination-{r['combination']}";lines.append(f"| {NAMES[r['combination']]} | {r['sourceFile']} F{r['faceId']} | 1 | [BREP]({folder}/unit-01.brep) | [STEP]({folder}/units-colored.step) | [对照图]({folder}/comparison.png) |")
    lines+=['','生成方式：U 形面按原内侧连续曲线向底边投影，保留斜壁和圆角；四个常规条带由原始轨道生成；环带按两个闭环生成周期单元；弓形小面由公共端点和原弧线生成扇形单元。',
        '', '这七个选例的几何区域均可由一个直母线单元覆盖，因此图中每个实样都是一种填充色。侧边和内部边的含义沿用上轮分析；几何单元生成不替代摆放、入口可达性及半切透工艺判断。',
        '', '核验：所有单元 BREP 有效，BREP/STEP 回读通过，单元曲面沿母线方向的次数均为 1。原面减单元、单元减原面的布尔剩余面积均为 0（布尔容差 1e-7 mm）；最大积分面积差 %.9g mm²；25×33 点的最大单元到原面距离 %.9g mm。面积验收阈值为 max(1e-7 mm², 原面面积×1e-8)。这些结果不属于机床加工验收。'%(max(abs(r['unit']['unitAreaMm2']-r['unit']['originalAreaMm2']) for r in rows),max(r['unit']['maxSampledDistanceMm'] for r in rows)),
        '', '[生成清单与轨道来源](manifest.json) · [核验结果](validation.json)']
    (DEST/'README.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    assert all(r['unit']['vDegree']==1 and r['unit']['missingAreaMm2']==0 and r['unit']['extraAreaMm2']==0 for r in rows)
    assert all(hashlib.sha256(Path(r['source']).read_bytes()).hexdigest()==r['sourceSha256'] for r in rows)
    validation={'status':'passed','exampleCount':7,'unitCount':7,'singleFaceBreps':7,'coloredStepReadbacks':7,
        'maximumAreaDifferenceMm2':max(abs(r['unit']['unitAreaMm2']-r['unit']['originalAreaMm2']) for r in rows),
        'maximumSampledDistanceMm':max(r['unit']['maxSampledDistanceMm'] for r in rows),'sourceFilesUnchanged':True,
        'rulingDegree':1,'boolFuzzyToleranceMm':1e-7,'geometryDisplay':'readback BREP meshes; native CAD viewer not exercised'}
    (DEST/'validation.json').write_text(json.dumps(validation,ensure_ascii=False,indent=2),encoding='utf-8')
    archive=DEST.parent/'seven-cut-units.zip'
    with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
        for p in DEST.rglob('*'):
            if p.is_file():z.write(p,p.relative_to(DEST))
    print(json.dumps(validation,ensure_ascii=False));print(str(archive))

if __name__=='__main__':main()
