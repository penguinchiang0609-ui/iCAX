"""Authored RC-1000 split-chain five-axis laser centre; build dimensions are mm.

XYZ+B belongs to the tool; C belongs to the chuck. STL/URDF output uses metres.
Install ../requirements-build.txt. Each rigid body's exterior is Boolean-unioned
before splitting by surface material. This is an original demonstration asset.
"""
import math
import pathlib
import struct
import sys
import xml.etree.ElementTree as ET

ROOT = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT.parent))
from authored_surface import AuthoredSurface

SURFACE = AuthoredSurface()
COLORS = {
    'shell': (.79, .82, .84, 1), 'dark': (.075, .10, .13, 1),
    'steel': (.51, .58, .63, 1), 'rail': (.77, .81, .84, 1),
    'blue': (.045, .28, .49, 1), 'black': (.022, .03, .04, 1),
    'copper': (.80, .47, .20, 1), 'screen': (.05, .22, .28, 1),
    'amber': (.97, .56, .06, 1), 'green': (.08, .70, .35, 1),
    'red': (.76, .055, .045, 1),
}


def transform(point, origin, rotation):
    x, y, z = point
    rx, ry, rz = rotation
    y, z = y * math.cos(rx) - z * math.sin(rx), y * math.sin(rx) + z * math.cos(rx)
    x, z = x * math.cos(ry) + z * math.sin(ry), -x * math.sin(ry) + z * math.cos(ry)
    x, y = x * math.cos(rz) - y * math.sin(rz), x * math.sin(rz) + y * math.cos(rz)
    return x + origin[0], y + origin[1], z + origin[2]


def add(link, color, triangles, origin=(0, 0, 0), rotation=(0, 0, 0)):
    SURFACE.add(link, color, [tuple(transform(p, origin, rotation) for p in t) for t in triangles])


def rings_mesh(rings):
    result = []
    count = len(rings[0])
    for a, b in zip(rings, rings[1:]):
        for i in range(count):
            j = (i + 1) % count
            result.extend([(a[i], a[j], b[j]), (a[i], b[j], b[i])])
    for ring, reverse in [(rings[0], True), (rings[-1], False)]:
        center = tuple(sum(p[k] for p in ring) / count for k in range(3))
        for i in range(count):
            triangle = center, ring[i], ring[(i + 1) % count]
            result.append(tuple(reversed(triangle)) if reverse else triangle)
    return result


def box(link, color, position, size, bevel=4, rotation=(0, 0, 0)):
    x, y, z = [v / 2 for v in size]
    b = min(bevel, x * .2, y * .2, z * .2)
    def ring(rx, ry, rz):
        return [(a, c, rz) for a, c in [(-rx+b,-ry),(rx-b,-ry),(rx,-ry+b),
                (rx,ry-b),(rx-b,ry),(-rx+b,ry),(-rx,ry-b),(-rx,-ry+b)]]
    add(link, color, rings_mesh([ring(x-b,y-b,-z),ring(x,y,-z+b),
                                ring(x,y,z-b),ring(x-b,y-b,z)]), position, rotation)


def lathe(link, color, position, profile, rotation=(0, 0, 0), segments=64):
    rings = [[(r * math.cos(i * 2 * math.pi / segments),
               r * math.sin(i * 2 * math.pi / segments), z)
              for i in range(segments)] for z, r in profile]
    add(link, color, rings_mesh(rings), position, rotation)


def cylinder(link, color, position, radius, height, rotation=(0, 0, 0), segments=40):
    lathe(link, color, position, [(-height/2, radius), (height/2, radius)], rotation, segments)


