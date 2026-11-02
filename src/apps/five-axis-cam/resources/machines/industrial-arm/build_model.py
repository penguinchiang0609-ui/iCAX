"""Deterministic authored industrial laser cell meshes. Dimensions below are mm.
URDF and mesh outputs are metres. Dependencies: ../requirements-build.txt.
"""
import math, struct, pathlib, sys, xml.etree.ElementTree as ET
from collections import defaultdict

ROOT=pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT.parent))
from authored_surface import AuthoredSurface
SURFACE=AuthoredSurface()
PARTS=defaultdict(lambda:defaultdict(list))
COLORS={'shell':(.91,.31,.055,1),'dark':(.08,.11,.14,1),'steel':(.54,.61,.65,1),
        'rail':(.75,.8,.83,1),'teal':(.16,.19,.22,1),'black':(.025,.035,.043,1),
        'copper':(.78,.46,.19,1),'glass':(.035,.15,.17,1),'red':(.72,.06,.04,1),
        'amber':(.96,.55,.06,1),'green':(.08,.7,.35,1),'screen':(.07,.22,.28,1)}

def transformed(p,origin,rotation):
    x,y,z=p; rx,ry,rz=rotation
    y,z=y*math.cos(rx)-z*math.sin(rx),y*math.sin(rx)+z*math.cos(rx)
    x,z=x*math.cos(ry)+z*math.sin(ry),-x*math.sin(ry)+z*math.cos(ry)
    x,y=x*math.cos(rz)-y*math.sin(rz),x*math.sin(rz)+y*math.cos(rz)
    return (x+origin[0],y+origin[1],z+origin[2])

def add(link,color,triangles,origin=(0,0,0),rotation=(0,0,0)):
    SURFACE.add(link,color,[tuple(transformed(p,origin,rotation) for p in t) for t in triangles])

def rings_mesh(rings):
    t=[];n=len(rings[0])
    for a,b in zip(rings,rings[1:]):
        for i in range(n): j=(i+1)%n;t.extend([(a[i],a[j],b[j]),(a[i],b[j],b[i])])
    for ring,flip in [(rings[0],True),(rings[-1],False)]:
        center=tuple(sum(v[k] for v in ring)/n for k in range(3))
        for i in range(n):
            tri=(center,ring[i],ring[(i+1)%n]);t.append(tuple(reversed(tri)) if flip else tri)
    return t

def box(link,color,pos,size,bevel=4,rotation=(0,0,0)):
    x,y,z=[v/2 for v in size];b=min(bevel,x*.2,y*.2,z*.2)
    def ring(x,y,z):return [(a,c,z) for a,c in [(-x+b,-y),(x-b,-y),(x,-y+b),(x,y-b),(x-b,y),(-x+b,y),(-x,y-b),(-x,-y+b)]]
    rings=[ring(x-b,y-b,-z),ring(x,y,-z+b),ring(x,y,z-b),ring(x-b,y-b,z)]
    add(link,color,rings_mesh(rings),pos,rotation)

def lathe(link,color,pos,profile,rotation=(0,0,0),segments=64):
    rings=[[(r*math.cos(i*2*math.pi/segments),r*math.sin(i*2*math.pi/segments),z) for i in range(segments)] for z,r in profile]
    add(link,color,rings_mesh(rings),pos,rotation)

def cyl(link,color,pos,r,h,rotation=(0,0,0),segments=48):
    lathe(link,color,pos,[(-h/2,r), (h/2,r)],rotation,segments)

def bolt(link,pos,rotation=(0,0,0),radius=7):
    cyl(link,'black',pos,radius,6,rotation,6)

def tube(link,color,points,radius=9):
    for a,b in zip(points,points[1:]):
        d=[b[i]-a[i] for i in range(3)];length=math.sqrt(sum(v*v for v in d))
        rotation=(0,math.acos(d[2]/length),math.atan2(d[1],d[0]))
        cyl(link,color,tuple((a[i]+b[i])/2 for i in range(3)),radius,length,rotation,12)

