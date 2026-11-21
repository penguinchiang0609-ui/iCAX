"""Build standalone CAM heads with an 80 mm longer narrow nozzle.

Source machine assets remain unchanged. URDF and STL coordinates are metres;
tcp_mm is the virtual cutting point, 0.2 mm beyond the physical nozzle end.
"""
from pathlib import Path
import copy
import json
import math
import shutil
import struct
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parent
GANTRY = ROOT.parent / 'machines/gantry-heads'
EXTENSION = .080
VIRTUAL_TIP_OFFSET = .0002
TIP_RADIUS = .0005
SLAB_LENGTH = .002


def vector(text):
    return [float(v) for v in text.split()]


def text(values):
    return ' '.join(format(v, '.15g') for v in values)


def pose(node):
    if node is None:
        return [0., 0., 0.], [[1., 0., 0.], [0., 1., 0.], [0., 0., 1.]]
    a, b, c = vector(node.get('rpy', '0 0 0'))
    sx, cx, sy, cy, sz, cz = math.sin(a), math.cos(a), math.sin(b), math.cos(b), math.sin(c), math.cos(c)
    return vector(node.get('xyz', '0 0 0')), [
        [cz*cy, cz*sy*sx-sz*cx, cz*sy*cx+sz*sx],
        [sz*cy, sz*sy*sx+cz*cx, sz*sy*cx-cz*sx], [-sy, cy*sx, cy*cx]]


def world(p, frame):
    t, r = frame
    return [t[i]+sum(r[i][k]*p[k] for k in range(3)) for i in range(3)]


def local(p, frame):
    t, r = frame
    return [sum(r[k][i]*(p[k]-t[k]) for k in range(3)) for i in range(3)]


def dot(a, b):
    return sum(x*y for x, y in zip(a, b))


def split(polygon, beam, station):
    """Split a surface before applying the piecewise axial extension."""
    distances = [dot(p, beam)-station for p in polygon]
    if min(distances) >= -1e-10 or max(distances) <= 1e-10:
        return [polygon]
    halves = [[], []]
    for i, p in enumerate(polygon):
        q, d, e = polygon[(i+1) % len(polygon)], distances[i], distances[(i+1) % len(polygon)]
        for side in (0, 1):
            if (d >= 0) == bool(side) or abs(d) < 1e-12:
                halves[side].append(p)
        if d*e < 0:
            u = d/(d-e)
            crossing = [p[k]+u*(q[k]-p[k]) for k in range(3)]
            for half in halves:
                half.append(crossing)
    return [half for half in halves if len(half) >= 3]


def normal(triangle):
    a, b, c = triangle
    u, v = [[p[k]-a[k] for k in range(3)] for p in (b, c)]
    n = [u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0]]
    length = math.sqrt(dot(n, n))
    return [x/length for x in n] if length > 1e-17 else None


def nozzle_profile(folder, link, tcp, beam):
    tip = dot(tcp, beam)
    rings = [[], []]
    for visual in link.findall('visual'):
        frame = pose(visual.find('origin'))
        mesh = visual.find('geometry/mesh')
        scale = vector(mesh.get('scale', '1 1 1'))
        data = (folder/mesh.get('filename')).read_bytes()
        for row in struct.iter_unpack('<12fH', data[84:]):
            for offset in (3, 6, 9):
                p = world([row[offset+k]*scale[k] for k in range(3)], frame)
                station = dot(p, beam)
                radius = math.sqrt(sum((p[k]-tcp[k]-beam[k]*(station-tip))**2 for k in range(3)))
                for index, level in enumerate((tip-.006, tip)):
                    if abs(station-level) < 1e-7:
                        rings[index].append(radius)
    assert all(rings)
    return [max(ring) for ring in rings]


def nozzle_sections(tcp, beam):
    tip = dot(tcp, beam)
    start, end = tip-.006, tip+EXTENSION
    count = math.ceil((end-start)/SLAB_LENGTH)
    return [start+(end-start)*i/count for i in range(count+1)]


def extend_mesh(source, destination, visual, tcp, beam, profile):
    frame = pose(visual.find('origin'))
    scale = vector(visual.find('geometry/mesh').get('scale', '1 1 1'))
    # Keep the original seam at the shoulder; end in a 1 mm diameter tip.
    tip = dot(tcp, beam)
    start, end = tip-.006, tip-.001
    data = source.read_bytes()
    assert len(data) == 84+50*struct.unpack_from('<I', data, 80)[0]
    triangles, changed = [], False
    for row in struct.iter_unpack('<12fH', data[84:]):
        triangle = [world([row[offset+k]*scale[k] for k in range(3)], frame) for offset in (3, 6, 9)]
        if max(dot(p, beam) for p in triangle) <= start+1e-10:
            triangles.append(tuple(tuple(row[offset:offset+3]) for offset in (3, 6, 9)))
            continue
        changed = True
        polygons = [triangle]
        stations = [start, end]
        for station in nozzle_sections(tcp, beam)[1:-1]:
            # Inverse of the axial extension, so each triangle stays inside
            # one of the fitted collision slabs after deformation.
            stations.append(station-EXTENSION if station >= end+EXTENSION else start+(station-start)/(1+EXTENSION/(end-start)))
        for station in sorted(set(stations)):
            polygons = [part for poly in polygons for part in split(poly, beam, station)]
        for poly in polygons:
            mapped = []
            for p in poly:
                fraction = max(0., min(1., (dot(p, beam)-start)/(end-start)))
                station = dot(p, beam)
                advanced = station+EXTENSION*fraction
                source_radius = profile[0]+(profile[1]-profile[0])*max(0., min(1., (station-start)/.006))
                target_radius = profile[0]+(TIP_RADIUS-profile[0])*max(0., min(1., (advanced-start)/(.006+EXTENSION)))
                center = [tcp[k]+beam[k]*(advanced-tip) for k in range(3)]
                radial = [p[k]-tcp[k]-beam[k]*(station-tip) for k in range(3)]
                moved = local([center[k]+radial[k]*target_radius/source_radius for k in range(3)], frame)
                mapped.append(tuple(moved[k]/scale[k] for k in range(3)))
            triangles.extend((mapped[0], mapped[i], mapped[i+1]) for i in range(1, len(mapped)-1))
    if not changed:
        shutil.copyfile(source, destination)
        return
    triangles = [(n, t) for t in triangles if (n := normal(t)) is not None]
    with destination.open('wb') as stream:
        stream.write(b'iCAX standalone head - nozzle extended 80 mm'.ljust(80, b'\0'))
        stream.write(struct.pack('<I', len(triangles)))
        for n, triangle in triangles:
            stream.write(struct.pack('<12fH', *n, *(v for p in triangle for v in p), 0))