def annulus(link, color, inner_radius, outer_radius, bottom, top, segments=64):
    """Closed annular solid, with inward inner wall and no centre fan/caps."""
    def ring(radius, z):
        return [(radius*math.cos(i*2*math.pi/segments),
                 radius*math.sin(i*2*math.pi/segments),z) for i in range(segments)]
    ob,ot,ib,it=ring(outer_radius,bottom),ring(outer_radius,top),ring(inner_radius,bottom),ring(inner_radius,top)
    triangles=[]
    for i in range(segments):
        j=(i+1)%segments
        triangles.extend([(ob[i],ob[j],ot[j]),(ob[i],ot[j],ot[i]),
                          (ib[i],it[j],ib[j]),(ib[i],it[i],it[j]),
                          (it[i],ot[i],ot[j]),(it[i],ot[j],it[j]),
                          (ib[i],ob[j],ob[i]),(ib[i],ib[j],ob[j])])
    add(link,color,triangles)


def bolt(link, position, rotation=(0, 0, 0), radius=5):
    cylinder(link, 'black', position, radius, 6, rotation, 6)


def tube(link, color, points, radius=7):
    for a, b in zip(points, points[1:]):
        d = [b[i] - a[i] for i in range(3)]
        length = math.sqrt(sum(v*v for v in d))
        cylinder(link, color, tuple((a[i]+b[i])/2 for i in range(3)), radius, length,
                 (0, math.acos(d[2]/length), math.atan2(d[1], d[0])), 12)


# Welded lower bed leaves the machining space open above the chuck.
box('base', 'dark', (0, 0, -440), (2500, 1730, 300), 28)
box('base', 'shell', (0, -875, -440), (2350, 28, 245), 12)
box('base', 'blue', (0, -894, -530), (2240, 10, 48), 2)
for y in (-720, 720):
    box('base', 'dark', (0, y, -80), (2440, 230, 690), 22)
    box('base', 'shell', (0, y, 300), (2400, 210, 90), 10)
    for yy in (y-56, y+56):
        box('base', 'rail', (0, yy, 360), (2400, 25, 30), 2)
        for x in (-1000,-750,-500,-250,0,250,500,750,1000):
            bolt('base', (x, yy, 378), radius=4)
    for x in (-950, 0, 950):
        cylinder('base', 'steel', (x, y, -635), 32, 90)
        cylinder('base', 'black', (x, y, -698), 72, 36)
    for x in (-810, 0, 810):
        box('base', 'shell', (x, y + (-122 if y<0 else 122), -25), (720, 14, 470), 10)

# Independent stationary C-axis bearing pedestal, beneath the rotary chuck.
box('chuck_base', 'dark', (0, 0, -285), (680, 680, 160), 22)
lathe('chuck_base', 'shell', (0, 0, 0), [(-210,300),(-190,292),(-147,275),(-132,260)])
lathe('chuck_base', 'black', (0, 0, 0), [(-132,260),(-125,260)])
for i in range(12):
    a = i * math.pi / 6
    bolt('chuck_base', (288*math.cos(a), 288*math.sin(a), -203), radius=7)
box('chuck_base', 'blue', (286, 0, -192), (100,180,90), 9)

# Chuck support face is Z=0. The three static jaws stay open at diameter 260 mm
# for CAM loading; each is 30 mm farther out than the old diameter-200 pose.
lathe('c_chuck', 'dark', (0,0,0), [(-139,190),(-124,190),(-120,255),(-92,270),(-15,270),(-8,260)])
for i in range(3):
    a = i * 2 * math.pi / 3
    position = lambda r,z: ((r+30)*math.cos(a), (r+30)*math.sin(a), z)
    box('c_chuck', 'dark', position(177.5,5), (145,67,12), 2, (0,0,a))
    box('c_chuck', 'rail', position(165.5,27), (129,58,44), 3, (0,0,a))
    box('c_chuck', 'steel', position(117,57), (34,58,18), 2, (0,0,a))
    for r in (160,205):
        bolt('c_chuck', position(r,51), radius=7)
for i in range(9):
    a = (i+.5)*2*math.pi/9
    bolt('c_chuck', (245*math.cos(a),245*math.sin(a),2), radius=6)