def casting(link,start,end,wa,wb,depth):
    d=[end[i]-start[i] for i in range(3)];length=math.sqrt(sum(v*v for v in d));rotation=(0,math.acos(d[2]/length),math.atan2(d[1],d[0]))
    def ring(z,w,h):
        b=24;return [(x,y,z) for x,y in [(-w/2+b,-h/2),(w/2-b,-h/2),(w/2,-h/2+b),(w/2,h/2-b),(w/2-b,h/2),(-w/2+b,h/2),(-w/2,h/2-b),(-w/2,-h/2+b)]]
    add(link,'shell',rings_mesh([ring(0,wa-20,depth-20),ring(20,wa,depth),ring(length-20,wb,depth*.78),ring(length,wb-20,depth*.78-20)]),start,rotation)
    for side in [-1,1]:
        pos=transformed((0,side*(depth/2+2),length*.48),start,rotation)
        box(link,'teal',pos,(wa*.56,8,length*.62),10,rotation)

def bearing(link,pos,radius,length,axis='y'):
    rotation=(math.pi/2,0,0) if axis=='y' else (0,math.pi/2,0) if axis=='x' else (0,0,0)
    lathe(link,'shell',pos,[(-length/2,radius*.84),(-length/2+12,radius),(length/2-12,radius),(length/2,radius*.88)],rotation)
    for sign in [-1,1]:
        face=transformed((0,0,sign*(length/2+3)),pos,rotation)
        cyl(link,'steel',face,radius*.75,8,rotation)
        for i in range(10):
            a=i*math.pi/5;p=transformed((radius*.61*math.cos(a),radius*.61*math.sin(a),sign*(length/2+10)),pos,rotation);bolt(link,p,rotation,6)

# Anchored pedestal, yaw reducer and cast shoulder housing.
box('pedestal','dark',(0,0,35),(640,640,70),28)
lathe('pedestal','shell',(0,0,0),[(70,255),(95,235),(195,195),(230,205)])
for x in [-245,245]:
    for y in [-245,245]:bolt('pedestal',(x,y,78),radius=15)
bearing('turret',(0,0,15),190,55,'z')
casting('turret',(0,0,40),(130,0,210),285,270,240)
box('turret','dark',(-115,-70,125),(180,220,180),20)
for z in range(65,195,18):box('turret','black',(-211,-70,z),(7,170,7),1)

# Six independently driven links. Side covers, reducers and cable conduits
# belong to their own rigid link rather than stretching across moving joints.
bearing('upper_arm',(0,0,0),158,270)
cyl('upper_arm','dark',(0,-192,0),108,115,(math.pi/2,0,0))
casting('upper_arm',(0,0,55),(250,0,620),245,180,205)
box('upper_arm','steel',(75,109,205),(70,8,120),8,(0,.352,0))
tube('upper_arm','black',[(-45,120,70),(0,130,190),(180,130,590),(245,100,655)],18)
bearing('forearm',(0,0,0),125,225)
casting('forearm',(45,0,0),(600,0,50),200,135,175)
cyl('forearm','dark',(0,-170,0),85,100,(math.pi/2,0,0))
tube('forearm','black',[(45,95,35),(160,105,65),(520,90,100),(610,60,85)],15)
bearing('wrist_roll',(70,0,0),82,160,'x')
cyl('wrist_roll','dark',(165,0,0),68,45,(0,math.pi/2,0))
bearing('wrist_pitch',(0,0,0),72,150)
casting('wrist_pitch',(0,0,-30),(0,0,-145),120,95,110)
cyl('wrist_pitch','dark',(0,-110,0),50,55,(math.pi/2,0,0))
bearing('tool',(10,0,0),52,55,'x')
lathe('tool','dark',(0,0,0),[(35,43),(135,43),(145,35)],(0,math.pi/2,0))
lathe('tool','steel',(0,0,0),[(145,35),(195,29),(205,24)],(0,math.pi/2,0))
lathe('tool','copper',(0,0,0),[(205,24),(220,24),(263,6),(270,5)],(0,math.pi/2,0))
box('tool','teal',(85,-45,0),(85,20,50),8)
tube('tool','black',[(65,35,0),(40,65,0),(-10,65,0)],9)

# Dedicated fixture table; its link frame is the tabletop work coordinate.
box('work_table','dark',(0,0,-78),(900,850,124),20)
for x in range(-400,401,100):box('work_table','steel',(x,0,-8),(88,820,16),3)
for x in [-355,355]:
    for y in [-330,330]:
        box('work_table','dark',(x,y,-355),(100,100,570),10)
        box('work_table','steel',(x,y,-637),(160,160,25),6)
    for yy in [-350,350]:bolt('work_table',(x,yy,3),radius=5)
