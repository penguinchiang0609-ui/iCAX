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
COLORS={'shell':(.79,.82,.84,1),'dark':(.08,.11,.14,1),'steel':(.54,.61,.65,1),
        'rail':(.75,.8,.83,1),'teal':(.035,.39,.43,1),'black':(.025,.035,.043,1),
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

# A welded bed, machined rail seats, levelling feet and segmented service panels.
for y in [-1060,1060]:
    box('base','dark',(-430,y,-390),(4300,260,730),28)
    box('base','shell',(-430,y,660),(4300,235,210),18)
    box('base','steel',(-430,y,778),(3980,155,28),3)
    for x in [-2300,-1550,-800,0,800,1500]:
        box('base','shell',(x,y,265),(190,220,600),16)
    for yy in [y-47,y+47]:
        box('base','rail',(-430,yy,801),(3900,24,28),2)
        for x in range(-2330,1471,150):bolt('base',(x,yy,818),radius=4)
    for x in [-1450,-700,0,700,1450]:
        box('base','shell',(x,y-132 if y<0 else y+132,-355),(650,12,530),9)
        box('base','teal',(x,y-140 if y<0 else y+140,-520),(650,8,55),1)
    for x in [-1450,0,1450]:
        cyl('base','steel',(x,y,-790),40,80)
        cyl('base','black',(x,y,-845),85,35)
for x in [-1550,1550]:box('base','dark',(x,0,-490),(250,2000,330),20)
box('base','dark',(0,0,-705),(3300,1800,90),12)
# Table frame and replaceable T-slot fixture rails, extraction tray.
box('work_table','dark',(0,0,-175),(3120,1580,220),24)
for y in [-730,730]:box('work_table','steel',(0,y,-38),(3020,100,75),4)
for x in range(-1450,1451,100):
    box('work_table','steel',(x,0,-43),(72,1440,74),3)
    box('work_table','rail',(x,0,-3),(70,1440,6),1)
    for y in [-640,640]:bolt('work_table',(x,y,3),radius=5)
for x in [-1300,1300]:
    for y in [-580,580]:box('work_table','dark',(x,y,-450),(140,140,420),8)
# X travelling portal: feet on linear guide blocks, tapered uprights and crossbeam.
for y in [-1060,1060]:
    box('x_bridge','dark',(0,y,850),(480,215,75),6)
    for x in [-155,155]:
        box('x_bridge','teal',(x,y,830),(125,140,45),4)
        for yy in [y-65,y+65]:bolt('x_bridge',(x,yy,894),radius=8)
    box('x_bridge','shell',(0,y,1115),(350,260,520),30)
    box('x_bridge','teal',(-183,y,1120),(12,220,400),8)
    box('x_bridge','dark',(180,y,1000),(150,170,230),12)
    for z in range(930,1090,25):box('x_bridge','black',(260,y,z),(5,135,8),1)
box('x_bridge','shell',(0,0,1450),(360,2420,300),26)
box('x_bridge','teal',(-190,0,1470),(12,2300,125),5)
box('x_bridge','dark',(200,0,1460),(50,2250,260),5)
for z in [1380,1540]:
    box('x_bridge','rail',(235,0,z),(22,2130,26),2)
    for y in range(-1000,1001,125):bolt('x_bridge',(251,y,z),(0,math.pi/2,0),5)
# Cable carrier along the rear of the Y travel.
for y in range(-1010,401,37):
    box('x_bridge','black',(-140,y,1640),(125,30,52),4)
    box('x_bridge','steel',(-140,y,1668),(105,9,5),1)
# Y saddle, rails and servo cover, Z ram moves through the saddle.
box('y_carriage','dark',(266,0,1455),(90,370,360),14)
box('y_carriage','shell',(335,0,1400),(110,350,450),16)
for y in [-120,120]:box('y_carriage','rail',(397,y,1400),(20,32,420),2)
box('y_carriage','dark',(355,0,1695),(190,230,140),14)
for y in range(-84,85,21):box('y_carriage','black',(455,y,1695),(6,9,100),1)
# Ram local origin at (430,0,650). C-A offset 220 + A-TCP offset 220 mm.
box('z_slide','shell',(0,0,760),(180,230,1600),16)
box('z_slide','teal',(96,0,765),(8,180,1420),3)
for y in [-90,90]:box('z_slide','rail',(-98,y,760),(15,25,1540),2)
box('z_slide','dark',(0,0,-3),(230,260,80),13)
for z in range(0,1500,35):box('z_slide','black',(-113,0,z),(16,190,10),2)
for x in [-80,80]:
    for y in [-100,100]:bolt('z_slide',(x,y,-48),radius=7)
# C direct-drive rotary bearing and a real fork around the A-axis trunnions.
lathe('c_yoke','dark',(0,0,0),[(-100,103),(-90,113),(-40,113),(-30,102),(-10,102),(0,94)])
lathe('c_yoke','steel',(0,0,0),[(-110,99),(-100,104),(-90,104)])
for i in range(12):
    a=i*math.pi/6;bolt('c_yoke',(94*math.cos(a),94*math.sin(a),-112),radius=5)
for x in [-105,105]:
    box('c_yoke','shell',(x,0,-175),(60,160,150),18)
    cyl('c_yoke','dark',(x,0,-220),70,75,(0,math.pi/2,0))
    cyl('c_yoke','steel',(x+(-40 if x<0 else 40),0,-220),52,8,(0,math.pi/2,0))
    for i in range(8):
        a=i*math.pi/4;bolt('c_yoke',(x+(-46 if x<0 else 46),40*math.cos(a),-220+40*math.sin(a)),(0,math.pi/2,0),4)
# A head local origin at the trunnion centre; TCP is 220 mm below it.
lathe('laser_head','dark',(0,0,0),[(45,38),(35,48),(-65,48),(-75,39)])
lathe('laser_head','shell',(0,0,0),[(-75,43),(-85,43),(-140,35),(-153,29)])
lathe('laser_head','steel',(0,0,0),[(-150,30),(-158,34),(-175,34),(-180,25)])
lathe('laser_head','copper',(0,0,0),[(-180,24),(-188,24),(-214,7),(-220,5)])
cyl('laser_head','black',(0,0,-219.5),2.5,1)
for z in [-90,-100,-110,-120]:lathe('laser_head','steel',(0,0,0),[(z,43),(z-2,43)])
for x in [-65,65]:cyl('laser_head','dark',(x,0,0),36,55,(0,math.pi/2,0))
box('laser_head','teal',(0,51,-10),(52,14,80),6)
cyl('laser_head','copper',(25,0,47),12,24)
tube('laser_head','black',[(25,0,58),(35,0,100),(65,0,125),(95,0,110)],9)
tube('laser_head','teal',[(-30,20,35),(-60,25,70),(-62,35,105)],5)
# Cutaway enclosure: rear and end panels remain; front/roof removed for demonstration.
for y in [1310]:
    for x in [-2220,-1480,-740,0,740,1480]:
        box('enclosure','dark',(x,y,420),(735,70,2450),10)
        box('enclosure','shell',(x,y-40,390),(701,12,2350),9)
        box('enclosure','teal',(x,y-49,1450),(701,6,150),2)
for x in [-2700,1840]:
    box('enclosure','dark',(x,0,-550),(80,2580,500),10)
    box('enclosure','shell',(x+(-45 if x<0 else 45),0,-550),(12,2500,420),8)
    for y in [-1220,1220]:box('enclosure','shell',(x,y,450),(150,170,2500),18)
    box('enclosure','shell',(x,0,1710),(170,2600,160),20)
box('enclosure','shell',(-430,1310,1710),(4540,160,160),20)
# External electrical/laser cabinet, louvres, service door and pendant HMI.
box('cabinet','dark',(2230,900,0),(620,830,1630),25)
box('cabinet','shell',(2230,465,0),(570,30,1540),16)
for z in range(-550,-150,35):box('cabinet','black',(2230,444,z),(440,8,13),2)
box('cabinet','black',(2455,437,280),(20,18,170),4)
for z in [-500,500]:box('cabinet','steel',(1930,450,z),(20,30,70),3)
box('cabinet','teal',(2230,440,650),(520,10,85),3)
cyl('cabinet','steel',(2230,900,-840),70,50)
box('console','dark',(2070,-1100,-220),(110,110,1250),12)
box('console','dark',(2070,-1100,-850),(500,400,45),12)
box('console','shell',(2070,-1100,450),(540,160,520),22)
box('console','black',(2070,-1185,490),(455,14,340),12)
box('console','screen',(2070,-1194,500),(410,5,285),4)
for x in [1900,1980,2060,2140,2220]:box('console','steel',(x,-1195,295),(42,12,22),3)
cyl('console','amber',(2260,-1198,320),27,12,(math.pi/2,0,0))
cyl('console','red',(2260,-1212,320),19,20,(math.pi/2,0,0))
cyl('enclosure','steel',(1770,1250,1890),18,210)
for z,c in [(1815,'green'),(1870,'amber'),(1925,'red')]:cyl('enclosure',c,(1770,1250,z),42,48)

def write_stl(path,triangles):
    with path.open('wb') as f:
        f.write(b'iCAX industrial laser cell - authored geometry'.ljust(80,b'\0'));f.write(struct.pack('<I',len(triangles)))
        for a,b,c in triangles:
            u=[b[i]-a[i] for i in range(3)];v=[c[i]-a[i] for i in range(3)]
            n=[u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]];l=math.sqrt(sum(x*x for x in n))
            n=[x/l if l else 0 for x in n];f.write(struct.pack('<12fH',*n,*(x/1000 for p in [a,b,c] for x in p),0))

