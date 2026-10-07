"""Stable installed frame ends, independent of manufacturing choices.

The product keeps its logical centre lines. Display solids alone extend to
the corner's long points and share complementary planar end faces. Original
section curves, including the inside contour and round corners, stay exact.
"""
from copy import deepcopy
import itertools
import math


def _sub(a, b):
    return [x-y for x, y in zip(a, b)]


def _dot(a, b):
    return sum(x*y for x, y in zip(a, b))


def _cross(a, b):
    return [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]]


def _unit(a):
    length = math.sqrt(_dot(a, a))
    if length <= 1.e-9:
        raise ValueError('显示框件的方向不能退化')
    return [0. if abs(x/length) < 1.e-12 else x/length for x in a]


def _family(key, face, layout):
    if key.startswith('outer_frame.'):
        return 'outer_frame' if face != 'single' or layout == 'four_sides' else None
    for prefix in ('access_door.fixed_frame.', 'access_door.leaf.frame.'):
        if key.startswith(prefix):
            return prefix
    return None


def frame_end_layout(segments, *, face, layout, support_interval):
    """Only frame neighbours participate; infill rods never trim the frame."""
    families = {}
    for key, segment in segments.items():
        family = _family(key, face, layout)
        if family:
            families.setdefault(family, []).append(key)
    result = {}
    for family, keys in families.items():
        ends = [(key, end, segments[key][end]) for key in keys for end in ('start', 'end')]
        for key in keys:
            segment = segments[key]
            axis = _unit(_sub(segment['end'], segment['start']))
            length = math.dist(segment['start'], segment['end'])
            spec = {'start': 0., 'end': length, 'planes': []}
            multi_outer = family == 'outer_frame' and face != 'single'
            post = multi_outer and key.startswith('outer_frame.vertical.')
            for end in ('start', 'end'):
                point = segment[end]
                inward = axis if end == 'start' else [-v for v in axis]
                neighbours = [(other, side) for other, side, node in ends
                              if other != key and math.dist(node, point) < 1.e-7]
                if multi_outer:
                    neighbours = [(other, side) for other, side in neighbours
                                  if not other.startswith('outer_frame.vertical.')]
                if post:
                    # The horizontal perimeter covers the post. Its visible
                    # end meets the perimeter's inside surface, not its axis.
                    if neighbours:
                        inset = max(support_interval(segments[other], inward, point)[1]
                                    for other, _ in neighbours)
                        spec[end] = inset if end == 'start' else length-inset
                    continue
                if neighbours:
                    if len(neighbours) != 1:
                        raise ValueError('显示框角没有唯一的相邻框边: '+key)
                    other, side = neighbours[0]
                    mate = segments[other]
                    outgoing = _unit(_sub(mate['end' if side == 'start' else 'start'], mate[side]))
                    if abs(_dot(inward, outgoing)) > 1.-1.e-8:
                        continue
                    normal = _unit(_sub(inward, outgoing))
                    section_at_node = {**segment, 'placement': {**segment['placement'],
                        'origin': [segment['placement']['origin'][i]+point[i]-segment['start'][i]
                                   for i in range(3)]}}
                    _, maximum = support_interval(section_at_node, normal, point)
                    axial = _dot(normal, axis)
                    station = 0. if end == 'start' else length
                    long_point = station-maximum/axial
                    spec[end] = min(0., long_point) if end == 'start' else max(length, long_point)
                    spec['planes'].append({'station': station, 'inward': normal})
                elif multi_outer:
                    # An open horizontal perimeter end wraps its end post.
                    posts = [(other, side) for other, side, node in ends
                             if other.startswith('outer_frame.vertical.') and math.dist(node, point) < 1.e-7]
                    if posts:
                        extent = max(support_interval(segments[other], inward, point)[1]
                                     for other, _ in posts)
                        spec[end] = -extent if end == 'start' else length+extent
            if spec['end'] <= spec['start']:
                raise ValueError('框件显示端面使管段退化: '+key)
            result[key] = spec
    return result