# The central disc and coplanar annulus form one actual flat support surface.
# Keep their union on the explicit support body; the raised static jaws remain
# on C and cannot be mistaken for the seating plane.
lathe('c_support','rail',(0,0,0),[(-8,100),(0,100)])
annulus('c_support','steel',100,260,-8,0)

# Travelling X portal on double linear guideways, then Y saddle and Z ram.
for y in (-720,720):
    for x in (-140,140):
        box('x_bridge', 'blue', (x,y,402), (135,160,54), 5)
        bolt('x_bridge', (x,y,432), radius=9)
    box('x_bridge', 'dark', (0,y,452), (440,220,80), 8)
    box('x_bridge', 'shell', (0,y,832), (310,240,700), 28)
    box('x_bridge', 'blue', (-165,y,840), (12,195,550), 6)
box('x_bridge', 'shell', (0,0,1235), (320,1700,280), 24)
box('x_bridge', 'blue', (-171,0,1260), (14,1570,115), 4)
box('x_bridge', 'dark', (171,0,1240), (22,1520,230), 4)
for z in (1160,1310):
    box('x_bridge', 'rail', (191,0,z), (20,1410,27), 2)
    for y in (-625,-375,-125,125,375,625):
        bolt('x_bridge', (205,y,z), (0,math.pi/2,0), 4)
for y in range(-660,301,40):
    box('x_bridge', 'black', (-82,y,1405), (106,32,50), 4)
    box('x_bridge', 'steel', (-82,y,1432), (95,8,4), 1)
box('y_saddle','dark',(241,0,1235),(82,300,320),12)
box('y_saddle','shell',(297,0,1170),(86,290,450),14)
for y in (-104,104):
    box('y_saddle','rail',(348,y,1170),(20,25,420),2)
box('y_saddle','dark',(288,0,1470),(150,210,140),12)
for y in (-65,-35,-5,25,55):
    box('y_saddle','black',(368,y,1470),(5,10,98),1)

# Z ram coordinates are relative to the B trunnion centre. The ram remains well
# above the head, and its fork is split so it never fills the B-axis sweep space.
box('z_ram','shell',(0,0,655),(150,176,1050),15)
box('z_ram','blue',(81,0,660),(10,135,910),3)
for y in (-70,70):
    box('z_ram','rail',(-82,y,655),(14,24,980),2)
box('z_ram','dark',(0,0,102),(185,205,64),10)
for y in (-97,97):
    box('z_ram','shell',(0,y,35),(135,45,155),13)
    cylinder('z_ram','dark',(0,y,0),57,64,(math.pi/2,0,0))
    cylinder('z_ram','steel',(0,y+(-35 if y<0 else 35),0),43,6,(math.pi/2,0,0))
    for i in range(6):
        a = i*math.pi/3
        bolt('z_ram',(32*math.cos(a),y+(-40 if y<0 else 40),32*math.sin(a)),(math.pi/2,0,0),4)

# B is around local Y. The authored physical TCP is the copper nozzle endpoint.
lathe('b_head','dark',(0,0,0),[(44,38),(32,47),(-61,47),(-74,39)])
lathe('b_head','shell',(0,0,0),[(-73,43),(-86,43),(-140,34),(-153,29)])
lathe('b_head','steel',(0,0,0),[(-150,30),(-158,33),(-177,33),(-182,23)])
lathe('b_head','copper',(0,0,0),[(-180,23),(-188,23),(-214,7),(-220,5)])
cylinder('b_head','black',(0,0,-219.5),2.5,1)
for z in (-91,-102,-113,-124):
    lathe('b_head','steel',(0,0,0),[(z,42),(z-2,42)])
for y in (-65,65):
    cylinder('b_head','dark',(0,y,0),34,36,(math.pi/2,0,0))