# Authored collision boxes, independent from the display tessellation.
# OwnMesh uses the complete laser_head visual triangles for the narrow phase;
# its boxes only reject distant pairs. Boxes mode explicitly uses these boxes
# as the collision shape. Other machine links retain their authored boxes.
# Split hollow structures so the machining space remains open.
COLLIDERS={
 'base':[((-430,y,-5),(4300,280,1740)) for y in (-1060,1060)]+[((0,0,-490),(3350,1850,550))],
 'work_table':[((0,0,-85),(3120,1580,180))]+[((x,y,-450),(140,140,420)) for x in (-1300,1300) for y in (-580,580)],
 'x_bridge':[((0,y,1095),(530,280,570)) for y in (-1060,1060)]+[((20,0,1450),(490,2420,310)),((-140,-300,1640),(130,1440,65))],
 'y_carriage':[((310,0,1400),(200,370,450)),((355,0,1695),(210,230,140))],
 'z_slide':[((-10,0,760),(240,230,1600)),((0,0,-3),(230,260,95))],
 'c_yoke':[((0,0,-55),(226,226,110))]+[((x,0,-195),(110,160,200)) for x in (-105,105)],
 # Conservative union covers the complete authored laser-head exterior. The
 # copper nozzle is split at z=-214 mm; 0.001 mm per side covers STL float32
 # coordinates and seams without replacing its tip with the whole cone box.
 'laser_head':[((0,0,-15),(185.002,116.002,120.002)),((0,0,-115),(88.002,88.002,80.002)),((0,0,-166),(68.002,68.002,30.002)),((0,0,-217),(14.002,14.002,6.002)),((0,0,-197),(48.002,48.002,34.002)),((19.0485,25,89.75),(171.905,90.002,89.502))],
 'enclosure':[((-430,1310,450),(4540,100,2500))]+[((x,y,450),(150,170,2500)) for x in (-2700,1840) for y in (-1220,1220)]+[((x,0,1710),(170,2600,160)) for x in (-2700,1840)],
 'cabinet':[((2230,880,0),(620,870,1680))],
 'console':[((2070,-1100,-220),(110,110,1250)),((2070,-1100,-850),(500,400,45)),((2070,-1130,450),(540,240,520))],
}
COLLIDER_NAMES={'laser_head':['laser_head_box_1','laser_head_box_2','laser_head_box_3','laser_nozzle_tip_slab','laser_nozzle_upper_slab','laser_head_box_5']}
robot=ET.Element('robot',name='iCAX LC-3015 五轴激光加工中心')
PARTS=SURFACE.finish()
(ROOT/'meshes').mkdir(exist_ok=True)
for link,groups in PARTS.items():
    element=ET.SubElement(robot,'link',name=link)
    if link=='work_table':
        ET.SubElement(element,'icax_work_support')
    if link=='laser_head':
        element.append(ET.Comment(' Authored physical nozzle. CAM follow height is applied separately. '))
        ET.SubElement(element,'icax_tool',name='LC-3015 物理喷嘴',type='laser_cutting_head',tcp_mm='0 0 -220',beam='0 0 -1')
    for color,triangles in groups.items():
        name=f'{link}_{color}.stl';write_stl(ROOT/'meshes'/name,triangles)
        visual=ET.SubElement(element,'visual',name=f'{link}_{color}')
        ET.SubElement(ET.SubElement(visual,'geometry'),'mesh',filename=f'meshes/{name}')
        ET.SubElement(ET.SubElement(visual,'material',name=color),'color',rgba=' '.join(map(str,COLORS[color])))
    for i,(position,size) in enumerate(COLLIDERS[link]):
        name=COLLIDER_NAMES[link][i] if link in COLLIDER_NAMES else f'{link}_box_{i+1}'
        collision=ET.SubElement(element,'collision',name=name)
        ET.SubElement(collision,'origin',xyz=' '.join(str(x/1000) for x in position))
        ET.SubElement(ET.SubElement(collision,'geometry'),'box',size=' '.join(str(x/1000) for x in size))

