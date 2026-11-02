"""Author the TC-800 XYZ / AC cradle laser machine, dimensions in mm.

Install ../requirements-build.txt before executing. Meshes and URDF use metres.
Every rigid link is unioned before its exposed surface is split by material.
"""
import math
from pathlib import Path
import struct
import sys
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT.parent))
from authored_surface import AuthoredSurface

SURFACE = AuthoredSurface()
COLORS = {
    'shell': (.80, .83, .85, 1), 'dark': (.075, .10, .13, 1),
    'steel': (.48, .56, .62, 1), 'rail': (.77, .82, .86, 1),
    'blue': (.035, .29, .52, 1), 'black': (.024, .032, .044, 1),
    'copper': (.80, .46, .18, 1), 'screen': (.055, .21, .31, 1),
    'red': (.76, .055, .035, 1), 'amber': (.95, .56, .055, 1),
    'green': (.07, .68, .32, 1),
}


def transform(p, origin, rotation):
    x, y, z = p
    rx, ry, rz = rotation
    y, z = y * math.cos(rx) - z * math.sin(rx), y * math.sin(rx) + z * math.cos(rx)
    x, z = x * math.cos(ry) + z * math.sin(ry), -x * math.sin(ry) + z * math.cos(ry)
    x, y = x * math.cos(rz) - y * math.sin(rz), x * math.sin(rz) + y * math.cos(rz)
    return x + origin[0], y + origin[1], z + origin[2]


def add(link, color, triangles, position=(0, 0, 0), rotation=(0, 0, 0)):
    SURFACE.add(link, color, [tuple(transform(p, position, rotation) for p in face) for face in triangles])


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
            face = center, ring[i], ring[(i + 1) % count]
            result.append(tuple(reversed(face)) if reverse else face)
    return result


def box(link, color, position, size, bevel=5):
    x, y, z = [v / 2 for v in size]
    b = min(bevel, x * .2, y * .2, z * .2)
    def ring(x, y, z):
        return [(a, c, z) for a, c in [(-x + b, -y), (x - b, -y), (x, -y + b),
                (x, y - b), (x - b, y), (-x + b, y), (-x, y - b), (-x, -y + b)]]
    add(link, color, rings_mesh([ring(x-b, y-b, -z), ring(x, y, -z+b),
                               ring(x, y, z-b), ring(x-b, y-b, z)]), position)


def lathe(link, color, position, profile, rotation=(0, 0, 0), segments=64):
    rings = [[(r * math.cos(i * 2 * math.pi / segments), r * math.sin(i * 2 * math.pi / segments), z)
              for i in range(segments)] for z, r in profile]
    add(link, color, rings_mesh(rings), position, rotation)


def cylinder(link, color, position, radius, height, rotation=(0, 0, 0), segments=40):
    lathe(link, color, position, [(-height/2, radius), (height/2, radius)], rotation, segments)


def bolt(link, position, radius=6, rotation=(0, 0, 0)):
    cylinder(link, 'black', position, radius, 7, rotation, 6)


# One bed rigid body owns the two fixed A-axis bearing supports, and the
# overhead stationary portal. Front and roof are deliberately cut away.
box('base', 'dark', (0, 150, 235), (2560, 1920, 330), 30)
box('base', 'steel', (0, 150, 425), (2480, 1900, 60), 10)
for x in [-1080, 1080]:
    for y in [-610, 850]:
        cylinder('base', 'steel', (x, y, 65), 42, 70)
        cylinder('base', 'black', (x, y, 17.5), 85, 35)
for x in [-920, -300, 300, 920]:
    box('base', 'shell', (x, -822, 235), (570, 22, 265), 9)
    box('base', 'blue', (x, -837, 145), (570, 8, 45), 2)