box('b_head','blue',(49,0,-16),(12,52,76),5)
cylinder('b_head','copper',(22,0,47),11,22)
tube('b_head','black',[(22,0,57),(30,0,88),(62,0,103),(91,0,84)],7)
tube('b_head','blue',[(-25,18,34),(-45,25,63),(-49,36,91)],5)

# Open front and roof; rear panels and corner uprights show the enclosure scale.
for x in (-940,-470,0,470,940):
    box('enclosure','dark',(x,1030,572),(465,60,2380),9)
    box('enclosure','shell',(x,993,560),(437,14,2310),8)
    box('enclosure','blue',(x,982,1630),(437,7,110),2)
for x in (-1295,1295):
    for y in (-900,960):
        box('enclosure','shell',(x,y,570),(115,125,2390),16)
    box('enclosure','shell',(x,30,1810),(125,1980,135),15)
box('enclosure','shell',(0,1030,1810),(2700,125,135),15)
cylinder('enclosure','steel',(1230,970,1950),14,160)
for z,c in ((1900,'green'),(1946,'amber'),(1992,'red')):
    cylinder('enclosure',c,(1230,970,z),34,40)

# Service cabinet and suspended operator console remain outside the work area.
box('cabinet','dark',(1680,665,0),(590,690,1330),22)
box('cabinet','shell',(1680,304,0),(540,30,1240),14)
box('cabinet','blue',(1680,282,535),(490,8,70),3)
box('cabinet','black',(1895,276,200),(18,18,130),3)
for z in range(-430,-120,35):
    box('cabinet','black',(1680,280,z),(410,8,12),1)
for x in (1490,1870):
    cylinder('cabinet','black',(x,665,-692),45,55)
box('console','dark',(1600,-840,-120),(90,90,1140),10)
box('console','dark',(1600,-840,-710),(440,350,40),10)
box('console','shell',(1600,-840,550),(470,150,475),20)
box('console','black',(1600,-922,590),(390,14,290),10)
box('console','screen',(1600,-933,596),(352,5,245),4)
for x in (1455,1520,1585,1650):
    box('console','steel',(x,-927,369),(38,12,20),3)
cylinder('console','amber',(1766,-930,386),25,12,(math.pi/2,0,0))
cylinder('console','red',(1766,-942,386),17,20,(math.pi/2,0,0))

# Conservative split boxes keep the portal and work envelope hollow. Adjacent
# bearing/guide contacts are separate mechanical pairs, not solid interference.
COLLIDERS = {
 'base': [((0,0,-440),(2500,1800,300))]
       + [((0,y,-123),(2440,260,1186)) for y in (-720,720)],
 'chuck_base': [((0,0,-285),(680,680,160)),((0,0,-167.5),(600,600,85)),
       ((286,0,-192),(100,180,90))],
 'c_chuck': [((0,0,-69.5),(540,540,139))],
 'c_support': [((0,0,-4),(520,520,8))],
 'x_bridge': [((0,y,777),(450,250,810)) for y in (-720,720)]
       + [((14,0,1235),(392,1700,280)),((-82,-180,1405),(106,1000,58))],
 'y_saddle': [((270,0,1170),(180,300,450)),((288,0,1470),(165,210,140))],
 'z_ram': [((0,0,655),(188,176,1050)),((0,0,102),(185,205,64))]
       + [((0,y,28),(135,160,170)) for y in (-97,97)],
 'b_head': [((0,0,-15),(110.002,166.002,120.002)),
       ((0,0,-115),(86.002,86.002,80.002)),
       ((0,0,-166),(66.002,66.002,32.002)),
       ((0,0,-197),(46.002,46.002,34.002)),
       ((0,0,-217),(14.002,14.002,6.002)),
       ((20.75,16.5,77.5),(154.502,83.002,65.002))],
 'enclosure': [((0,1021.5,572),(2345,103,2380))]
       + [((x,y,570),(115,125,2390)) for x in (-1295,1295) for y in (-900,960)]
       + [((x,30,1810),(125,1980,135)) for x in (-1295,1295)]
       + [((0,1030,1810),(2700,125,135)),((1230,970,1941),(68,68,182))],
 'cabinet': [((1680,638,-13.5),(590,748,1412))],
 'console': [((1600,-840,-120),(90,90,1140)),((1600,-840,-710),(440,350,40)),
       ((1600,-877,550),(470,240,475))],
}
# Individual rotated boxes preserve free space between chuck jaws.
JAW_COLLIDERS = [((205*math.cos(a),205*math.sin(a),32.5),(150,67,67),(0,0,a))
                for a in (0,2*math.pi/3,4*math.pi/3)]