box('cabinet','shell',(0,0,470),(520,650,940),25)
box('cabinet','dark',(0,-335,470),(460,20,860),14)
for z in range(180,400,22):box('cabinet','black',(0,-350,z),(355,9,8),2)
box('cabinet','screen',(0,-354,735),(260,8,155),10)
box('cabinet','steel',(175,-360,560),(20,20,120),4)
cyl('cabinet','red',(155,-360,705),20,20,(math.pi/2,0,0))

POSES={'pedestal':(0,0,0,0,0,0),'turret':(0,0,240,0,0,0),'upper_arm':(130,0,460,0,0,0),
       'forearm':(380,0,1140,0,0,0),'wrist_roll':(1030,0,1190,0,0,0),'wrist_pitch':(1230,0,1190,0,0,0),
       'tool':(1230,0,1030,0,math.pi/2,0),'work_table':(1200,0,650,0,0,0),'cabinet':(-750,-750,0,0,0,0)}
BOXES={
 'pedestal':[((0,0,115),(640,640,230))],
 'turret':[((40,-15,130),(400,300,260))],
 'upper_arm':[((0,-45,0),(320,390,320)),((125,0,350),(215,255,620),(0,.352,0))],
 'forearm':[((0,-40,0),(250,350,250)),((330,0,28),(590,225,190))],
 'wrist_roll':[((95,0,0),(210,170,170))],
 'wrist_pitch':[((0,-20,0),(145,230,145)),((0,0,-100),(125,120,140))],
 'tool':[((75,0,0),(180,135,108)),((172,0,0),(65,70,70)),((237,0,0),(66,48,48))],
 'work_table':[((0,0,-70),(900,850,140))]+[((x,y,-355),(160,160,590)) for x in [-355,355] for y in [-330,330]],
 'cabinet':[((0,-20,470),(520,700,940))],
}
NAMES={'pedestal':'锚固底座','turret':'J1 回转腰座','upper_arm':'J2 大臂','forearm':'J3 小臂','wrist_roll':'J4 腕部回转','wrist_pitch':'J5 腕部俯仰','tool':'J6 激光加工头','work_table':'工件装夹台','cabinet':'控制柜'}

def write_stl(path,triangles):
    with path.open('wb') as f:
        f.write(b'iCAX authored industrial six-axis arm'.ljust(80,b'\0'));f.write(struct.pack('<I',len(triangles)))
        for a,b,c in triangles:
            u=[b[i]-a[i] for i in range(3)];v=[c[i]-a[i] for i in range(3)];n=[u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]];l=math.sqrt(sum(x*x for x in n));n=[x/l if l else 0 for x in n]
            f.write(struct.pack('<12fH',*n,*(x/1000 for p in [a,b,c] for x in p),0))
def pose_text(p):return ' '.join(str(v/1000 if i<3 else v) for i,v in enumerate(p))
sdf=ET.Element('sdf',version='1.7');model=ET.SubElement(sdf,'model',name='iCAX IR-1600 六轴激光加工工作站');ET.SubElement(model,'static').text='false'
PARTS=SURFACE.finish()
(ROOT/'meshes').mkdir(exist_ok=True)
for link,groups in PARTS.items():
    element=ET.SubElement(model,'link',name=NAMES[link]);ET.SubElement(element,'pose',relative_to='__model__').text=pose_text(POSES[link])
    if link=='work_table':ET.SubElement(element,'icax_work_support')
    for color,triangles in groups.items():
        name=f'{link}_{color}.stl';write_stl(ROOT/'meshes'/name,triangles);visual=ET.SubElement(element,'visual',name=f'{link}_{color}')
        ET.SubElement(ET.SubElement(ET.SubElement(visual,'geometry'),'mesh'),'uri').text=f'meshes/{name}'
        material=ET.SubElement(visual,'material');ET.SubElement(material,'diffuse').text=' '.join(map(str,COLORS[color]));ET.SubElement(material,'ambient').text=' '.join(map(str,COLORS[color]))
    for i,b in enumerate(BOXES[link]):
        pos,size,*rot=b;collision=ET.SubElement(element,'collision',name=f'{link}_box_{i+1}');ET.SubElement(collision,'pose').text=pose_text((*pos,*(rot[0] if rot else (0,0,0))))
        ET.SubElement(ET.SubElement(ET.SubElement(collision,'geometry'),'box'),'size').text=' '.join(str(v/1000) for v in size)
    if link=='tool':
        plugin=ET.SubElement(element,'plugin',name='icax_machine_tool',filename='icax.machine.tool');ET.SubElement(plugin,'tcp_position_mm').text='280 0 0';ET.SubElement(plugin,'beam_direction').text='1 0 0'
