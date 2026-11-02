"""Read-only shipped-asset audit: duplicate/degenerate faces and planar overlaps.

The surface intersection check is independent of the Boolean asset generator.
All calculations use millimetres. Coplanarity tolerance is 0.0005 mm (STL
float32 roundoff); positive overlap must exceed 0.01 square millimetres.
"""
import argparse
from collections import defaultdict, Counter
import json
from pathlib import Path
import struct
import xml.etree.ElementTree as ET
import numpy as np

ROOT = Path(__file__).resolve().parents[4]

def cross(a, b):
    return a[0] * b[1] - a[1] * b[0]

def overlap(a, b):
    polygon = list(a)
    if cross(b[1] - b[0], b[2] - b[0]) < 0:
        b = b[::-1]
    for u, v in zip(b, np.roll(b, -1, axis=0)):
        result = []
        for p, q in zip(polygon, polygon[1:] + polygon[:1]):
            dp, dq = cross(v-u, p-u), cross(v-u, q-u)
            if dp >= 0:
                result.append(p)
            if (dp > 0 and dq < 0) or (dp < 0 and dq > 0):
                result.append(p+(q-p)*dp/(dp-dq))
        polygon = result
        if len(polygon) < 3:
            return 0
    # Relative coordinates avoid loss of precision far from the origin.
    return abs(sum(cross(polygon[i]-polygon[0], polygon[i+1]-polygon[0])
                   for i in range(1, len(polygon)-1))) / 2

def triangles(path):
    data = path.read_bytes()
    count = struct.unpack_from('<I', data, 80)[0]
    assert len(data) == 84 + count * 50, path
    return np.array([struct.unpack_from('<9f', data, 96+i*50)
                     for i in range(count)]).reshape(-1, 3, 3) * 1000

def audit_link(folder, link):
    files = [v.find('geometry/mesh').get('filename') or v.findtext('geometry/mesh/uri')
             for v in link.findall('visual')]
    arrays = [triangles(folder / filename) for filename in files]
    names = [filename for filename, array in zip(files, arrays) for _ in array]
    t = np.concatenate(arrays)
    n = np.cross(t[:, 1]-t[:, 0], t[:, 2]-t[:, 0])
    lengths = np.linalg.norm(n, axis=1)
    n /= np.maximum(lengths[:, None], 1e-30)
    axis = np.argmax(np.abs(n), axis=1)
    n *= np.sign(n[np.arange(len(t)), axis])[:, None]
    d = np.sum(n * t[:, 0], axis=1)
    planes = defaultdict(list)
    duplicates, seen, edges = 0, set(), Counter()
    for i, face in enumerate(t):
        vertices = [tuple(p) for p in face]
        key = tuple(sorted(vertices))
        duplicates += key in seen
        seen.add(key)
        for a, b in zip(vertices, vertices[1:] + vertices[:1]):
            edges[tuple(sorted((a, b)))] += 1
        planes[(*np.round(n[i], 5), round(d[i], 3))].append(i)
    lower, upper = t.min(axis=1), t.max(axis=1)
    overlaps = []
    for indices in planes.values():
        ix = np.array(indices)
        dims = [k for k in range(3) if k != axis[ix[0]]]
        for off, i in enumerate(ix[:-1]):
            candidates = ix[off+1:]
            candidates = candidates[np.all(np.minimum(upper[candidates][:, dims], upper[i, dims])
                         - np.maximum(lower[candidates][:, dims], lower[i, dims]) > 1e-6, axis=1)]
            for j in candidates:
                if abs(np.dot(n[i], n[j])) < 1-1e-9 or np.max(np.abs(t[j] @ n[i]-d[i])) > .0005:
                    continue
                area = overlap(t[i][:, dims], t[j][:, dims]) / abs(n[i, axis[i]])
                if area > .01:
                    overlaps.append({'faces': [int(i), int(j)], 'files': [names[i], names[j]], 'areaMm2': area})
    return {'name': link.get('name'), 'triangles': len(t), 'duplicateFaces': duplicates,
            'degenerateFaces': int(sum(lengths < 1e-10)),
            'nonManifoldEdges': sum(v != 2 for v in edges.values()),
            'coplanarOverlapPairs': len(overlaps), 'examples': overlaps[:12]}

def audit(root, models=None):
    results = []
    sources = sorted([*root.glob('*/*.urdf'), *root.glob('*/*.sdf')])
    if models:
        sources = [source for source in sources if source.parent.name in models]
        assert {source.parent.name for source in sources} == set(models), 'Missing requested machine asset'
    assert sources, 'No machine definitions found'
    for source in sources:
        model, folder = source.parent.name, source.parent
        document = ET.parse(source).getroot()
        links = document.findall('link') or document.findall('model/link')
        results.append({'model': model, 'links': [audit_link(folder, link) for link in links]})
    return results

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=Path, default=ROOT/'src/apps/five-axis-cam/resources/machines')
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--report-only', action='store_true')
    parser.add_argument('--model', action='append', help='Audit only this machine folder; repeat for several')
    args = parser.parse_args()
    results = audit(args.root, args.model)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding='utf8')
    failures = [dict(model=r['model'], **link) for r in results for link in r['links']
                if any(link[k] for k in ('duplicateFaces', 'degenerateFaces', 'nonManifoldEdges', 'coplanarOverlapPairs'))]
    print(json.dumps({'models': len(results), 'links': sum(len(r['links']) for r in results),
                      'failedLinks': failures}, ensure_ascii=False))
    if not args.report_only:
        assert not failures, f'Surface audit failed; see {args.out}'
