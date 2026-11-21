"""Exact section observations for the grille's straight spatial members.

No stock is generated here. Line/circle/ellipse intersections use the actual
section contours; unsupported boundaries fail rather than being tessellated.
"""
import importlib.util
import math
from pathlib import Path

EPS = 1.e-7


def dot(a, b):
    return sum(x*y for x, y in zip(a, b))


def sub(a, b):
    return [x-y for x, y in zip(a, b)]


def add(a, b, scale=1.):
    return [x+scale*y for x, y in zip(a, b)]


def cross(a, b):
    return [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]]


def vector(value):
    if (not isinstance(value, (list, tuple)) or len(value) != 3
            or any(isinstance(v, bool) or not isinstance(v, (int, float))
                   or not math.isfinite(v) for v in value)):
        raise ValueError('空间构件坐标必须为有限三维坐标')
    return list(map(float, value))


def unit(value):
    value = vector(value)
    length = math.sqrt(dot(value, value))
    if length <= EPS:
        raise ValueError('空间构件方向不能为零')
    return [v/length for v in value]


def axes(member):
    start, end = vector(member['start']), vector(member['end'])
    axis = unit(sub(end, start))
    length = math.dist(start, end)
    frame = member['sectionFrame']
    x, y, z = [vector(frame[name]) for name in ('xAxis', 'yAxis', 'zAxis')]
    if (math.dist(vector(frame['originAtStart']), start) > EPS
            or any(abs(dot(a, a)-1) > EPS for a in (x, y, z))
            or abs(dot(x, y)) > EPS or math.dist(cross(x, y), z) > EPS
            or math.dist(axis, z) > EPS
            or any(abs(v) > EPS for v in frame.get('centerlineUV', (0., 0.)))):
        raise ValueError('空间构件截面坐标系必须与实际起点和轴线一致')
    return start, end, axis, length, x, y


def _section_geometry():
    path = Path(__file__).resolve().parents[2]/'_shared'/'section_geometry.py'
    spec = importlib.util.spec_from_file_location('minimal_grille_spatial_section', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def ray_intersections(member, origin, direction, *, inner=False):
    """Exact axial distances where a spatial line meets one extruded contour."""
    start, _, _, _, x, y = axes(member)
    delta = sub(vector(origin), start)
    p = [dot(delta, a) for a in (x, y)]
    d = [dot(vector(direction), a) for a in (x, y)]
    if dot(d, d) <= EPS*EPS:
        raise ValueError('连接射线与接收管轴线平行，不能形成侧面插接')
    geometry = _section_geometry()
    loops = geometry.from_profile({'contours': member['profile'].contours()}, tolerance=EPS)['contours']
    if inner and len(loops) != 2:
        raise ValueError('公母插接需要真实的闭口管内轮廓')
    loop = loops[1 if inner else 0]
    roots = []
    for edge in loop['edges']:
        if edge['kind'] == 'line':
            a, b = edge['start'], edge['end']
            e, q = sub(b, a), sub(a, p)
            determinant = d[0]*e[1]-d[1]*e[0]
            if abs(determinant) <= 1.e-14:
                continue
            t = (q[0]*e[1]-q[1]*e[0])/determinant
            fraction = (q[0]*d[1]-q[1]*d[0])/determinant
            if -EPS <= fraction <= 1.+EPS:
                roots.append(t)
        elif edge['kind'] in ('circleArc', 'ellipseArc'):
            q = sub(p, edge['center'])
            radii = ([edge['radius']]*2 if edge['kind'] == 'circleArc'
                     else [edge['majorRadius'], edge['minorRadius']])
            o = [dot(q, a)/r for a, r in zip((edge['xAxis'], edge['yAxis']), radii)]
            v = [dot(d, a)/r for a, r in zip((edge['xAxis'], edge['yAxis']), radii)]
            aa, bb, cc = dot(v, v), 2*dot(o, v), dot(o, o)-1
            discriminant = bb*bb-4*aa*cc
            if discriminant < -1.e-12:
                continue
            for t in ((-bb-math.sqrt(max(0., discriminant)))/(2*aa),
                      (-bb+math.sqrt(max(0., discriminant)))/(2*aa)):
                angle = math.atan2(o[1]+t*v[1], o[0]+t*v[0])
                if geometry.in_arc_range(edge, angle):
                    roots.append(t)
        else:
            raise ValueError('当前空间插接不支持该真实截面曲线的精确求交')
    unique = []
    for value in sorted(roots):
        if not unique or abs(value-unique[-1]) > EPS:
            unique.append(value)
    if len(unique) != 2:
        raise ValueError('连接方向必须穿过接收管完整截面，不能与轮廓相切或穿过多个独立区域')
    return unique


def contact_offset(host, center, outward_direction, *, allow_off_axis=False):
    """Return the true near skin along a branch line through the receiver.

    Mixed sections may align their outside faces instead of their axes. The
    optional off-axis seed still has to lie inside the real outer contour;
    the actual curve intersection, not a box face, determines the endpoint.
    """
    start, _, axis, length, _, _ = axes(host)
    center = vector(center)
    station = dot(sub(center, start), axis)
    if ((not allow_off_axis and math.dist(add(start, axis, station), center) > EPS)
            or not -EPS <= station <= length+EPS):
        raise ValueError('接头参考点必须位于接收管实际长度范围内，普通接头还须位于轴线上')
    direction = unit(outward_direction)
    roots = ray_intersections(host, center, direction)
    if not roots[0] < -EPS < EPS < roots[1]:
        raise ValueError('接头轴线中心不在接收管闭合外轮廓内')
    point = add(center, direction, roots[1])
    contact_station = dot(sub(point, start), axis)
    if not -EPS <= contact_station <= length+EPS:
        raise ValueError('斜接点超出接收管端部，不能作为侧面插接')
    return point
