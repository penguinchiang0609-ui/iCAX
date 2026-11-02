"""Independent geometry/axis audit of all five installed gantry layouts.
Uses only the Python standard library, never calls the asset generator.
"""
from pathlib import Path
import json
import math
import struct
import xml.etree.ElementTree as ET

ROOT=Path(__file__).resolve().parents[4]
ASSETS=ROOT/'src/apps/five-axis-cam/resources/machines/gantry-heads'


def vec(text):return [float(v) for v in text.split()]
def ident():return [[float(r==c) for c in range(4)] for r in range(4)]
def mul(a,b):return [[sum(a[r][k]*b[k][c] for k in range(4)) for c in range(4)] for r in range(4)]
def point(m,p):return [sum(m[r][k]*p[k] for k in range(3))+m[r][3] for r in range(3)]
def inverse(m):
    n=ident()
    for r in range(3):
        for c in range(3):n[r][c]=m[c][r]
        n[r][3]=-sum(n[r][k]*m[k][3] for k in range(3))
    return n
def rotation(axis,q):
    x,y,z=axis;c=math.cos(q);s=math.sin(q);d=1-c;m=ident()
    m[:3]=[[c+x*x*d,x*y*d-z*s,x*z*d+y*s,0],[y*x*d+z*s,c+y*y*d,y*z*d-x*s,0],[z*x*d-y*s,z*y*d+x*s,c+z*z*d,0]]
    return m
def origin(node):
    if node is None:return ident()
    x,y,z=vec(node.get('xyz','0 0 0'));a,b,c=vec(node.get('rpy','0 0 0'))
    m=mul(mul(rotation((0,0,1),c),rotation((0,1,0),b)),rotation((1,0,0),a))
    for k,v in enumerate((x,y,z)):m[k][3]=v
    return m
def triangles(path):
    data=path.read_bytes();count=struct.unpack_from('<I',data,80)[0]
    assert len(data)==84+50*count,str(path)
    for n in range(count):
        row=struct.unpack_from('<12fH',data,84+50*n)
        yield [row[k:k+3] for k in (3,6,9)]