def joint(name,parent,child,kind='fixed',origin=(0,0,0),axis=(0,0,1),limits=(0,0),velocity=1,home=None):
    j=ET.SubElement(robot,'joint',name=name,type=kind)
    # Home uses the same URDF metres/radians as this joint's limits.
    if home is not None:ET.SubElement(j,'icax_home',position=str(home))
    ET.SubElement(j,'parent',link=parent);ET.SubElement(j,'child',link=child)
    ET.SubElement(j,'origin',xyz=' '.join(str(x/1000) for x in origin))
    if kind!='fixed':
        ET.SubElement(j,'axis',xyz=' '.join(map(str,axis)))
        ET.SubElement(j,'limit',lower=str(limits[0]),upper=str(limits[1]),velocity=str(velocity),effort='5000')
joint('table_mount','base','work_table')
joint('X','base','x_bridge','prismatic',origin=(-430,0,0),axis=(1,0,0),limits=(-1.5,1.5),velocity=1.2)
joint('Y','x_bridge','y_carriage','prismatic',axis=(0,1,0),limits=(-.75,.75),velocity=1.2)
joint('Z','y_carriage','z_slide','prismatic',origin=(430,0,650),limits=(-.45,.15),velocity=.8,home=.15)
joint('C','z_slide','c_yoke','revolute',limits=(-math.pi,math.pi),velocity=1.5)
joint('A','c_yoke','laser_head','revolute',origin=(0,0,-220),axis=(1,0,0),limits=(-2.09439510239,2.09439510239),velocity=1.5)
joint('enclosure_mount','base','enclosure');joint('cabinet_mount','base','cabinet');joint('console_mount','base','console')