def _cutter(model, key, point, inward, bounds):
    """One exact bounded prism on the excluded side of an end plane."""
    normal = _unit(inward)
    reference = min(([1., 0., 0.], [0., 1., 0.], [0., 0., 1.]), key=lambda axis: abs(_dot(axis, normal)))
    x_axis = _unit(_cross(reference, normal))
    y_axis = _cross(normal, x_axis)
    corners = list(itertools.product(*zip(bounds['min'], bounds['max'])))
    relative = [_sub(corner, point) for corner in corners]
    x_low, x_high = min(_dot(p, x_axis) for p in relative)-1., max(_dot(p, x_axis) for p in relative)+1.
    y_low, y_high = min(_dot(p, y_axis) for p in relative)-1., max(_dot(p, y_axis) for p in relative)+1.
    depth = max(0., -min(_dot(p, normal) for p in relative))+1.
    points = [[x_low, y_low], [x_high, y_low], [x_high, y_high], [x_low, y_high]]
    profile = model.geometry(key+'.profile', 'profile2d', arguments={
        'placement': {'origin': point, 'xAxis': x_axis, 'yAxis': y_axis},
        'contours': [{'kind': 'path', 'closed': True, 'segments': [
            {'kind': 'line', 'start': a, 'end': b} for a, b in zip(points, points[1:]+points[:1])]}]})
    return model.geometry(key+'.solid', 'extrude', inputs=[profile], arguments={'vector': [-v*depth for v in normal]})


def emit_frame_member(model, shared, key, segment, contours, spec, support_interval):
    """Share local finished display prototypes and preserve per-member pose."""
    axis = _unit(_sub(segment['end'], segment['start']))
    section = segment['placement']
    across, normal = _unit(section['xAxis']), _unit(section['yAxis'])
    z_axis = _unit(_cross(across, normal))
    sign = 1. if _dot(z_axis, axis) > 0. else -1.
    local_planes = [{'station': sign*plane['station'],
                     'inward': [_dot(plane['inward'], basis) for basis in (across, normal, z_axis)]}
                    for plane in spec['planes']]
    low, high = spec['start'], spec['end']
    signature = {'contours': contours, 'start': sign*low, 'end': sign*high, 'planes': local_planes}
    def build(prefix):
        stock = shared.emit_tube(prefix+'.tube', profile_arguments={
            'placement': {'origin': [0., 0., sign*low], 'xAxis': [1., 0., 0.], 'yAxis': [0., 1., 0.]},
            'contours': deepcopy(contours)}, extrude_arguments={'vector': [0., 0., sign*(high-low)]})
        if not local_planes:
            return stock
        local_section = {'placement': {'origin': [0., 0., 0.], 'xAxis': [1., 0., 0.], 'yAxis': [0., 1., 0.]},
                         'contours': contours}
        x_min, x_max = support_interval(local_section, [1., 0., 0.], [0., 0., 0.])
        y_min, y_max = support_interval(local_section, [0., 1., 0.], [0., 0., 0.])
        z_min, z_max = sorted((sign*low, sign*high))
        bounds = {'min': [x_min, y_min, z_min], 'max': [x_max, y_max, z_max]}
        tools = [_cutter(model, prefix+'.end.'+str(index), [0., 0., plane['station']], plane['inward'], bounds)
                 for index, plane in enumerate(local_planes)]
        return model.geometry(prefix+'.ends', 'boolean', inputs=[stock, *tools], arguments={'operation': 'subtract'})
    local = shared.emit_translated_processed(key+'.display-frame', signature=signature, origin=[0., 0., 0.], build=build)
    return model.geometry(key+'.display-frame.placed', 'transform', inputs=[local], arguments={'placement': {
        'origin': deepcopy(segment['start']), 'xAxis': across, 'yAxis': normal, 'zAxis': z_axis}})
