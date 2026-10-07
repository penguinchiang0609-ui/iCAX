"""End insertion and matching near-wall cuts over actual tube sections.

These functions consume rigid tube facts, without product member names or
layout rules. Rectangular ears reuse their established tool geometry; circular
ears and sockets retain the actual curved shell in the same transverse band.
"""
from copy import deepcopy
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import sys


DEPENDENCIES = ['assembly_tube_machining.py', 'section_geometry.py',
                '../mold/paired-end-tabs/tool.py', '../mold/paired-side-slots/tool.py']


def _load(name):
    path = Path(__file__).with_name(name + '.py')
    source = path.read_bytes()
    identity = 'icax_post_' + name + '_' + hashlib.sha256(source).hexdigest()
    if identity not in sys.modules:
        spec = importlib.util.spec_from_file_location(identity, path)
        module = importlib.util.module_from_spec(spec)
        sys.modules[identity] = module
        try:
            exec(compile(source, str(path), 'exec'), module.__dict__)
        except BaseException:
            sys.modules.pop(identity, None)
            raise
    return sys.modules[identity]


machining = _load('assembly_tube_machining')
_WALL_CACHE = {}


def _tool(name):
    path = Path(__file__).parent.parent / 'mold' / name / 'tool.py'
    source = path.read_bytes()
    sections = _load('section_geometry')
    identity = ('icax_post_tool_' + name.replace('-', '_') + '_'
                + hashlib.sha256(source).hexdigest() + '_' + sections.__name__)
    if identity not in sys.modules:
        spec = importlib.util.spec_from_file_location(identity, path)
        module = importlib.util.module_from_spec(spec)
        sys.modules[identity] = module
        try:
            exec(compile(source, str(path), 'exec'), module.__dict__)
            module.section_geometry = sections
        except BaseException:
            sys.modules.pop(identity, None)
            raise
    return sys.modules[identity]


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
    parser = Path(__file__).with_name('profile_recognition_geometry.py')
    parser_digest = hashlib.sha256(parser.read_bytes()).hexdigest()
    result = {}
    for role, part in parts.items():
        # The exact input contour and both analysis scripts form the identity.
        # A previous fully checked shell can be reused, while every caller gets
        # its own result and still performs its joint-specific checks below.
        key = (geometry.__name__, parser_digest, json.dumps(part['contours'], sort_keys=True,
                ensure_ascii=False, separators=(',', ':'), allow_nan=False))
        if key not in _WALL_CACHE:
            shell = geometry.closed_shell_metrics(geometry.from_profile({'contours': part['contours']}))
            if len(_WALL_CACHE) >= 256:
                _WALL_CACHE.clear()
            _WALL_CACHE[key] = deepcopy(shell)
        shell = deepcopy(_WALL_CACHE[key])
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


def _tab_depth(receiver, branch, depth, gap):
    # Only the retained thin-wall ears enter the receiver. Their transverse
    # fit is checked by the actual socket tools, not by the whole branch box.
    pose = machining._pose(receiver, branch)
    span = sum(abs(pose['xAxis'][i + 1]) *
               (receiver['bounds']['max'][i] - receiver['bounds']['min'][i]) for i in (0, 1))
    wall = machining._wall(receiver)
    if depth <= wall + gap + 1.e-7 or depth >= span - wall - gap - 1.e-7:
        raise ValueError('公舌长度必须穿过承接近侧壁，并与对侧壁保留间隙')


def _round_shell_band(branch, width, gap=0.):
    """Use the saved circular shell, retaining two separated curved skins."""
    geometry=_load('section_geometry')
    shell=geometry.closed_shell_metrics(geometry.from_profile({'contours':branch['contours']}))
    if not shell.get('circularShell'):
        raise ValueError('圆管公母端需要真实同心圆内外轮廓')
    center=[(branch['bounds']['min'][i]+branch['bounds']['max'][i])/2 for i in (0,1)]
    contours=[{'kind':'circle','center':[value+offset for value,offset in zip(loop['edges'][0]['center'],center)],
               'radius':loop['edges'][0]['radius']} for loop in shell['contours']]
    outer, inner = contours
    if width / 2 + gap >= inner['radius'] - .01:
        raise ValueError('圆管公舌宽度须保留两侧独立弧壁，不能包围整个管口')
    outer['radius'] += gap
    inner['radius'] -= gap
    if inner['radius'] <= .01:
        raise ValueError('公母间隙不能消除圆管内孔')
    return contours