def write_stl(path, triangles):
    with path.open('wb') as stream:
        stream.write(b'iCAX RC-1000 original authored exterior'.ljust(80,b'\0'))
        stream.write(struct.pack('<I',len(triangles)))
        for a,b,c in triangles:
            u=[b[i]-a[i] for i in range(3)]
            v=[c[i]-a[i] for i in range(3)]
            n=[u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]]
            length=math.sqrt(sum(x*x for x in n))
            n=[x/length if length else 0 for x in n]
            stream.write(struct.pack('<12fH',*n,*(x/1000 for p in (a,b,c) for x in p),0))


AUTHORED_PARTS = SURFACE.finish()
# Rebase source frames, not the viewer gizmo. A translated frame changes every
# local attachment by -c. The child joint becomes T(-c_parent) O T(c_child).
# Linear motion commutes with any translation. A rotary child may translate
# only along its declared rotation axis, preserving the complete physical line.
ROTARY_AXES = {'c_chuck': (0,0,1), 'b_head': (0,1,0)}
FRAME_SHIFTS = {}
for link,groups in AUTHORED_PARTS.items():
    points=[p for triangles in groups.values() for triangle in triangles for p in triangle]
    center=tuple((min(p[k] for p in points)+max(p[k] for p in points))/2 for k in range(3))
    if link=='base':
        shift=(0.,0.,0.)
    elif link in ROTARY_AXES:
        axis=ROTARY_AXES[link]
        projection=sum(center[k]*axis[k] for k in range(3))
        shift=tuple(projection*axis[k] for k in range(3))
    else:
        shift=center
    FRAME_SHIFTS[link]=shift
PARTS={link:{color:[tuple(tuple(p[k]-FRAME_SHIFTS[link][k] for k in range(3)) for p in triangle)
                       for triangle in triangles] for color,triangles in groups.items()}
       for link,groups in AUTHORED_PARTS.items()}
robot = ET.Element('robot',name='iCAX RC-1000 4+1 转盘五轴激光加工中心')
(ROOT/'meshes').mkdir(exist_ok=True)
for link, groups in PARTS.items():
    element = ET.SubElement(robot,'link',name=link)
    if link == 'c_support':
        ET.SubElement(element,'icax_work_support')
    if link == 'b_head':
        tcp=tuple(v-FRAME_SHIFTS[link][k] for k,v in enumerate((0,0,-220)))
        ET.SubElement(element,'icax_tool',name='RC-1000 物理喷嘴',type='laser_cutting_head',
                      tcp_mm=' '.join(str(v) for v in tcp),beam='0 0 -1')
    for color, triangles in groups.items():
        filename=f'{link}_{color}.stl'
        write_stl(ROOT/'meshes'/filename,triangles)
        visual=ET.SubElement(element,'visual',name=f'{link}_{color}')
        ET.SubElement(ET.SubElement(visual,'geometry'),'mesh',filename=f'meshes/{filename}')
        ET.SubElement(ET.SubElement(visual,'material',name=color),'color',
                      rgba=' '.join(map(str,COLORS[color])))
    boxes=[(p,s,(0,0,0)) for p,s in COLLIDERS[link]]
    if link=='c_chuck':
        boxes+=JAW_COLLIDERS
        # Small face bolts rise 5 mm above the chuck face.
        boxes += [((245*math.cos((i+.5)*2*math.pi/9),245*math.sin((i+.5)*2*math.pi/9),2),
                   (12,12,6),(0,0,0)) for i in range(9)]
    for i,(position,size,rotation) in enumerate(boxes):
        position=tuple(position[k]-FRAME_SHIFTS[link][k] for k in range(3))
        collision=ET.SubElement(element,'collision',name=f'{link}_box_{i+1}')
        ET.SubElement(collision,'origin',xyz=' '.join(str(v/1000) for v in position),
                      rpy=' '.join(str(v) for v in rotation))
        ET.SubElement(ET.SubElement(collision,'geometry'),'box',size=' '.join(str(v/1000) for v in size))


