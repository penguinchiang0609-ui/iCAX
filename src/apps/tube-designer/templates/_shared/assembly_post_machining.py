"""End insertion and matching near-wall cuts over actual tube sections.

These functions consume rigid tube facts, without product member names or
layout rules. Round ears and sockets reuse their existing tool geometry.
"""
from copy import deepcopy
import importlib.util
import math
from pathlib import Path


DEPENDENCIES = ['assembly_tube_machining.py', 'section_geometry.py',
                '../mold/paired-end-tabs/tool.py', '../mold/paired-side-slots/tool.py']


def _load(name):
    path = Path(__file__).with_name(name + '.py')
    spec = importlib.util.spec_from_file_location('icax_post_' + name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


machining = _load('assembly_tube_machining')


def _tool(name):
    path = Path(__file__).parent.parent / 'mold' / name / 'tool.py'
    spec = importlib.util.spec_from_file_location('icax_post_tool_' + name.replace('-', '_'), path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.section_geometry = _load('section_geometry')
    return module


def _values(parameters):
    if set(parameters) - {'joint', 'insertionDepth', 'clearance', 'tabWidth'}:
        raise ValueError('立柱插接工艺字段无效')
    joint = parameters.get('joint')
    if joint not in ('insert', 'tabs'):
        raise ValueError('立柱端连接仅接受直接插入或公母插入')
    depth = machining._number(parameters.get('insertionDepth'), '立柱插入深度', .1)
    gap = machining._number(parameters.get('clearance', 0.), '立柱配合间隙', 0.)
    width = machining._number(parameters.get('tabWidth', 0.), '立柱圆头宽度', 0.)
    if joint == 'tabs' and (width <= 0. or depth <= width / 2 + .01):
        raise ValueError('圆头插舌长度须大于半宽')
    return joint, depth, gap, width


def _actual_walls(parts):
    geometry = _load('section_geometry')
    result = {}
    for role, part in parts.items():
        shell = geometry.closed_shell_metrics(geometry.from_profile({'contours': part['contours']}))
        part['wallThickness'] = shell['wallThickness']
        result[role] = shell
    return result


def _append_tool(graph, generated, placement=None):
    if generated.get('mode') != 'solid' or generated.get('coordinateSpace') not in ('part', 'end-local'):
        raise ValueError('立柱插接模具必须给出明确实体坐标')
    nodes = deepcopy(generated['model']['geometry'])
    graph.nodes.extend(nodes)
    root = generated['outputKey']
    if placement is not None:
        root = graph.node('post-tool.placed', 'transform', [root], {'placement': placement})
    return root


def tube_post_end(process_input, parameters):
    """Trim one real post end, retaining the selected round-ear wall pair."""
    def build(graph):
        parts, geometry = machining._input(process_input, ('stock', 'mate'))
        if set(geometry) != {'end', 'station'}:
            raise ValueError('立柱端加工必须给出实际端点与接收轴站位')
        joint, depth, gap, width = _values(parameters)
        stock, mate = parts['stock'], parts['mate']
        _actual_walls(parts)
        local = deepcopy(process_input)
        for role, part in parts.items():
            local['parts'][role]['section']['wallThickness'] = part['wallThickness']
        local['geometry'] = {'mode': 'insert', **geometry}
        result = machining.tube_end_joint(local, {'insertionDepth': depth, 'clearance': gap})
        if not result['applicable']:
            raise ValueError(result['reason'])
        plane = next(item for item in result['checks'] if item['kind'] == 'end-plane')
        tip = plane['allocation']['rawStockStation']
        if not .01 <= tip <= stock['length'] - .01:
            raise ValueError('立柱插入深度超出原材端点余量，不能越过接收管中心')
        if joint == 'insert':
            graph.nodes.extend(result['geometry'])
            return {'operations': deepcopy(result['operations']), 'checks': deepcopy(result['checks'])}
        end = geometry['end']
        trim = tip if end == 'start' else stock['length'] - tip
        own = deepcopy(process_input['parts']['stock']['section'])
        own = own.get('profile', own)
        partner = deepcopy(process_input['parts']['mate']['section'])
        partner = partner.get('profile', partner)
        generated = _tool('paired-end-tabs').generate(
            {'pairCount': 2, 'tabWidth': width, 'tabLength': depth},
            {'targetSection': own, 'section': {'profile': partner}, 'end': end,
             'bounds': {'min': [0., *stock['bounds']['min']], 'max': [stock['length'], *stock['bounds']['max']]},
             'placement': {'trim': trim, 'rotation': 0.}})
        sign = 1. if end == 'start' else -1.
        placement = {'origin': [tip, 0., 0.], 'xAxis': [sign, 0., 0.],
                     'yAxis': [0., 1., 0.], 'zAxis': [0., 0., sign]}
        root = _append_tool(graph, generated, placement)
        return {'operations': [{'id': 'stock:post-end', 'kind': 'neutral-csg', 'role': 'stock',
                                'operation': 'subtract', 'tool': root}], 'checks': deepcopy(result['checks'])}
    return machining._result('tube-post-end', process_input, parameters, build)


def tube_post_aperture(process_input, parameters):
    """Cut a post's footprint or two sockets into its actual near host wall."""
    def build(graph):
        parts, geometry = machining._input(process_input, ('stock', 'branch'))
        if set(geometry) != {'node', 'branchEnd', 'sharedCorner', 'keepPlanes'} or type(geometry['sharedCorner']) is not bool:
            raise ValueError('立柱母壁加工必须给出实际节点及支管端点')
        joint, depth, gap, width = _values(parameters)
        stock, branch = parts['stock'], parts['branch']
        shells = _actual_walls(parts)
        end = geometry['branchEnd']
        if end not in ('start', 'end'):
            raise ValueError('立柱母壁缺少实际支管端点')
        node = machining._local(stock['matrix'], machining._vector(geometry['node'], '立柱接点'))
        pose = machining._pose(stock, branch)
        outward = [value * (1. if end == 'start' else -1.) for value in pose['xAxis']]
        if abs(outward[0]) > 1.e-7 or max(abs(outward[1]), abs(outward[2])) < 1 - 1.e-7:
            raise ValueError('立柱须垂直接入承接管的实际平直壁面')
        normal = 1 if abs(outward[1]) > abs(outward[2]) else 2
        face = ('right' if outward[1] > 0 else 'left') if normal == 1 else ('top' if outward[2] > 0 else 'bottom')
        span = stock['bounds']['max'][normal - 1] - stock['bounds']['min'][normal - 1]
        machining._insertion_fit(stock, branch, depth, gap, span)
        # The rectangular restrictions come from the actual inner/outer
        # contours. Neither metadata dimensions nor rebuilt sections suffice.
        host_profile = deepcopy(process_input['parts']['stock']['section'])
        host_profile = host_profile.get('profile', host_profile)
        branch_profile = deepcopy(process_input['parts']['branch']['section'])
        branch_profile = branch_profile.get('profile', branch_profile)
        host_shell = shells['stock']
        if host_shell.get('circularShell'):
            raise ValueError('立柱插接当前需要方矩管的平直承接壁')
        face_bounds = _tool('paired-side-slots')._face(host_shell, face, {
            'min': [0., *stock['bounds']['min']], 'max': [stock['length'], *stock['bounds']['max']]})
        envelope = machining._envelope(branch, pose, gap)
        across = 3 - normal
        if joint == 'insert' and (envelope['min'][across] <= face_bounds['flat'][0] + .01
                                 or envelope['max'][across] >= face_bounds['flat'][1] - .01):
            raise ValueError('立柱轮廓孔超出承接面的实际内外共同平直壁料')
        outside = stock['bounds']['max'][normal - 1] if outward[normal] > 0 else stock['bounds']['min'][normal - 1]
        reach = face_bounds['wall'] + .05
        if joint == 'insert':
            outer = deepcopy(branch['contours'][0])
            if gap:
                outer = machining._load('tube_profile_catalog')._offset_outer_contour(outer, gap)
            origin = list(node)
            origin[normal] = outside + outward[normal] * .025
            profile = graph.node('post-aperture.profile', 'profile2d', arguments={
                'placement': {'origin': origin, 'xAxis': pose['yAxis'], 'yAxis': pose['zAxis']}, 'contours': [outer]})
            root = graph.node('post-aperture.solid', 'extrude', [profile],
                              {'vector': [-reach * value for value in outward]})
        else:
            z = pose['zAxis']
            if abs(z[normal]) > 1.e-7 or max(abs(z[0]), abs(z[3 - normal])) < 1 - 1.e-7:
                raise ValueError('实际立柱公舌壁面须与承接面轴向或横向对齐')
            # Shell metrics normalize the saved contour around its bounds;
            # retain the actual section datum when locating the sockets.
            center = [(branch['bounds']['min'][i] + branch['bounds']['max'][i]) / 2 for i in (0, 1)]
            socket_center = [node[i] + pose['yAxis'][i] * center[0] + pose['zAxis'][i] * center[1] for i in range(3)]
            generated = _tool('paired-side-slots').generate(
                {'pairCount': 2, 'tabWidth': width, 'tabLength': depth, 'sideClearance': gap,
                 'pairRotation': math.degrees(math.atan2(-z[0], z[3 - normal])), 'allowEndOpening': False},
                {'targetSection': host_profile, 'feature': {'face': face, 'reference': 'start',
                 'station': socket_center[0], 'offset': socket_center[3 - normal] - face_bounds['center'], 'section': {'profile': branch_profile}},
                 'bounds': {'min': [0., *stock['bounds']['min']], 'max': [stock['length'], *stock['bounds']['max']]},
                 'placement': {'rotation': 0.}})
            if geometry['sharedCorner']:
                planes = geometry['keepPlanes']
                if not isinstance(planes, list) or not planes:
                    raise ValueError('共享拼角母槽须观察实际45度保留端面')
                slot_nodes = {item['key']: item for item in generated['model']['geometry']}
                retained = 0
                for key, item in slot_nodes.items():
                    if item['operator'] != 'profile2d':
                        continue
                    placement = item['arguments']['placement']
                    center, u, v = (placement[name] for name in ('origin', 'xAxis', 'yAxis'))
                    vector = slot_nodes[key.removesuffix('-profile')]['arguments']['vector']
                    # Read each generated capsule's real short radius; an
                    # eccentric inner boundary can give the two skins
                    # different thicknesses without moving their datum.
                    radius = abs(item['arguments']['contours'][0]['segments'][0]['start'][1])
                    half_line = width / 2
                    intervals = []
                    for plane in planes:
                        n = machining.unit(plane['inwardNormal'])
                        p = machining._vector(plane['origin'], '拼角端面')
                        distance = machining.dot([a-b for a,b in zip(center, p)], n)
                        support = half_line * abs(machining.dot(u, n)) + radius * math.hypot(machining.dot(u, n), machining.dot(v, n))
                        advance = machining.dot(vector, n)
                        intervals.append((distance-support+min(0.,advance), distance+support+max(0.,advance)))
                    if any(high <= .01 for low, high in intervals):
                        continue  # This ear belongs entirely to the other receiver.
                    if any(low <= .01 for low, high in intervals):
                        raise ValueError('公母插槽越过实际45度拼角接缝，无法保留完整母槽桥料')
                    retained += 1
                if not retained:
                    raise ValueError('共享拼角承接梁没有完整保留的实际母槽')
            root = _append_tool(graph, generated)
        if not geometry['sharedCorner']:
            # A terminal receiver owns the entire opening. At a true miter
            # corner, each of two separately trimmed receivers owns one half.
            if envelope['min'][0] <= .01 or envelope['max'][0] >= stock['length'] - .01:
                raise ValueError('端部立柱的完整母孔须保留承接梁远侧壁料')
        return {'operations': [{'id': 'stock:post-aperture', 'kind': 'neutral-csg', 'role': 'stock',
                                'operation': 'subtract', 'tool': root}],
                'checks': [{'kind': 'post-aperture', 'joint': joint, 'face': face, 'node': node,
                            'insertion': depth, 'clearance': gap}]}
    return machining._result('tube-post-aperture', process_input, parameters, build)


FUNCTIONS = {'tube-post-end': tube_post_end, 'tube-post-aperture': tube_post_aperture}