def _round_end_tool(graph, stock, end, tip, depth, width):
    _round_shell_band(stock, width)
    lo, hi = stock['bounds']['min'], stock['bounds']['max']
    reach = 4 * (stock['length'] + sum(b-a for a,b in zip(lo,hi)) + 10)
    sign = 1. if end=='start' else -1.
    center_y = (lo[0]+hi[0])/2
    # The blank's exact annular section intersects this axial rounded band.
    # Thus the two retained ears remain curved original tube walls.
    slab = graph.node('round-ear.slab-profile','profile2d',arguments={
        'placement':{'origin':[tip+sign*(depth-reach)/2,center_y,lo[1]-1.],
                     'xAxis':[sign,0.,0.],'yAxis':[0.,1.,0.]},
        'contours':[_tool('paired-end-tabs')._rectangle(depth+reach,2*reach)]})
    slab = graph.node('round-ear.slab','extrude',[slab],{'vector':[0.,0.,hi[1]-lo[1]+2.]})
    ear = graph.node('round-ear.keep-profile','profile2d',arguments={
        'placement':{'origin':[tip,center_y,lo[1]-1.],
                     'xAxis':[sign,0.,0.],'yAxis':[0.,1.,0.]},
        'contours':[_tool('paired-end-tabs')._round_tab(depth,width)]})
    ear = graph.node('round-ear.keep','extrude',[ear],{'vector':[0.,0.,hi[1]-lo[1]+2.]})
    return graph.node('round-ear.tool','boolean',[slab,ear],{'operation':'subtract'})


def _circle_path(circle):
    center,radius=circle['center'],circle['radius']
    def point(angle):
        return [center[0]+radius*math.cos(angle),center[1]+radius*math.sin(angle)]
    return {'kind':'path','closed':True,'segments':[
        {'kind':'arc','start':point(index*math.pi/2),'middle':point((index+.5)*math.pi/2),
         'end':point((index+1)*math.pi/2)} for index in range(4)]}


def _round_aperture_tool(graph, stock, branch, pose, node, outward, normal, outside, depth, width, gap,
                         end_opening=None, clip_interval=None):
    contours = _round_shell_band(branch,width,gap)
    origin = list(node)
    origin[normal] = outside + outward[normal]*.01
    # Trim the same actual annulus to the ear's tangential band. No flat
    # surrogate is used for its round wall or mating cavity.
    center = contours[0].get('center',[0.,0.])
    radius = contours[0]['radius']+1.
    band_origin = [origin[i]+pose['yAxis'][i]*center[0]+pose['zAxis'][i]*center[1] for i in range(3)]
    # Only this retained band enters. A wider unused portion of the round
    # branch must not reject a valid pair of curved ears.
    extents=[abs(pose['yAxis'][i])*(width/2+gap)+abs(pose['zAxis'][i])*contours[0]['radius'] for i in range(3)]
    envelope={'min':[band_origin[i]-extents[i] for i in range(3)],
              'max':[band_origin[i]+extents[i] for i in range(3)]}
    across = 3-normal
    if (envelope['min'][across] <= stock['bounds']['min'][across-1]+.01 or
            envelope['max'][across] >= stock['bounds']['max'][across-1]-.01):
        raise ValueError('圆管公舌弧壁超出承接管真实外截面')
    # Allocation observes the whole branch section conservatively. Near a
    # seam that section can touch both spans while its narrower retained ear
    # band touches only one. Do not manufacture an empty cutter for the miss.
    if (clip_interval is not None and min(envelope['max'][0],clip_interval[1],stock['length'])
            - max(envelope['min'][0],clip_interval[0],0.)<=1.e-7):
        return None
    # A closed frame's straight seam shares this exact shell slot between its
    # two unfolded ends. Only the explicitly allocated end may open; the
    # opposite end must retain its bridge even for this observation.
    if (envelope['min'][0] <= .01 and end_opening!='start' or
            envelope['max'][0] >= stock['length']-.01 and end_opening!='end'):
        raise ValueError('圆管母槽须保留承接管端部桥料')
    profile = graph.node('round-socket.shell-profile','profile2d',arguments={
        'placement':{'origin':origin,'xAxis':pose['yAxis'],'yAxis':pose['zAxis']},
        'contours':[_circle_path(contour) for contour in contours]})
    inward = [-(depth+.02)*v for v in outward]
    shell = graph.node('round-socket.shell','extrude',[profile],{'vector':inward})
    band = graph.node('round-socket.band-profile','profile2d',arguments={
        'placement':{'origin':band_origin,'xAxis':pose['yAxis'],'yAxis':pose['zAxis']},
        'contours':[_tool('paired-end-tabs')._rectangle(width+2*gap,2*radius)]})
    band = graph.node('round-socket.band','extrude',[band],{'vector':inward})
    return graph.node('round-socket.tool','boolean',[shell,band],{'operation':'intersect'})