def joint(name,parent,child,kind='fixed',origin=(0,0,0),axis=(0,0,1),limits=(0,0),velocity=1,home=None):
    if kind in ('revolute','continuous'):
        shift=FRAME_SHIFTS[child]
        projection=sum(shift[k]*axis[k] for k in range(3))
        assert all(abs(shift[k]-projection*axis[k])<1e-9 for k in range(3)), 'Rotary frame shift must stay on its physical axis'
    # All authored joint origins in this model have identity rotation.
    origin=tuple(origin[k]+FRAME_SHIFTS[child][k]-FRAME_SHIFTS[parent][k] for k in range(3))
    element=ET.SubElement(robot,'joint',name=name,type=kind)
    ET.SubElement(element,'parent',link=parent)
    ET.SubElement(element,'child',link=child)
    ET.SubElement(element,'origin',xyz=' '.join(str(v/1000) for v in origin))
    if kind!='fixed':
        ET.SubElement(element,'axis',xyz=' '.join(str(v) for v in axis))
        ET.SubElement(element,'limit',lower=str(limits[0]),upper=str(limits[1]),
                      effort='5000',velocity=str(velocity))
        ET.SubElement(element,'icax_home',position=str(home if home is not None else 0))


joint('chuck_base_mount','base','chuck_base')
joint('C','chuck_base','c_chuck','revolute',limits=(-math.pi,math.pi),velocity=1.2)
joint('work_support_mount','c_chuck','c_support')
joint('X','base','x_bridge','prismatic',origin=(-320,0,0),axis=(1,0,0),limits=(-.6,.6),velocity=.8)
joint('Y','x_bridge','y_saddle','prismatic',axis=(0,1,0),limits=(-.4,.4),velocity=.8)
joint('Z','y_saddle','z_ram','prismatic',origin=(320,0,650),limits=(-.40,.12),velocity=.5,home=.12)
joint('B','z_ram','b_head','revolute',axis=(0,1,0),limits=(-2.0943951023931953,2.0943951023931953),velocity=1.2)
for link in ('enclosure','cabinet','console'):
    joint(link+'_mount','base',link)
NAMES = {'base':'床身与直线导轨','chuck_base':'C 轴轴承座','c_chuck':'C 三爪回转卡盘','c_support':'中央装夹承托盘',
         'x_bridge':'X 移动龙门','y_saddle':'Y 滑鞍','z_ram':'Z 滑枕与摆头叉架',
         'b_head':'B 摆动激光切割头','enclosure':'外罩（剖视）','cabinet':'电气柜','console':'操作台'}
for element in robot.findall('link'):
    element.set('name',NAMES[element.get('name')])
for element in robot.findall('joint'):
    for tag in ('parent','child'):
        node=element.find(tag)
        node.set('link',NAMES[node.get('link')])
ET.indent(robot)
ET.ElementTree(robot).write(ROOT/'RC-1000.urdf',encoding='utf-8',xml_declaration=True)
print(f'{len(PARTS)} links; {sum(len(g) for g in PARTS.values())} meshes; '
      f'{sum(len(t) for g in PARTS.values() for t in g.values())} triangles')