for x in [-630, 630]:
    box('base', 'shell', (x, 0, 725), (250, 440, 540), 22)
    box('base', 'blue', (x, -227, 710), (210, 12, 420), 7)
    cylinder('base', 'dark', (x, 0, 1000), 154, 245, (0, math.pi/2, 0))
    cylinder('base', 'rail', (x + (-128 if x < 0 else 128), 0, 1000), 122, 10, (0, math.pi/2, 0))
    for i in range(10):
        angle = i * math.tau / 10
        bolt('base', (x + (-137 if x < 0 else 137), 108*math.cos(angle), 1000+108*math.sin(angle)),
             6, (0, math.pi/2, 0))
for x in [-1040, 1040]:
    box('base', 'shell', (x, 900, 1270), (300, 360, 1630), 25)
    box('base', 'blue', (x, 708, 1260), (230, 18, 1360), 8)
box('base', 'shell', (0, 900, 2100), (2380, 360, 360), 25)
box('base', 'dark', (0, 706, 2100), (2200, 35, 345), 5)
for z in [1995, 2205]:
    box('base', 'rail', (0, 678, z), (2180, 22, 28), 2)
    for x in range(-1000, 1001, 125):
        bolt('base', (x, 661, z), 4, (math.pi/2, 0, 0))

# XYZ tool branch. X carries a forward-reaching Y beam. The rail lengths
# cover the complete travel with a finite guide-block margin.
box('x_carriage', 'dark', (-250, 647, 2100), (340, 75, 420), 10)
# The beam is offset to the side of the Z ram, leaving the complete ram
# travel physically open; the Y saddle bridges this 250 mm lateral offset.
box('x_carriage', 'shell', (-250, 57.5, 2160), (280, 1245, 230), 17)
box('x_carriage', 'blue', (-250, -572, 2160), (230, 18, 180), 6)
for x in [-345, -155]:
    box('x_carriage', 'rail', (x, 65, 2032), (28, 1200, 24), 2)
    for y in range(-470, 601, 110):
        bolt('x_carriage', (x, y, 2017), 4)
box('x_carriage', 'dark', (-250, 580, 2340), (260, 180, 160), 12)
for y in range(526, 627, 20):
    box('x_carriage', 'black', (-119, y, 2340), (8, 8, 115), 1)
for y in range(-500, 571, 40):
    box('x_carriage', 'black', (-410, y, 2220), (38, 30, 70), 4)

# A real through-opening is needed: at the loading height the upper laser
# head rises into this saddle's vertical range. Only its side/rear/front
# frames surround the ram; no cross-member fills the central tool passage.
for y in [-165, 165]:
    box('y_carriage', 'dark', (-150, y, 1957), (500, 70, 132), 10)
box('y_carriage', 'dark', (-350, 0, 1957), (100, 400, 132), 10)
for x in [-345, -155]:
    for y in [-85, 85]:
        box('y_carriage', 'blue', (x, y, 2020), (85, 95, 45), 5)
for x in [-145, 145]:
    box('y_carriage', 'shell', (x, 0, 1900), (60, 300, 210), 9)
for x in [-137.5, 137.5]:
    box('y_carriage', 'dark', (x, 0, 1900), (65, 280, 200), 7)

box('z_slide', 'shell', (0, 0, 500), (190, 210, 1200), 12)
box('z_slide', 'blue', (0, -113, 500), (150, 12, 1090), 4)
for x in [-96, 96]:
    box('z_slide', 'rail', (x, 32, 500), (15, 30, 1180), 2)
box('z_slide', 'dark', (0, 0, -2), (200, 240, 85), 10)
for z in range(-60, 1070, 45):
    box('z_slide', 'black', (0, 117, z), (170, 12, 11), 2)

# Physical laser head owns its true copper nozzle tip at local z=-250 mm.
# Beam stand-off is an operation setting, not a baked-in tool TCP offset.
lathe('laser_head', 'dark', (0, 0, 0), [(24, 50), (12, 58), (-58, 58), (-70, 47)])
lathe('laser_head', 'shell', (0, 0, 0), [(-65, 49), (-80, 49), (-150, 36), (-166, 30)])
lathe('laser_head', 'steel', (0, 0, 0), [(-160, 32), (-171, 38), (-190, 38), (-197, 25)])
lathe('laser_head', 'copper', (0, 0, 0), [(-190, 24), (-205, 24), (-244, 6), (-250, 4.5)])
cylinder('laser_head', 'black', (0, 0, -249.5), 2, 1)
for z in [-85, -98, -111]:
    cylinder('laser_head', 'steel', (0, 0, z), 49, 3)