def extend_tip_box(tool, tcp, beam, profile):
    count = 0
    for collision in tool.findall('collision'):
        frame = pose(collision.find('origin'))
        box = collision.find('geometry/box')
        assert box is not None
        size = vector(box.get('size'))
        p = local(tcp, frame)
        if not all(abs(p[k]) <= size[k]/2+1e-8 for k in range(3)):
            continue
        t, r = frame
        direction = [sum(r[k][i]*beam[k] for k in range(3)) for i in range(3)]
        axis = max(range(3), key=lambda k: abs(direction[k]))
        assert abs(abs(direction[axis])-1) < 1e-8
        assert .005 < size[axis] < .007
        tool.remove(collision)
        tip = dot(tcp, beam)
        sections = nozzle_sections(tcp, beam)
        for index, (a, b) in enumerate(zip(sections, sections[1:]), 1):
            radius = profile[0]+(TIP_RADIUS-profile[0])*(a-sections[0])/(sections[-1]-sections[0])
            slab = ET.SubElement(tool, 'collision', name=collision.get('name')+f'_{index:02}')
            ET.SubElement(slab, 'origin', xyz=text([tcp[k]+beam[k]*((a+b)/2-tip) for k in range(3)]))
            geometry = ET.SubElement(slab, 'geometry')
            ET.SubElement(geometry, 'box', size=text([2*radius+.000002, 2*radius+.000002, b-a+.000002]))
        count += 1
    assert count == 1, 'Exactly one narrow nozzle box must contain the TCP'


def build():
    for spec in json.loads((GANTRY/'catalog.json').read_text(encoding='utf-8')):
        kind = {'4+1': 'A', '3+2': 'Fixed'}.get(spec['kind'], spec['kind'])
        source = GANTRY/spec['model']/(spec['model']+'.urdf')
        source_robot = ET.parse(source).getroot()
        links = {link.get('name'): link for link in source_robot.findall('link')}
        parents = {joint.find('child').get('link'): joint for joint in source_robot.findall('joint')}
        chain, name = [], spec['toolLink']
        while True:
            chain.append(name)
            if name == spec['headRoot']:
                break
            name = parents[name].find('parent').get('link')
        chain.reverse()
        robot = ET.Element('robot', name=kind+' 长嘴切割头')
        # The root's incoming joint owns its rotary axis (C for AC/BC).
        # Keep that joint on a geometry-free mount without importing the machine.
        mount_name = '刀头安装基准'
        ET.SubElement(robot, 'link', name=mount_name)
        root_joint = copy.deepcopy(parents[chain[0]])
        root_joint.find('parent').set('link', mount_name)
        robot.append(root_joint)
        folder = ROOT/kind
        (folder/'meshes').mkdir(parents=True, exist_ok=True)
        for name in chain:
            link = copy.deepcopy(links[name])
            robot.append(link)
            if name != chain[0]:
                robot.append(copy.deepcopy(parents[name]))
            tool = link.find('icax_tool')
            if tool is not None:
                tcp = [v/1000 for v in vector(tool.get('tcp_mm'))]
                beam = vector(tool.get('beam'))
                assert abs(dot(beam, beam)-1) < 1e-9
                profile = nozzle_profile(source.parent, link, tcp, beam)
                extend_tip_box(link, tcp, beam, profile)
            for visual in link.findall('visual'):
                mesh = visual.find('geometry/mesh')
                relative = Path(mesh.get('filename'))
                destination = folder/relative
                if tool is None:
                    shutil.copyfile(source.parent/relative, destination)
                else:
                    extend_mesh(source.parent/relative, destination, visual, tcp, beam, profile)
            if tool is not None:
                tool.set('tcp_mm', text([(tcp[k]+beam[k]*(EXTENSION+VIRTUAL_TIP_OFFSET))*1000 for k in range(3)]))
        ET.indent(robot)
        ET.ElementTree(robot).write(folder/(kind+'.urdf'), encoding='utf-8', xml_declaration=True)


if __name__ == '__main__':
    build()
