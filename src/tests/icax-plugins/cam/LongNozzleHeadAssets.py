"""Independent audit of the five standalone long-nozzle head assets."""
from pathlib import Path
from collections import Counter
import hashlib
import json
import math
import xml.etree.ElementTree as ET
from GantryHeadAssets import vec, origin, point, inverse, triangles

ROOT = Path(__file__).resolve().parents[4]
ASSETS = ROOT/'src/apps/five-axis-cam/resources'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def signature(node):
    return node.tag, sorted(node.attrib.items()), (node.text or '').strip(), [signature(c) for c in node]


def audit(spec, original):
    source = ROOT/'src'/spec['sourcePath']
    baseline = ASSETS/'machines/gantry-heads'/original['model']/(original['model']+'.urdf')
    robot, old = [ET.parse(p).getroot() for p in (source, baseline)]
    links = {n.get('name'): n for n in robot.findall('link')}
    old_links = {n.get('name'): n for n in old.findall('link')}
    joints = robot.findall('joint')
    assert all(j.get('type') in ('fixed', 'revolute', 'continuous') for j in joints)
    assert sorted(j.get('name') for j in joints if j.get('type') != 'fixed') == sorted(spec['headAxes'])
    assert len(joints) == len(links)-1
    assert spec['headRoot'] in links
    assert not any(n.find('icax_work_support') is not None for n in links.values())
    for joint in joints:
        before = old.find(f"joint[@name='{joint.get('name')}']")
        if joint.find('child').get('link') == spec['headRoot']:
            before.find('parent').set('link', '刀头安装基准')
        assert signature(joint) == signature(before)
    tool_name = original['toolLink']
    tool = links[tool_name].find('icax_tool')
    old_tcp = vec(old_links[tool_name].find('icax_tool').get('tcp_mm'))
    tcp, beam = vec(tool.get('tcp_mm')), vec(tool.get('beam'))
    physical_tip = [old_tcp[k]+80*beam[k] for k in range(3)]
    assert all(abs(tcp[k]-physical_tip[k]-.2*beam[k]) < 1e-8 for k in range(3))
    vertices, checked, unchanged, altered, edges = [], 0, [], [], Counter()
    for name, link in links.items():
        if name == '刀头安装基准':
            assert not link.findall('visual') and not link.findall('collision')
            continue
        boxes = []
        for c in link.findall('collision'):
            size = vec(c.find('geometry/box').get('size'))
            assert min(size) > 0
            boxes.append((inverse(origin(c.find('origin'))), [v/2+1e-8 for v in size]))
            old_c = old_links[name].find(f"collision[@name='{c.get('name')}']")
            if old_c is None:
                assert name == tool_name
                assert c.get('name').rsplit('_', 1)[0] in [v.get('name') for v in old_links[name].findall('collision')]
                assert size[2] <= .0020021
                assert max(size[:2]) <= .014003
                altered.append(c.get('name'))
            else:
                assert signature(c) == signature(old_c)
        for visual in link.findall('visual'):
            mesh = visual.find('geometry/mesh')
            asset = source.parent/mesh.get('filename')
            old_visual = old_links[name].find(f"visual[@name='{visual.get('name')}']")
            original_asset = baseline.parent/old_visual.find('geometry/mesh').get('filename')
            assert signature(visual) == signature(old_visual)
            transform = origin(visual.find('origin'))
            scale = vec(mesh.get('scale', '1 1 1'))
            if name != tool_name or not any(color in visual.get('name') for color in ('copper', 'black')):
                assert digest(asset) == digest(original_asset), visual.get('name')
                unchanged.append(visual.get('name'))
            for tri in triangles(asset):
                pts = [point(transform, [p[k]*scale[k] for k in range(3)]) for p in tri]
                assert all(math.isfinite(v) for p in pts for v in p)
                assert any(all(abs(v) <= half[k] for p in pts for k, v in enumerate(point(inv, p))) for inv, half in boxes), (spec['kind'], name, pts)
                checked += 1
                if name == tool_name:
                    vertices.extend(pts)
                    # Materials share seams; audit the complete nozzle surface.
                    keys = [tuple(round(v/1e-7) for v in p) for p in pts]
                    # Welding float32 seams can collapse submicron facets.
                    if len(set(keys)) < 3:
                        continue
                    for a, b in zip(keys, keys[1:]+keys[:1]):
                        edges[tuple(sorted((a, b)))] += 1
    assert len(altered) >= 43
    assert all(count == 2 for count in edges.values()), (spec['kind'], Counter(edges.values()))
    tip = min(p[2] for p in vertices)
    assert abs(tip*1000-physical_tip[2]) < 2e-5
    end = [p for p in vertices if abs(p[2]-tip) < 1e-7]
    for k in (0, 1):
        assert abs((min(p[k] for p in end)+max(p[k] for p in end))*500-tcp[k]) < 2e-5
    end_radius = max(math.hypot(p[0]*1000-tcp[0], p[1]*1000-tcp[1]) for p in end)
    assert abs(end_radius-.5) < 2e-5
    # No wide box may fill the working clearance beside the fine tip.
    for c in links[tool_name].findall('collision'):
        position = vec(c.find('origin').get('xyz'))
        size = vec(c.find('geometry/box').get('size'))
        if position[2]-size[2]/2 < tip+.0001:
            assert max(size[:2]) < .0014
    # The entire added 80 mm is a narrow neck, not a stretched main housing.
    extended = [p for p in vertices if p[2]*1000 < old_tcp[2]-1]
    radius = max(math.hypot(p[0]*1000-tcp[0], p[1]*1000-tcp[1]) for p in extended)
    assert radius <= 7.001
    return dict(kind=spec['kind'],physicalTipMm=physical_tip,virtualTcpMm=tcp,virtualTipOffsetMm=.2,extensionMm=80,narrowRadiusMm=radius,
                headParts=len(links)-1,coveredTriangles=checked,closedSurfaceEdges=len(edges),
                nozzleSlabs=len(altered),tipDiameterMm=2*end_radius,unchangedMeshes=len(unchanged))


if __name__ == '__main__':
    manifest = json.loads((ROOT/'src/apps/five-axis-cam/product.manifest.json').read_text(encoding='utf-8'))
    originals = json.loads((ASSETS/'machines/gantry-heads/catalog.json').read_text(encoding='utf-8'))
    originals = {dict([('4+1', 'A'), ('3+2', 'Fixed')]).get(s['kind'], s['kind']): s for s in originals}
    results = [audit(s, originals[s['kind']]) for s in manifest['capabilities']['cuttingHead']['defaults']]
    output = ROOT/'output/acceptance/long-nozzle-heads-20261102'
    output.mkdir(parents=True, exist_ok=True)
    (output/'assets.json').write_text(json.dumps(dict(passed=True,heads=results), ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps(results, ensure_ascii=False))