def _receiver_interval(stock, geometry):
    if 'clipInterval' not in geometry:
        return None
    interval = geometry['clipInterval']
    if (not isinstance(interval,list) or len(interval)!=2
            or not all(isinstance(value,(int,float)) and not isinstance(value,bool)
                       and math.isfinite(value) for value in interval)
            or interval[0]<-1.e-7 or interval[1]>stock['length']+1.e-7 or interval[1]<=interval[0]):
        raise ValueError('母槽接收跨度无效')
    return interval


def _clip_receiver_span(graph, root, stock, geometry):
    interval = _receiver_interval(stock,geometry)
    if interval is None:
        return root
    lo,hi=stock['bounds']['min'],stock['bounds']['max']
    cutter=graph.prism('post-aperture.span',[[lo[0]-1.,lo[1]-1.],[hi[0]+1.,lo[1]-1.],
        [hi[0]+1.,hi[1]+1.],[lo[0]-1.,hi[1]+1.]],
        [interval[0],0.,0.],[0.,1.,0.],[0.,0.,1.],[1.,0.,0.],interval[1]-interval[0])
    return graph.node('post-aperture.cropped','boolean',[root,cutter],{'operation':'intersect'})


def tube_post_end(process_input, parameters):
    """Trim one real post end, retaining the selected round-ear wall pair."""
    def build(graph):
        parts, geometry = machining._input(process_input, ('stock', 'mate'))
        if set(geometry) != {'end', 'station'}:
            raise ValueError('立柱端加工必须给出实际端点与接收轴站位')
        joint, depth, gap, width = _values(parameters)
        stock, mate = parts['stock'], parts['mate']
        shells = _actual_walls(parts)
        local = deepcopy(process_input)
        for role, part in parts.items():
            local['parts'][role]['section']['wallThickness'] = part['wallThickness']
        local['geometry'] = {'mode': 'insert' if joint == 'insert' else 'butt', **geometry}
        result = machining.tube_end_joint(local, {
            'insertionDepth': depth if joint == 'insert' else 0., 'clearance': gap})
        if not result['applicable']:
            raise ValueError(result['reason'])
        plane = next(item for item in result['checks'] if item['kind'] == 'end-plane')
        if joint == 'tabs':
            _tab_depth(mate, stock, depth, gap)
            # Use the same real receiving-wall datum as a butt end. The ear
            # tip extends from it; the rest of the original stock is set back.
            sign = 1. if geometry['end'] == 'start' else -1.
            tip = plane['origin'][0] - sign * depth
            plane['origin'][0] = tip
            plane['jointMode'] = 'tabs'
            plane['allocation'] = {
                'startStation': tip if sign > 0 else 0.,
                'endStation': stock['length'] if sign > 0 else tip,
                'length': stock['length'] - tip if sign > 0 else tip,
                'rawStockStation': tip}
        tip = plane['allocation']['rawStockStation']
        if not .01 <= tip <= stock['length'] - .01:
            raise ValueError('立柱插入深度超出原材端点余量，不能越过接收管中心')
        if joint == 'insert':
            graph.nodes.extend(result['geometry'])
            return {'operations': deepcopy(result['operations']), 'checks': deepcopy(result['checks'])}
        end = geometry['end']
        if shells['stock'].get('circularShell'):
            root = _round_end_tool(graph,stock,end,tip,depth,width)
            return {'operations':[{'id':'stock:post-end','kind':'neutral-csg','role':'stock',
                                   'operation':'subtract','tool':root}], 'checks':deepcopy(result['checks'])}
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
        if (set(geometry)-{'node','branchEnd','sharedCorner','keepPlanes','clipInterval','endOpening'} or
                not {'node','branchEnd','sharedCorner','keepPlanes'}<=set(geometry) or type(geometry['sharedCorner']) is not bool):
            raise ValueError('立柱母壁加工必须给出实际节点及支管端点')
        joint, depth, gap, width = _values(parameters)
        stock, branch = parts['stock'], parts['branch']
        shells = _actual_walls(parts)
        interval=_receiver_interval(stock,geometry)
        end_opening=geometry.get('endOpening')
        if end_opening is not None:
            if (end_opening not in ('start','end') or geometry['sharedCorner'] or geometry['keepPlanes']
                    or interval is None
                    or abs(interval[0] if end_opening=='start' else interval[1]-stock['length'])>1.e-7):
                raise ValueError('接缝母槽开口须对应实际原材端的加工区间')
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
        if joint == 'insert':
            machining._insertion_fit(stock, branch, depth, gap, span)
        else:
            _tab_depth(stock, branch, depth, gap)
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
            if shells['branch'].get('circularShell'):
                root = _round_aperture_tool(graph,stock,branch,pose,node,outward,normal,outside,depth,width,gap,
                                             end_opening,interval)
                if root is None:
                    return {'operations':[], 'checks':[{'kind':'no-contact-in-stock-interval',
                                                       'interval':list(interval)}]}
                root = _clip_receiver_span(graph,root,stock,geometry)
                return {'operations':[{'id':'stock:post-aperture','kind':'neutral-csg','role':'stock',
                                       'operation':'subtract','tool':root}],
                        'checks':[{'kind':'post-aperture','joint':joint,'face':face,'node':node,
                                   'insertion':depth,'clearance':gap,'retainedSection':'curved-shell-band'}]}
            generated = _tool('paired-side-slots').generate(
                {'pairCount': 2, 'tabWidth': width, 'tabLength': depth, 'sideClearance': gap,
                 'pairRotation': math.degrees(math.atan2(-z[0], z[3 - normal])),
                 'allowEndOpening': not geometry['sharedCorner'],
                 'allowSideOpening': not geometry['sharedCorner']},
                {'targetSection': host_profile, 'feature': {'face': face, 'reference': 'start',
                 'station': socket_center[0], 'offset': socket_center[3 - normal] - face_bounds['center'],
                 'section': {'profile': branch_profile}},
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
        if joint == 'insert' and not geometry['sharedCorner']:
            # A terminal receiver owns the entire opening. At a true miter
            # corner, each of two separately trimmed receivers owns one half.
            if envelope['min'][0] <= .01 or envelope['max'][0] >= stock['length'] - .01:
                raise ValueError('端部立柱的完整母孔须保留承接梁远侧壁料')
        root = _clip_receiver_span(graph,root,stock,geometry)
        return {'operations': [{'id': 'stock:post-aperture', 'kind': 'neutral-csg', 'role': 'stock',
                                'operation': 'subtract', 'tool': root}],
                'checks': [{'kind': 'post-aperture', 'joint': joint, 'face': face, 'node': node,
                            'insertion': depth, 'clearance': gap}]}
    return machining._result('tube-post-aperture', process_input, parameters, build)


FUNCTIONS = {'tube-post-end': tube_post_end, 'tube-post-aperture': tube_post_aperture}