chain=[('J1','pedestal','turret','0 0 1',170,110),('J2','turret','upper_arm','0 1 0',110,100),('J3','upper_arm','forearm','0 1 0',150,110),('J4','forearm','wrist_roll','1 0 0',180,170),('J5','wrist_roll','wrist_pitch','0 1 0',120,170),('J6','wrist_pitch','tool','1 0 0',360,230)]
for name,parent,child,axis,limit,velocity in chain:
    j=ET.SubElement(model,'joint',name=name,type='revolute');ET.SubElement(j,'parent').text=NAMES[parent];ET.SubElement(j,'child').text=NAMES[child]
    a=ET.SubElement(j,'axis');xyz=ET.SubElement(a,'xyz');xyz.text=axis
    if name=='J3':xyz.set('expressed_in','__model__')
    lim=ET.SubElement(a,'limit');ET.SubElement(lim,'lower').text=str(-math.radians(limit));ET.SubElement(lim,'upper').text=str(math.radians(limit));ET.SubElement(lim,'velocity').text=str(math.radians(velocity));ET.SubElement(lim,'effort').text='1200'
for link in ['pedestal','work_table','cabinet']:
    j=ET.SubElement(model,'joint',name=link+'_mount',type='fixed');ET.SubElement(j,'parent').text='world';ET.SubElement(j,'child').text=NAMES[link]

def center_link_frames():
    """Rebase editable frames while preserving physical geometry and axis lines.

    Link poses are absolute model-space rest poses. Joints have child-relative
    default frames; an axial shift preserves their revolute axis at every q.
    Visual origins retain the exact original STL coordinates and surface mesh.
    """
    joints={j.find('child').text:j for j in model.findall('joint')}
    for link,groups in PARTS.items():
        element=next(e for e in model.findall('link') if e.get('name')==NAMES[link])
        points=[p for triangles in groups.values() for triangle in triangles for p in triangle]
        center=tuple((min(p[k] for p in points)+max(p[k] for p in points))/2 for k in range(3))
        j=joints[NAMES[link]]
        if link=='pedestal':
            continue  # The anchored mechanical root remains unchanged.
        elif j.get('type')=='revolute':
            xyz=j.find('axis/xyz');axis=tuple(float(v) for v in xyz.text.split())
            if xyz.get('expressed_in')=='__model__':
                # Convert the model-space axis into this link's local frame.
                basis=[transformed(tuple(float(i==k) for i in range(3)),(0,0,0),POSES[link][3:]) for k in range(3)]
                axis=tuple(sum(axis[i]*column[i] for i in range(3)) for column in basis)
            projection=sum(center[k]*axis[k] for k in range(3))/sum(v*v for v in axis)
            offset=tuple(projection*v for v in axis)
        else:
            offset=center
        pose=POSES[link];position=transformed(offset,pose[:3],pose[3:])
        element.find('pose').text=pose_text((*position,*pose[3:]))
        for geometry in element.findall('visual')+element.findall('collision'):
            local=geometry.find('pose')
            if local is None:local=ET.SubElement(geometry,'pose');local.text='0 0 0 0 0 0'
            old=[float(v) for v in local.text.split()]
            local.text=' '.join(str(old[k]-offset[k]/1000) if k<3 else str(old[k]) for k in range(6))
        tcp=element.find("plugin[@name='icax_machine_tool']/tcp_position_mm")
        if tcp is not None:
            old=tuple(float(v) for v in tcp.text.split())
            tcp.text=' '.join(str(old[k]-offset[k]) for k in range(3))

center_link_frames()
ET.indent(sdf);ET.ElementTree(sdf).write(ROOT/'IR-1600.sdf',encoding='utf-8',xml_declaration=True)
print(f'{len(PARTS)} links; {sum(len(g) for g in PARTS.values())} meshes; {sum(len(t) for g in PARTS.values() for t in g.values())} triangles; 6 rotary axes')

