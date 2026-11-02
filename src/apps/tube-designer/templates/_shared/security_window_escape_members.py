"""Lay out an escape opening with continuous rails and square-ended jambs."""
from copy import deepcopy
import math


def _sub(a, b):
    return [x-y for x, y in zip(a, b)]


def _dot(a, b):
    return sum(x*y for x, y in zip(a, b))


def _span(segment, start, end):
    delta = _sub(start, segment['start'])
    segment['placement']['origin'] = [x+d for x, d in zip(segment['placement']['origin'], delta)]
    segment['start'], segment['end'] = list(start), list(end)


def arrange(source, segments, parameters):
    """Change product members before publishing either display or manufacturing.

    The complete horizontal row is one member with one tube definition. A
    boundary between the grid and the opening is not a physical stock joint.
    The opening rail's selected tube therefore owns that entire row. Empty
    space alone does not introduce a new full-width horizontal member.
    """
    opening = source.get('extensions', {}).get('tubeDesigner.escapeWindowLayout')
    if not opening:
        return
    surface = opening.get('surface', {})
    origin = surface.get('origin', [0., 0., 0.])
    u = surface.get('uAxis', [1., 0., 0.])
    v = surface.get('vAxis', [0., 0., 1.])

    def point(x, z):
        return [o+x*a+z*b for o, a, b in zip(origin, u, v)]

    clear = opening['clearBounds']
    anchor = point((clear['left']+clear['right'])/2, clear['top'])
    annotations = source['extensions'].setdefault('tubeDesigner.specificationAnnotations', [])
    annotations.append({
        'id': 'security-window.escape-window.doorPosition',
        'parameter': 'doorPosition', 'kind': 'parameter',
        'start': anchor, 'end': list(anchor),
        'offset': [max(68., float(parameters['doorFrameWidth'])*2.2)*axis for axis in v],
        'generatedValue': parameters['doorPosition'],
    })

    rails, removed = [], set()
    for item in source['items']:
        key = item['key']
        if not key.startswith(('access_door.fixed_frame.bottom.', 'access_door.fixed_frame.top.')):
            continue
        segment = segments[key]
        height = _dot(_sub(segment['start'], origin), v)
        reference = point(0., height)
        original = sorted(_dot(_sub(p, origin), u) for p in (segment['start'], segment['end']))
        candidates = []
        for other, candidate in segments.items():
            if not other.startswith(('main_grid.', 'side_grid.', 'cap_grid.')):
                continue
            a, b = candidate['start'], candidate['end']
            length = math.dist(a, b)
            if length <= 1.e-7 or abs(_dot(_sub(b, a), u))/length < 1.-1.e-7:
                continue
            if all(math.dist(p, point(_dot(_sub(p, origin), u), height)) < 1.e-7 for p in (a, b)):
                candidates.append((other, sorted(_dot(_sub(p, origin), u) for p in (a, b))))
        left, right = original
        # The old stubs stop at the jamb's outside surface. Crossing that
        # existing corner replaces a weld; it does not bridge a free gap.
        allowance = item['properties']['tubeDesigner.profile']['width']/2
        joined = set()
        for other, (a, b) in sorted(candidates, key=lambda c: c[1][0], reverse=True):
            if a < left-1.e-7 and left-allowance-1.e-7 <= b <= left+1.e-7:
                left = a
                joined.add(other)
        for other, (a, b) in sorted(candidates, key=lambda c: c[1][0]):
            if b > right+1.e-7 and right-1.e-7 <= a <= right+allowance+1.e-7:
                right = b
                joined.add(other)
        ends = []
        for other, mate in segments.items():
            if not other.startswith('outer_frame.'):
                continue
            delta = _sub(mate['end'], mate['start'])
            length = math.dist(mate['start'], mate['end'])
            direction = [value/length for value in delta]
            if abs(_dot(direction, v)) < 1.-1.e-7:
                continue
            station = _dot(_sub(reference, mate['start']), direction)
            if not -1.e-7 <= station <= length+1.e-7:
                continue
            hit = [a+station*b for a, b in zip(mate['start'], direction)]
            coordinate = _dot(_sub(hit, reference), u)
            if math.dist(hit, point(coordinate, height)) < 1.e-7:
                ends.append(coordinate)
        # Restore the receiver centreline datum used by end processing. The
        # emitted solid still stops at that receiver's actual material face.
        if left < original[0]-1.e-7 and ends:
            left = min(ends)
        if right > original[1]+1.e-7 and ends:
            right = max(ends)
        if right-left <= 1.e-7:
            raise ValueError('逃生窗贯通横杆不能退化')
        _span(segment, point(left, height), point(right, height))
        item['properties']['tubeDesigner.escapeFrameMember'] = 'through_rail' if joined else 'square_rail'
        if joined:
            item['displayName'] = item['displayName'].removesuffix('下边').removesuffix('上边') + (
                '下贯通横杆' if '.bottom.' in key else '上贯通横杆')
            rails.append(key)
        removed.update(joined)
    for item in source['items']:
        if item['key'].startswith(('access_door.fixed_frame.left.', 'access_door.fixed_frame.right.')):
            item['properties']['tubeDesigner.escapeFrameMember'] = 'square_jamb'
    source['items'] = [item for item in source['items'] if item['key'] not in removed]
    for key in removed:
        del segments[key]
    opening['fixedFrameConstruction'] = {'horizontal': 'one_member_per_continuous_row', 'vertical': 'square_butt',
        'railKeys': rails, 'removedGridStubKeys': sorted(removed)}
    for item in source['items']:
        if 'tubeDesigner.escapeWindowLayout' in item['properties']:
            item['properties']['tubeDesigner.escapeWindowLayout'] = deepcopy(opening)