box('laser_head', 'blue', (0, -55, -25), (60, 18, 90), 5)
cylinder('laser_head', 'copper', (25, 0, 32), 11, 26)

# A rotates the whole cradle around X. Its lowered cross-beam leaves a
# useful central machining opening. C rotates the platter in A local Z.
for x in [-515, 515]:
    box('a_cradle', 'blue', (x, 0, -120), (135, 250, 310), 17)
    cylinder('a_cradle', 'steel', (x, 0, 0), 112, 155, (0, math.pi/2, 0))
    cylinder('a_cradle', 'dark', (x + (-82 if x < 0 else 82), 0, 0), 87, 12, (0, math.pi/2, 0))
box('a_cradle', 'blue', (0, 0, -280), (1110, 270, 140), 18)
box('a_cradle', 'dark', (0, 0, -201), (570, 390, 36), 10)
lathe('a_cradle', 'steel', (0, 0, 0), [(-205, 188), (-185, 215), (-131, 215), (-120, 185)])
for i in range(12):
    a = i * math.tau/12
    bolt('a_cradle', (235*math.cos(a), 165*math.sin(a), -181), 6)

lathe('c_table', 'dark', (0, 0, 0), [(-160, 148), (-145, 220), (-73, 220), (-64, 275)])
lathe('c_table', 'steel', (0, 0, 0), [(-70, 370), (-60, 400), (-8, 400), (0, 392)], segments=96)
# Raised annular-free fixture strips divide the top into visible T-slots.
# Their highest surface is z=0, exactly the declared support origin.
for y in [-225, -75, 75, 225]:
    half = math.sqrt(380**2 - (abs(y)+29)**2)
    box('c_table', 'rail', (0, y, -2), (half*2, 52, 4), 1)
for i in range(12):
    a = i*math.tau/12
    bolt('c_table', (350*math.cos(a), 350*math.sin(a), -3.5), 5)

# Rear and side lower panels retain the enclosure silhouette while exposing
# the cradle, tool and workpiece attachment for editing and simulation.
box('enclosure', 'dark', (0, 1130, 1330), (2860, 80, 2600), 12)
for x in [-1050, -350, 350, 1050]:
    box('enclosure', 'shell', (x, 1081, 1290), (680, 18, 2480), 9)
    box('enclosure', 'blue', (x, 1067, 2260), (680, 8, 130), 3)
for x in [-1440, 1440]:
    box('enclosure', 'shell', (x, 130, 360), (85, 1940, 640), 12)
    box('enclosure', 'blue', (x, -858, 340), (75, 20, 510), 6)
    box('enclosure', 'shell', (x, 1100, 1510), (140, 140, 2270), 13)
cylinder('enclosure', 'steel', (1360, 1100, 2730), 15, 180)
for z, color in [(2680, 'green'), (2730, 'amber'), (2780, 'red')]:
    cylinder('enclosure', color, (1360, 1100, z), 35, 42)

box('cabinet', 'dark', (1810, 680, 905), (590, 790, 1750), 20)
box('cabinet', 'shell', (1810, 269, 905), (550, 28, 1650), 13)
box('cabinet', 'blue', (1810, 249, 1580), (510, 10, 90), 3)
box('cabinet', 'black', (2025, 240, 1060), (18, 16, 160), 3)
for z in range(290, 651, 32):
    box('cabinet', 'black', (1810, 247, z), (440, 8, 12), 2)
for x in [-220, 220]:
    cylinder('cabinet', 'black', (1810+x, 680, 17.5), 42, 35)