def center_link_frames():
    """Place editable frames near their link without moving any physical part.

    A revolute frame may move only along its axis: translating it sideways
    changes the mechanism. Fixed/prismatic frames may use the full mesh center.
    Keep STL coordinates intact and compensate through the visual origins.
    """
    joints={j.find('child').get('link'):j for j in robot.findall('joint')}
    offsets={}
    for link,groups in PARTS.items():
        points=[p for triangles in groups.values() for triangle in triangles for p in triangle]
        center=tuple((min(p[k] for p in points)+max(p[k] for p in points))/2 for k in range(3))
        j=joints.get(link)
        if j is None:
            offsets[link]=(0,0,0)  # Preserve the machine root/work coordinate.
        elif j.get('type') in ('revolute','continuous'):
            axis=tuple(float(v) for v in j.find('axis').get('xyz').split())
            projection=sum(center[k]*axis[k] for k in range(3))/sum(v*v for v in axis)
            offsets[link]=tuple(projection*v for v in axis)
        else:
            offsets[link]=center
    for element in robot.findall('link'):
        offset=offsets[element.get('name')]
        if not any(offset):continue
        for geometry in element.findall('visual')+element.findall('collision'):
            origin=geometry.find('origin')
            if origin is None:origin=ET.SubElement(geometry,'origin')
            old=tuple(float(v) for v in origin.get('xyz','0 0 0').split())
            origin.set('xyz',' '.join(str(old[k]-offset[k]/1000) for k in range(3)))
        tool=element.find('icax_tool')
        if tool is not None:
            old=tuple(float(v) for v in tool.get('tcp_mm').split())
            tool.set('tcp_mm',' '.join(str(old[k]-offset[k]) for k in range(3)))
    for j in robot.findall('joint'):
        parent=offsets[j.find('parent').get('link')];child=offsets[j.find('child').get('link')]
        origin=j.find('origin');old=tuple(float(v) for v in origin.get('xyz').split())
        rotation=tuple(float(v) for v in origin.get('rpy','0 0 0').split())
        child=transformed(child,(0,0,0),rotation)
        # O' = T(-parent_offset) O T(child_offset), in URDF metres.
        origin.set('xyz',' '.join(str(old[k]+(child[k]-parent[k])/1000) for k in range(3)))

center_link_frames()
names={'base':'床身与导轨','work_table':'工作台','x_bridge':'X 龙门横梁','y_carriage':'Y 滑鞍','z_slide':'Z 滑枕','c_yoke':'C 回转叉架','laser_head':'A 激光切割头','enclosure':'外罩（剖视）','cabinet':'电气柜','console':'操作台'}
for element in robot.findall('link'):element.set('name',names[element.get('name')])
for element in robot.findall('joint'):
    for tag in ['parent','child']:
        node=element.find(tag);node.set('link',names[node.get('link')])
ET.indent(robot);ET.ElementTree(robot).write(ROOT/'LC-3015.urdf',encoding='utf-8',xml_declaration=True)
print(f'{len(PARTS)} links; {sum(len(g) for g in PARTS.values())} meshes; {sum(len(t) for g in PARTS.values() for t in g.values())} triangles')
