"""Exact planar polygon clipping audit of every head STL triangle against its boxes.

This is a geometry envelope audit, not sampled-point collision acceptance.  Each
triangle is successively subtracted by all six half-spaces of each authored box;
remaining positive-area polygons are material outside the union of colliders.
"""
import argparse
import hashlib
import json
import math
import pathlib
import struct
import xml.etree.ElementTree as ET


def area(poly):
    if len(poly) < 3:
        return 0.0
    origin = poly[0]
    total = [0.0, 0.0, 0.0]
    for i in range(1, len(poly) - 1):
        a = [poly[i][k] - origin[k] for k in range(3)]
        b = [poly[i + 1][k] - origin[k] for k in range(3)]
        total[0] += a[1] * b[2] - a[2] * b[1]
        total[1] += a[2] * b[0] - a[0] * b[2]
        total[2] += a[0] * b[1] - a[1] * b[0]
    return math.sqrt(sum(v * v for v in total)) / 2


def clip(poly, axis, bound, sign):
    """Keep sign * (coordinate - bound) <= 0, with exact plane intersections."""
    if not poly:
        return []
    output = []
    previous = poly[-1]
    old = sign * (previous[axis] - bound)
    for point in poly:
        current = sign * (point[axis] - bound)
        if (current <= 0) != (old <= 0):
            fraction = old / (old - current)
            hit = [previous[k] + fraction * (point[k] - previous[k]) for k in range(3)]
            hit[axis] = bound
            output.append(hit)
        if current <= 0:
            output.append(point)
        previous, old = point, current
    return output


def subtract_box(poly, box):
    remaining = poly
    outside = []
    for axis in range(3):
        for bound, sign in ((box['min'][axis], -1), (box['max'][axis], 1)):
            piece = clip(remaining, axis, bound, -sign)
            # A polygon lying exactly on the box boundary is covered. Closed
            # clipping half-spaces otherwise duplicate that entire face outside.
            if any(sign * (p[axis] - bound) > 0 for p in piece) and area(piece) > 1e-10:
                outside.append(piece)
            remaining = clip(remaining, axis, bound, sign)
            if not remaining:
                return outside
    return outside


def triangle_list(path):
    data = path.read_bytes()
    count = struct.unpack_from('<I', data, 80)[0]
    if len(data) != 84 + 50 * count:
        raise ValueError(f'Expected binary STL: {path}')
    for i in range(count):
        values = struct.unpack_from('<12fH', data, 84 + 50 * i)
        yield [[values[start + k] * 1000 for k in range(3)] for start in (3, 6, 9)]


def bounds(points):
    return {'min': [min(p[k] for p in points) for k in range(3)],
            'max': [max(p[k] for p in points) for k in range(3)]}


def audit(urdf_path, link_name):
    robot = ET.parse(urdf_path).getroot()
    link = next(v for v in robot.findall('link') if v.get('name') == link_name)
    boxes = []
    for collision in link.findall('collision'):
        origin = collision.find('origin')
        xyz = [float(v) * 1000 for v in (origin.get('xyz', '0 0 0') if origin is not None else '0 0 0').split()]
        rpy = [float(v) for v in (origin.get('rpy', '0 0 0') if origin is not None else '0 0 0').split()]
        if any(abs(v) > 1e-12 for v in rpy):
            raise ValueError('This authored head has rotated colliders; extend the exact plane implementation before auditing.')
        box = collision.find('geometry/box')
        if box is None:
            raise ValueError('Every head collider must be an explicitly audited box.')
        size = [float(v) * 1000 for v in box.get('size').split()]
        boxes.append({'name': collision.get('name'),
                      'min': [xyz[k] - size[k] / 2 for k in range(3)],
                      'max': [xyz[k] + size[k] / 2 for k in range(3)]})
    reports = []
    for visual in link.findall('visual'):
        origin = visual.find('origin')
        if origin is not None and any(abs(float(v)) > 1e-12 for key in ('xyz', 'rpy') for v in origin.get(key, '0 0 0').split()):
            raise ValueError('This authored visual needs its declared transform applied before auditing.')
        mesh = visual.find('geometry/mesh')
        if any(abs(float(v) - 1) > 1e-12 for v in mesh.get('scale', '1 1 1').split()):
            raise ValueError('Apply the authored mesh scale before auditing a scaled visual.')
        mesh_path = urdf_path.parent / mesh.get('filename')
        raw = mesh_path.read_bytes()
        uncovered_area, count, bad_count, pieces = 0.0, 0, 0, []
        all_points = []
        uncovered_points = []
        for index, triangle in enumerate(triangle_list(mesh_path)):
            count += 1
            all_points.extend(triangle)
            pending = [triangle]
            for box in boxes:
                pending = [piece for poly in pending for piece in subtract_box(poly, box)]
                if not pending:
                    break
            bad = sum(area(poly) for poly in pending)
            if bad > 1e-8:
                bad_count += 1
                uncovered_area += bad
                uncovered_points.extend(p for poly in pending for p in poly)
                if len(pieces) < 40:
                    pieces.append({'triangle': index, 'areaMm2': bad,
                                   'bounds': bounds([p for poly in pending for p in poly]),
                                   'polygons': pending})
        reports.append({'mesh': str(mesh_path), 'sha256': hashlib.sha256(raw).hexdigest(),
                        'triangles': count, 'bounds': bounds(all_points),
                        'uncoveredTriangleCount': bad_count, 'uncoveredAreaMm2': uncovered_area,
                        'uncoveredBounds': bounds(uncovered_points) if uncovered_points else None,
                        'uncoveredSamples': pieces})
    return {'passed': all(r['uncoveredTriangleCount'] == 0 for r in reports),
            'scope': 'Full triangle polygon subtraction by the union of authored collision boxes; includes triangles crossing nozzle slab planes.',
            'urdf': str(urdf_path), 'urdfSha256': hashlib.sha256(urdf_path.read_bytes()).hexdigest(),
            'link': link_name, 'lengthUnit': 'mm', 'positiveAreaThresholdMm2': 1e-8,
            'boxes': boxes, 'meshes': reports}


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--urdf', type=pathlib.Path, required=True)
    parser.add_argument('--output', type=pathlib.Path, required=True)
    parser.add_argument('--link', default='A 激光切割头')
    args = parser.parse_args()
    result = audit(args.urdf, args.link)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding='utf-8')
    print(json.dumps({'passed': result['passed'], 'output': str(args.output),
                      'meshes': [{k: r[k] for k in ('mesh', 'triangles', 'uncoveredTriangleCount', 'uncoveredAreaMm2')} for r in result['meshes']]}, indent=2))
    raise SystemExit(0 if result['passed'] else 1)