box('console', 'dark', (1580, -850, 35), (460, 400, 70), 12)
box('console', 'dark', (1580, -850, 600), (95, 95, 1120), 10)
box('console', 'shell', (1580, -850, 1390), (530, 170, 520), 20)
box('console', 'black', (1580, -943, 1450), (450, 15, 325), 10)
box('console', 'screen', (1580, -954, 1450), (405, 7, 280), 4)
for x in [1410, 1480, 1550, 1620]:
    box('console', 'steel', (x, -944, 1210), (36, 14, 24), 3)
cylinder('console', 'amber', (1760, -945, 1210), 25, 10, (math.pi/2, 0, 0))
cylinder('console', 'red', (1760, -960, 1210), 18, 24, (math.pi/2, 0, 0))

# Conservative authored boxes. Hollow bed/cradle and portal are represented
# by separate solids, leaving the actual machining opening available.
COLLIDERS = {
    'base': [((0, 150, 235), (2560, 1920, 330)), ((0, 150, 425), (2480, 1900, 60)),
             ((0, -825, 235), (2440, 32, 270))]
        + [((x, y, 47.5), (170, 170, 95)) for x in [-1080, 1080] for y in [-610, 850]]
        + [((x, 0, 725), (250, 480, 540)) for x in [-630, 630]]
        + [((x, 0, 1000), (282, 308, 308)) for x in [-630, 630]]
        + [((x, 900, 1270), (300, 402, 1630)) for x in [-1040, 1040]]
        + [((0, 900, 2100), (2380, 360, 360)), ((0, 683, 2100), (2200, 76, 345))],
    'x_carriage': [((-250, 647, 2100), (340, 75, 420)), ((-250, 49.5, 2160), (280, 1261, 230)),
                   ((-250, 65, 2028), (218, 1200, 34)), ((-250, 580, 2340), (270, 180, 160)),
                   ((-410, 35, 2220), (38, 1100, 70))],
    'y_carriage': [((-150, y, 1957), (500, 70, 132)) for y in [-165, 165]]
        + [((-350, 0, 1957), (100, 400, 132))]
        + [((x, 0, 1900), (70, 300, 220)) for x in [-140, 140]]
        + [((-250, 0, 2020), (275, 265, 45))],
    'z_slide': [((0, 2, 500), (207, 244, 1200)), ((0, 0, -2), (200, 240, 85))],
    'laser_head': [((0, -2, -24), (116.002, 124.002, 96.002)),
                   ((0, 0, -112), (98.002, 98.002, 104.002)),
                   ((0, 0, -179), (76.002, 76.002, 38.002)),
                   ((0, 0, -217), (48.002, 48.002, 54.002)),
                   ((0, 0, -247), (12.002, 12.002, 6.002)),
                   ((25, 0, 32), (22.002, 22.002, 26.002))],
    'a_cradle': [((x, 0, -120), (135, 250, 310)) for x in [-515, 515]]
        + [((x, 0, 0), (177, 224, 224)) for x in [-515, 515]]
        + [((0, 0, -280), (1110, 270, 140)), ((0, 0, -201), (570, 390, 36)),
           ((0, 0, -162.5), (430, 430, 85)), ((0, 0, -181), (482, 342, 7))],
    # Circular platter uses eight strips: a single square would sweep its
    # unused corners into the fixed A-bearing supports during C rotation.
    'c_table': [((0, y, -35), (2*math.sqrt(400**2-(abs(y)-50)**2), 100, 70))
                for y in range(-350, 351, 100)]
        + [((0, 0, -112), (550, 550, 96))],
    'enclosure': [((0, 1116.5, 1330), (2860, 107, 2600))]
        + [((x, 130, 360), (85, 1998, 640)) for x in [-1440, 1440]]
        + [((x, 1100, 1510), (140, 140, 2270)) for x in [-1440, 1440]]
        + [((1360, 1100, 2730), (70, 70, 180))],
    'cabinet': [((1810, 650, 905), (590, 850, 1750)), ((1810, 680, 17.5), (524, 84, 35))],
    'console': [((1580, -850, 35), (460, 400, 70)), ((1580, -850, 600), (95, 95, 1120)),
                ((1580, -874, 1390), (530, 218, 520))],
}