def audit(spec):
    folder=ASSETS/spec['model'];robot=ET.parse(folder/(spec['model']+'.urdf')).getroot()
    links={n.get('name'):n for n in robot.findall('link')};parents={j.find('child').get('link'):j for j in robot.findall('joint')}
    axes={j.get('name'):j for j in parents.values() if j.get('type')!='fixed'}
    assert len(axes)==5
    for name,j in axes.items():assert vec(j.find('axis').get('xyz'))==list({'X':(1,0,0),'Y':(0,1,0),'Z':(0,0,1),'A':(1,0,0),'B':(0,1,0),'C':(0,0,1)}[name])
    def branch(name):
        result=[]
        while name in parents:
            j=parents[name]
            if j.get('type') in ('revolute','continuous'):result.append(j.get('name'))
            name=j.find('parent').get('link')
        return sorted(result)
    assert branch(spec['toolLink'])==sorted(spec['headAxes'])
    assert branch(spec['supportLink'])==sorted(spec['workpieceAxes'])
    assert len([l for l in links.values() if l.find('icax_tool') is not None])==1
    assert len([l for l in links.values() if l.find('icax_work_support') is not None])==1
    if spec['kind']=='AB':
        assert spec['headRoot']=='AB 刀头安装座'
        assert parents[spec['headRoot']].get('type')=='fixed'
        assert axes['B'].find('parent').get('link')==spec['headRoot']
        assert axes['A'].find('parent').get('link')==axes['B'].find('child').get('link')
        assert not any(j.get('name')=='C' for j in parents.values())
        assert vec(axes['A'].find('origin').get('rpy','0 0 0'))==[0,0,0]
        assert vec(axes['B'].find('origin').get('rpy','0 0 0'))==[0,0,0]
        # The compact head has a fixed B motor and a side-driven A motor.
        # Both physical shafts must share their declared joint centre/axis.
        def mesh_bounds(link,name):
            visual=link.find(f"visual[@name='{name}']");assert visual is not None,name
            points=[p for t in triangles(folder/visual.find('geometry/mesh').get('filename')) for p in t]
            return [[min(p[k] for p in points) for k in range(3)],[max(p[k] for p in points) for k in range(3)]]
        fixed=links[spec['headRoot']];carrier=links['B 回转叉架'];tool=links[spec['toolLink']]
        stator=mesh_bounds(fixed,'ab_B_stator');shaft=mesh_bounds(carrier,'ab_B_shaft')
        for bounds in (stator,shaft):
            assert all(abs(bounds[0][k]+bounds[1][k])<1e-7 for k in (0,2))
        assert min(stator[1][1],shaft[1][1])>max(stator[0][1],shaft[0][1])
        pivot=vec(axes['A'].find('origin').get('xyz'))
        bearing=mesh_bounds(carrier,'ab_A_bearing');axle=mesh_bounds(tool,'ab_A_shaft')
        for k in (1,2):assert abs((bearing[0][k]+bearing[1][k])/2-pivot[k])<1e-7
        for k in (1,2):assert abs((axle[0][k]+axle[1][k])/2)<1e-7
        assert min(bearing[1][0],axle[1][0]+pivot[0])>max(bearing[0][0],axle[0][0]+pivot[0])
        assert pivot[1]>.12 # The tool is forward of the ram, not trapped in it.
        assert not any('ab_fork_' in v.get('name','') for v in carrier.findall('visual'))
        fixed_bounds=[mesh_bounds(fixed,v.get('name')) for v in fixed.findall('visual')]
        assert max(b[1][2] for b in fixed_bounds)<.21
        for name,limits in (('A',(-46,50)),('B',(-50,50))):
            declared=axes[name].find('limit')
            assert all(abs(math.degrees(float(declared.get(key)))-value)<1e-8 for key,value in zip(('lower','upper'),limits))
    chain=[];name=spec['toolLink']
    while True:
        chain.append(name)
        if name==spec['headRoot']:break
        name=parents[name].find('parent').get('link')
    checked=0;tip=[]
    for name in chain:
        link=links[name];boxes=[]
        for b in link.findall('collision'):
            size=vec(b.find('geometry/box').get('size'));assert all(v>0 for v in size)
            boxes.append((inverse(origin(b.find('origin'))),[v/2+.00002 for v in size]))
        assert boxes
        for visual in link.findall('visual'):
            mesh=visual.find('geometry/mesh');path=folder/mesh.get('filename');scale=vec(mesh.get('scale','1 1 1'));m=origin(visual.find('origin'))
            for triangle in triangles(path):
                points=[point(m,[v[k]*scale[k] for k in range(3)]) for v in triangle];checked+=3
                # One convex box must contain the WHOLE triangle, rather than
                # three vertices distributed across separate boxes with a gap.
                assert any(all(abs(x)<=h[k] for p in points for k,x in enumerate(point(inv,p))) for inv,h in boxes),(spec['model'],name,points)
                if name==spec['toolLink']:tip.extend(points)
    tcp=[v/1000 for v in vec(links[spec['toolLink']].find('icax_tool').get('tcp_mm'))]
    z=min(p[2] for p in tip);assert abs(z-tcp[2])<.00002
    end=[p for p in tip if abs(p[2]-z)<.000001]
    for k in (0,1):assert abs((min(p[k] for p in end)+max(p[k] for p in end))/2-tcp[k])<.00002
    # Both physical branches are traversed in independent FK for each pose.
    def forward(values):
        world={}
        def visit(name):
            if name in world:return world[name]
            if name not in parents:world[name]=ident();return world[name]
            j=parents[name];m=origin(j.find('origin'));q=values.get(j.get('name'),0)
            if j.get('type')=='prismatic':
                t=ident()
                for k,v in enumerate(vec(j.find('axis').get('xyz'))):t[k][3]=v*q
                m=mul(m,t)
            elif j.get('type') in ('revolute','continuous'):m=mul(m,rotation(vec(j.find('axis').get('xyz')),q))
            world[name]=mul(visit(j.find('parent').get('link')),m);return world[name]
        for name in links:visit(name)
        return world
    for phase in range(9):
        values={name:(.1*phase if name in ('A','B','C') else 0) for name in axes}
        world=forward(values);relative=mul(inverse(world[spec['supportLink']]),world[spec['toolLink']]);p=point(relative,tcp)
        assert all(math.isfinite(v) for v in p)
    return dict(model=spec['model'],headAxes=spec['headAxes'],workpieceAxes=spec['workpieceAxes'],headParts=len(chain),coveredHeadVertices=checked,physicalTcpMm=[v*1000 for v in tcp])


if __name__=='__main__':
    results=[audit(spec) for spec in json.loads((ASSETS/'catalog.json').read_text(encoding='utf-8'))]
    output=ROOT/'output/tests/gantry-heads-20261101';output.mkdir(parents=True,exist_ok=True)
    (output/'asset-audit.json').write_text(json.dumps(dict(passed=True,cases=results),ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(results,ensure_ascii=False))