def write_stl(path, triangles):
    with path.open('wb') as stream:
        stream.write(b'iCAX TC-800 authored cradle laser machine'.ljust(80, b'\0'))
        stream.write(struct.pack('<I', len(triangles)))
        for a, b, c in triangles:
            u, v = [b[i]-a[i] for i in range(3)], [c[i]-a[i] for i in range(3)]
            normal = [u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0]]
            length = math.sqrt(sum(x*x for x in normal))
            normal = [x/length if length else 0 for x in normal]
            stream.write(struct.pack('<12fH', *normal, *(x/1000 for p in (a, b, c) for x in p), 0))


PARTS = SURFACE.finish()
robot = ET.Element('robot', name='iCAX TC-800 3+2 摇篮式五轴激光机床')
(ROOT/'meshes').mkdir(exist_ok=True)
for name, groups in PARTS.items():
    element = ET.SubElement(robot, 'link', name=name)
    if name == 'c_table':
        ET.SubElement(element, 'icax_work_support')
    if name == 'laser_head':
        ET.SubElement(element, 'icax_tool', name='TC-800 物理喷嘴', type='laser_cutting_head',
                      tcp_mm='0 0 -250', beam='0 0 -1')
    for color, triangles in groups.items():
        filename = f'{name}_{color}.stl'
        visual = ET.SubElement(element, 'visual', name=f'{name}_{color}')
        ET.SubElement(ET.SubElement(visual, 'geometry'), 'mesh', filename=f'meshes/{filename}')
        ET.SubElement(ET.SubElement(visual, 'material', name=color), 'color',
                      rgba=' '.join(map(str, COLORS[color])))
    for index, (position, size) in enumerate(COLLIDERS[name]):
        # The small guard covers all float32 metre-storage roundoff.
        collision = ET.SubElement(element, 'collision', name=f'{name}_box_{index+1}')
        ET.SubElement(collision, 'origin', xyz=' '.join(str(v/1000) for v in position))
        ET.SubElement(ET.SubElement(collision, 'geometry'), 'box',
                      size=' '.join(str((v+.002)/1000) for v in size))


def joint(name, parent, child, kind='fixed', origin=(0, 0, 0), axis=(0, 0, 1),
          limits=(0, 0), velocity=1, home=None):
    element = ET.SubElement(robot, 'joint', name=name, type=kind)
    ET.SubElement(element, 'parent', link=parent)
    ET.SubElement(element, 'child', link=child)
    ET.SubElement(element, 'origin', xyz=' '.join(str(x/1000) for x in origin))
    if kind != 'fixed':
        ET.SubElement(element, 'axis', xyz=' '.join(map(str, axis)))
        ET.SubElement(element, 'limit', lower=str(limits[0]), upper=str(limits[1]),
                      velocity=str(velocity), effort='8000')
        ET.SubElement(element, 'icax_home', position=str(0 if home is None else home))


joint('X', 'base', 'x_carriage', 'prismatic', axis=(1, 0, 0), limits=(-.65, .65), velocity=.8)
joint('Y', 'x_carriage', 'y_carriage', 'prismatic', axis=(0, 1, 0), limits=(-.38, .38), velocity=.8)
joint('Z', 'y_carriage', 'z_slide', 'prismatic', origin=(0, 0, 1600),
      limits=(-.55, .25), velocity=.6, home=.25)
joint('head_mount', 'z_slide', 'laser_head')
joint('A', 'base', 'a_cradle', 'revolute', origin=(0, 0, 1000), axis=(1, 0, 0),
      limits=(-math.radians(110), math.radians(110)), velocity=1.2)
joint('C', 'a_cradle', 'c_table', 'revolute', limits=(-math.pi, math.pi), velocity=1.5)
joint('enclosure_mount', 'base', 'enclosure')
joint('cabinet_mount', 'base', 'cabinet')
joint('console_mount', 'base', 'console')

# The construction dimensions above stay in their original engineering
# coordinates. Export each rigid link in a local frame near its own geometry,
# rather than leaving e.g. the Y saddle's origin on the floor. A revolute
# frame may move only along its own axis, preserving the actual axis line.
# For every q: O'_i J_i(q) = T(-c_parent) O_i J_i(q) T(c_i).
# Visual attachments/collider/TCP coordinates are translated by -c_i, and
# every child joint compensates for its parent's frame translation. Keeping
# authored STL vertices unchanged avoids a second float32 quantization.
link_nodes = {node.get('name'): node for node in robot.findall('link')}
parent_joints = {node.find('child').get('link'): node for node in robot.findall('joint')}
FRAME_CENTERS = {}
for name, groups in PARTS.items():
    points = [p for triangles in groups.values() for face in triangles for p in face]
    center = tuple(round((min(p[k] for p in points)+max(p[k] for p in points))/2, 3)
                   for k in range(3))
    source_joint = parent_joints.get(name)
    if source_joint is None:
        center = (0., 0., 0.)
    elif source_joint.get('type') in ('revolute', 'continuous'):
        axis = tuple(float(v) for v in source_joint.find('axis').get('xyz').split())
        norm = math.sqrt(sum(v*v for v in axis))
        axis = tuple(v/norm for v in axis)
        projection = sum(center[k]*axis[k] for k in range(3))
        center = tuple(projection*v for v in axis)
    FRAME_CENTERS[name] = center

for name, groups in PARTS.items():
    center = FRAME_CENTERS[name]
    for color, triangles in groups.items():
        write_stl(ROOT/'meshes'/f'{name}_{color}.stl', triangles)
    if any(center):
        for visual in link_nodes[name].findall('visual'):
            visual.insert(0, ET.Element('origin', xyz=' '.join(str(-v/1000) for v in center)))
    for collision in link_nodes[name].findall('collision'):
        attachment = collision.find('origin')
        point = tuple(float(v)*1000 for v in attachment.get('xyz').split())
        attachment.set('xyz', ' '.join(str((point[k]-center[k])/1000) for k in range(3)))
    tool = link_nodes[name].find('icax_tool')
    if tool is not None:
        tcp = tuple(float(v) for v in tool.get('tcp_mm').split())
        tool.set('tcp_mm', ' '.join(str(tcp[k]-center[k]) for k in range(3)))

for node in robot.findall('joint'):
    parent = node.find('parent').get('link')
    child = node.find('child').get('link')
    attachment = node.find('origin')
    point = tuple(float(v)*1000 for v in attachment.get('xyz').split())
    rotation = tuple(float(v) for v in attachment.get('rpy', '0 0 0').split())
    translated = transform(FRAME_CENTERS[child], point, rotation)
    attachment.set('xyz', ' '.join(str((translated[k]-FRAME_CENTERS[parent][k])/1000)
                                    for k in range(3)))

NAMES = {
    'base': '床身与耳轴支承', 'x_carriage': 'X 悬臂横滑座', 'y_carriage': 'Y 滑鞍',
    'z_slide': 'Z 滑枕', 'laser_head': '固定激光切割头', 'a_cradle': 'A 摇篮',
    'c_table': 'C 回转工作台', 'enclosure': '外罩（剖视）', 'cabinet': '电气柜', 'console': '操作台',
}
for element in robot.findall('link'):
    element.set('name', NAMES[element.get('name')])
for element in robot.findall('joint'):
    for tag in ['parent', 'child']:
        node = element.find(tag)
        node.set('link', NAMES[node.get('link')])
ET.indent(robot)
ET.ElementTree(robot).write(ROOT/'TC-800.urdf', encoding='utf-8', xml_declaration=True)
print(f'{len(PARTS)} links; {sum(len(g) for g in PARTS.values())} meshes; '
      f'{sum(len(t) for g in PARTS.values() for t in g.values())} triangles')
