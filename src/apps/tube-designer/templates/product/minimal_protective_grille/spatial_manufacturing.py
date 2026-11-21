"""Declare spatial grille joints on whole stocks using shared machining tools."""
from copy import deepcopy
import importlib.util
import math
from pathlib import Path

_spec = importlib.util.spec_from_file_location('minimal_grille_spatial_geometry',
                                               Path(__file__).with_name('spatial_geometry.py'))
g = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(g)
EPS = g.EPS


def _frame(member):
    start, _, axis, _, x, y = g.axes(member)
    return {'origin': start, 'xAxis': axis, 'yAxis': [-v for v in y], 'zAxis': x}


def _envelope(member):
    start, end, _, _, x, y = g.axes(member)
    return {'placement': {'origin': start, 'xAxis': x, 'yAxis': y},
            'contours': [member['profile'].contours()[0]], 'vector': g.sub(end, start)}


def _corner_envelope(neighbor, primary, members, source_connections):
    """Include the neighbour's real cope shoulder inside this shared receiver.

    A nominal visible axis starts on the receiver's near skin. The finished
    cope extends backwards to the SAME far-skin keep plane as its own stock
    fit. Subtract the primary receiver and select the same retained side as the
    actual neighbour; otherwise a disconnected far-side remnant becomes a
    fictitious cutter and removes good branch material.
    """
    matches = [connection for connection in source_connections
               if connection['branchKey'] == neighbor['key']
               and connection['hostKey'] == primary['key']
               and connection.get('kind') != 'cross-through']
    if len(matches) != 1:
        raise ValueError('角节点相邻接收管须与同一主框有唯一的实际端接')
    source = matches[0]
    _, _, _, _, _, span, _ = _contact(source, members)
    start, end, axis, _, _, _ = g.axes(neighbor)
    if source['branchEnd'] == 'start':
        start = g.add(start, axis, -span)
    else:
        end = g.add(end, axis, span)
    result = _envelope(neighbor)
    result['placement']['origin'] = start
    result['vector'] = g.sub(end, start)
    return {'section': result, 'subtractRegions': [_envelope(primary)],
            'keepConnectedTo': _wall_witness(neighbor)}


def _wall_witness(member):
    start, _, axis, length, x, _ = g.axes(member)
    center = g.add(start, axis, length/2)
    if not member['profile'].hollow:
        return center
    outer = g.ray_intersections(member, center, x)[1]
    inner = g.ray_intersections(member, center, x, inner=True)[1]
    if inner <= EPS or outer-inner <= EPS:
        raise ValueError('无法确认接头支管真实壁料内的保留点')
    return g.add(center, x, (inner+outer)/2)


def _local_recipe(recipe, frame):
    result = deepcopy(recipe)
    basis = [frame[k] for k in ('xAxis', 'yAxis', 'zAxis')]
    def direction(v): return [g.dot(v, axis) for axis in basis]
    def point(v): return direction(g.sub(v, frame['origin']))
    if 'placement' in result:
        p = result['placement']
        p['origin'] = point(p['origin'])
        p['xAxis'], p['yAxis'] = direction(p['xAxis']), direction(p['yAxis'])
        result['vector'] = direction(result['vector'])
    else:
        result['center'] = point(result['center'])
        result['xAxis'], result['yAxis'] = direction(result['xAxis']), direction(result['yAxis'])
    return result


def _contact(connection, members):
    host, branch = members[connection['hostKey']], members[connection['branchKey']]
    hs, _, ha, hl, _, _ = g.axes(host)
    bs, be, ba, _, _, _ = g.axes(branch)
    end = connection.get('branchEnd')
    if end not in ('start', 'end'):
        raise ValueError('空间端接必须声明实际的支管端部')
    point, normal = g.vector(connection['point']), g.vector(connection['normal'])
    expected = ba if end == 'start' else [-v for v in ba]
    if math.dist(point, bs if end == 'start' else be) > EPS or math.dist(normal, expected) > EPS:
        raise ValueError('空间接头点和方向必须与支管实际端点一致')
    station = g.dot(g.sub(point, hs), ha)
    roots = g.ray_intersections(host, point, normal)
    if not -EPS <= station <= hl+EPS or abs(roots[1]) > EPS or roots[0] >= -EPS:
        raise ValueError('空间端接点必须位于接收管真实外表面')
    sine = math.sqrt(max(0., 1-g.dot(ha, ba)**2))
    if sine <= EPS:
        raise ValueError('平行管件不能声明为侧面插接')
    # A conservative construction envelope covers every sloping/curved edge.
    # Its excess is removed against the exact receiver, never reported as stock.
    reach = (host['profile'].width+host['profile'].depth
             +branch['profile'].width+branch['profile'].depth)/sine
    return host, branch, point, normal, station, -roots[0], reach


def _near_region(branch, point, normal, span):
    _, _, _, length, x, y = g.axes(branch)
    extent = 4*(length+span+branch['profile'].width+branch['profile'].depth+1)
    return {'center': g.add(point, normal, (extent-span)/2), 'xAxis': x, 'yAxis': y,
            'width': 2*extent, 'height': 2*extent, 'thickness': extent}


def _socket(host, branch, point, normal, length, padding, settings, api):
    hp, bp = host['profile'], branch['profile']
    _, _, _, _, x, y = g.axes(branch)
    full = settings['form'] == 'full_section'
    contours = ([bp.contours(clearance=settings['clearance']/2)[0]] if full else
                api._geometry_queries('directional_tube_geometry').expanded_ring(
                    bp.contours(), settings['clearanceDepth']/2))
    hole = {'contours': contours,
            'placement': {'origin': g.add(point, normal, padding+EPS), 'xAxis': x, 'yAxis': y},
            'vector': [-(length+padding+2*EPS)*v for v in normal]}
    if not full:
        frame = _frame(branch)
        hole['directions'] = {'count': settings['count'], 'angle': settings['angle'],
            'width': settings['width']+settings['clearanceHeight'],
            'zAxis': frame['yAxis'], 'yAxis': frame['zAxis'],
            'reach': 4*(hp.width+hp.depth+bp.width+bp.depth+1)}
    return hole


def _flat_far_tip(host, branch, point, normal, span, api):
    """Prove the whole branch end meets a real flat face normal to its axis."""
    start, _, axis, _, hx, hy = g.axes(host)
    if abs(g.dot(axis, normal)) > EPS:
        return False
    n = [g.dot(normal, hx), g.dot(normal, hy)]
    tangent = [-n[1], n[0]]
    far = g.add(point, normal, -span)
    uv = [g.dot(g.sub(far, start), a) for a in (hx, hy)]
    world_tangent = [tangent[0]*hx[i]+tangent[1]*hy[i] for i in range(3)]
    _, _, _, _, bx, by = g.axes(branch)
    low, high = api._geometry_queries('assembly_stock_allowance')._support_interval({
        'placement': {'origin': far, 'xAxis': bx, 'yAxis': by},
        'contours': branch['profile'].contours()}, world_tangent, start)
    geometry = g._section_geometry()
    outer = geometry.from_profile({'contours': host['profile'].contours()}, tolerance=EPS)['contours'][0]
    for edge in outer['edges']:
        if edge['kind'] != 'line':
            continue
        a, b = edge['start'], edge['end']
        if any(abs(g.dot(g.sub(p, uv), n)) > EPS for p in (a, b)):
            continue
        lo, hi = sorted((g.dot(a, tangent), g.dot(b, tangent)))
        if lo-EPS <= low and high <= hi+EPS:
            return True
    return False


def _cross(connection, members, parameters, api):
    host, branch = members[connection['hostKey']], members[connection['branchKey']]
    point = g.vector(connection['point'])
    for member in (host, branch):
        start, _, axis, length, _, _ = g.axes(member)
        station = g.dot(g.sub(point, start), axis)
        if math.dist(point, g.add(start, axis, station)) > EPS or not EPS < station < length-EPS:
            raise ValueError('通长穿接必须位于两根完整管件的内部轴线交点')
    hs, _, ha, _, _, _ = g.axes(host)
    bs, be, ba, _, x, y = g.axes(branch)
    if abs(g.dot(ha, ba)) > 1-EPS:
        raise ValueError('通长穿接的两根管材不能平行')
    for member in (host, branch):
        # A continuous crossing only needs its real outer envelope. Solid bars
        # are valid here; only hollow sections need shell-metadata validation.
        # Whether the aperture severs the receiving stock remains a native
        # final-solid check, never a blanket hollow/solid restriction.
        if member['profile'].hollow:
            api._profile_shell(member['profile'])
    clearance = api._number(parameters.get('fullHeadTotalClearance', 0.), '通长穿接总间隙')
    # The whole actual crossing section passes through both walls. The native
    # executor checks that the receiver remains one connected solid.
    hole = {'contours': [branch['profile'].contours(clearance=clearance/2)[0]],
            'placement': {'origin': bs, 'xAxis': x, 'yAxis': y}, 'vector': g.sub(be, bs)}
    return host, branch, hole


def _installation(parameters, layout, members, api):
    """Reuse proven mounting validation in each rear upright's translated plane."""
    plan = layout['facts']['installationHoles']
    if not plan['enabled']:
        return {}
    result = {}
    for host_key in {group['memberKey'] for group in plan['groups']}:
        host = members[host_key]
        hs, _, ha, _, hx, hy = g.axes(host)
        if math.dist(ha, [0., 0., 1.]) > EPS or math.dist(hx, [1., 0., 0.]) > EPS or math.dist(hy, [0., 1., 0.]) > EPS:
            raise ValueError('安装边框须保持竖直且截面方向与安装孔方向一致')
        shift = hs[1]
        local_members = deepcopy(members)
        for member in local_members.values():
            member['start'] = g.add(member['start'], [0., -shift, 0.])
            member['end'] = g.add(member['end'], [0., -shift, 0.])
        local_connections = []
        for connection in layout['connections']:
            if connection['hostKey'] != host_key:
                continue
            # The old wall-access test uses a horizontal projection. It must
            # also protect oblique/end footprints and full crossing pieces.
            branch = members[connection['branchKey']]
            point = connection['point']
            radius = float(parameters['installHoleLargeDiameter'])/2
            _, _, axis, _, x, y = g.axes(branch)
            z_radius = (abs(x[2])*branch['profile'].width+abs(y[2])*branch['profile'].depth)/2
            for group in plan['groups']:
                if group['memberKey'] != host_key:
                    continue
                if abs(group['point'][2]-point[2]) < z_radius+radius+float(parameters['installHoleMinimumWeb'])-EPS:
                    raise ValueError('安装孔与空间接头或通长横杆重叠，请调整组数或上下端距')
            if connection.get('kind') != 'cross-through' and abs(axis[0]) > 1-EPS:
                translated = deepcopy(connection)
                translated['point'] = g.add(point, [0., -shift, 0.])
                local_connections.append(translated)
        local_plan = deepcopy(plan)
        local_plan['groups'] = [group for group in local_plan['groups'] if group['memberKey'] == host_key]
        for group in local_plan['groups']:
            group['point'][1] -= shift
        local_layout = {'connections': local_connections, 'facts': {'installationHoles': local_plan}}
        holes = api._installation_holes(parameters, local_layout, local_members)[host_key]
        for hole in holes:
            hole['placement']['origin'][1] += shift
        result[host_key] = holes
    return result


def manufacturing(parameters, layout, api):
    mode = parameters.get('jointMode', 'butt_weld')
    return_mode = parameters.get('returnJointMode', 'butt_weld')
    if any(value not in ('butt_weld', 'male_female_half_hole', 'male_female_through_hole')
           for value in (mode, return_mode)):
        raise ValueError('内杆连接方式无效')
    members = {member['key']: member for member in layout['members']}
    if not members or len(members) != len(layout['members']):
        raise ValueError('空间构件标识必须唯一且不为空')
    for member in members.values():
        g.axes(member)
    joint_settings = {value: api._joint_settings(parameters, value == 'male_female_through_hole')
                      for value in {mode, return_mode} if value != 'butt_weld'}
    holes = {key: [] for key in members}
    endpoints = {key: {} for key in members}
    connections, seen = [], set()
    for connection in layout['connections']:
        if connection['key'] in seen:
            raise ValueError('空间接头标识重复')
        seen.add(connection['key'])
        if connection.get('kind') == 'cross-through':
            host, branch, hole = _cross(connection, members, parameters, api)
            holes[host['key']].append(hole)
            connections.append({'key': connection['key'], 'kind': 'assembly',
                'items': [host['key'], branch['key']], 'properties': {
                    'recipe': 'cross-through', 'contactPoint': list(connection['point']),
                    'continuousMembers': True, 'throughHole': True,
                    'preservedParticipants': [branch['key']],
                    'manufacturingProcesses': {host['key']: [host['key']+'.apertures'],
                                              branch['key']: [branch['key']+'.prepare']}}})
            continue
        host, branch, point, normal, station, span, reach = _contact(connection, members)
        end, key = connection['branchEnd'], branch['key']
        # Perimeter skeleton stocks stay welded. Only actual B infill returns
        # use the return-joint setting, with each endpoint's real receiver.
        connection_mode = ('butt_weld' if connection.get('jointMode') == 'butt_weld' or branch.get('profileRole') in
                           ('projectionHorizontal', 'projectionVertical', 'drying') else
                           return_mode if branch['role'].startswith(
                               ('inner.return_', 'inner.top_return', 'inner.bottom_return')) else mode)
        through = connection_mode == 'male_female_through_hole'
        settings = joint_settings.get(connection_mode)
        if end in endpoints[key]:
            raise ValueError('同一管材端部不能重复分配空间连接')
        inserted = settings is not None and host['role'].startswith('frame.') and branch['role'].startswith('inner.')
        near = _near_region(branch, point, normal, span)
        details = {'host': host, 'point': point, 'normal': normal, 'span': span, 'reach': reach,
                   'near': near, 'inserted': inserted}
        props = {'recipe': connection_mode if inserted else 'butt_weld', 'branchEnd': end,
                 'contactPoint': point, 'faceNormal': normal, 'spatialJoint': True}
        if connection.get('nonInterferencePairs'):
            props['nonInterferencePairs'] = deepcopy(connection['nonInterferencePairs'])
        extra_hosts = connection.get('fitHostKeys', [])
        if (not isinstance(extra_hosts, list) or len(set(extra_hosts)) != len(extra_hosts)
                or any(name not in members or name in (host['key'], key) for name in extra_hosts)):
            raise ValueError('角节点相邻接收管声明无效')
        details['fitReceivers'] = [_envelope(host)]+[
            _corner_envelope(members[name], host, members, layout['connections']) for name in extra_hosts]
        if extra_hosts:
            props['nonInterferencePairs'] = [{'items': [key, name], 'maximumIntersectionVolumeMm3': 1.e-5}
                                            for name in extra_hosts]
        if inserted:
            hp, bp = host['profile'], branch['profile']
            if not hp.hollow or not bp.hollow:
                raise ValueError('公母插接须选择中空管材')
            api._profile_shell(hp)
            api._profile_shell(bp)
            flat_tip = _flat_far_tip(host, branch, point, normal, span, api) if through else False
            if through and not flat_tip and settings['form'] == 'tongue' and settings['corner'] != 'square':
                raise ValueError('贯穿公头与斜面或曲面齐平时暂不支持圆角或倒角，请选择直角端头')
            length = (span if flat_tip else reach) if through else settings['length']
            if not through:
                inner = g.ray_intersections(host, point, normal, inner=True)
                if not -inner[1]+EPS < length < -inner[0]-EPS:
                    raise ValueError('单边公头须穿过实际接收侧壁，且不能进入对侧管壁')
            if settings['form'] == 'tongue' and not 2*bp.wall+EPS < settings['width'] <= bp.width-2*settings['web']+EPS:
                raise ValueError('公头宽度须保留有效壁料和两侧肩部')
            joint = {**settings, 'length': length,
                     'width': bp.width if settings['form'] == 'full_section' else settings['width']}
            details['settings'] = joint
            holes[host['key']].append(_socket(host, branch, point, normal, length, reach, joint, api))
            props.update(halfHole=not through, throughHole=through,
                         flushWithOppositeOuterFace=through, maleEndForm=joint['form'],
                         insertionDepth=span if through else length,
                         manufacturingProcesses={host['key']: [host['key']+'.apertures'],
                                                 key: [key+'.male-head.'+end]})
        endpoints[key][end] = details
        connections.append({'key': connection['key'], 'kind': 'assembly' if inserted else 'weld',
                            'items': [host['key'], key], 'properties': props})
    # Preserve explicit receiver adjacency so the native host measures exact
    # finished material, including separate branches meeting inside one tube.
    # The declaration makes no claim based on overlapping bounding boxes.
    by_receiver = {}
    for index, connection in enumerate(layout['connections']):
        by_receiver.setdefault(connection['hostKey'], []).append((connection['branchKey'], index))
    for entries in by_receiver.values():
        branches = sorted({key for key, _ in entries})
        pairs = [{'items': [first, second], 'maximumIntersectionVolumeMm3': 1.e-5}
                 for i, first in enumerate(branches) for second in branches[i+1:]]
        if pairs:
            connections[entries[0][1]]['properties'].setdefault('nonInterferencePairs', []).extend(pairs)
    for key, cuts in _installation(parameters, layout, members, api).items():
        holes[key].extend(cuts)
    processes = []
    for index, (key, member) in enumerate(members.items(), 1):
        start, end, axis, length, _, _ = g.axes(member)
        first = -endpoints[key].get('start', {}).get('reach', 0.)
        last = length+endpoints[key].get('end', {}).get('reach', 0.)
        previous = key+'.prepare'
        processes.append(api._call(previous, key, 'profile-stock-preparation', {
            'axialInterval': {'startStation': first, 'endStation': last},
            'direction': 'forward', 'status': 'prepared', 'ready': True}, parameters={
                'requestedPartNumber': member.get('partNumber', str(parameters.get('productCode', 'JJ'))+f'-{index:03d}'),
                'reportingLengthPrecision': 3}))
        for which, contact in endpoints[key].items():
            process_key = key+('.male-head.' if contact['inserted'] else '.fit.')+which
            if contact['inserted']:
                frame, profile = _frame(member), member['profile']
                settings = contact['settings']
                mate = {'station': 0. if which == 'start' else length,
                        'direction': 1. if which == 'start' else -1.,
                        'envelope': _local_recipe(_envelope(contact['host']), frame),
                        'nearRegion': _local_recipe(contact['near'], frame)}
                processes.append(api._call(process_key, key, 'structural-male-head', {
                    'visibleLeft': 0., 'visibleRight': length, 'centerZ': 0.,
                    'profileWidth': profile.width, 'profileDepth': profile.depth, 'wall': profile.wall,
                    'mateEnds': [mate]}, parameters={k: settings[k] for k in ('length', 'width', 'corner', 'size', 'form')}
                    | ({k: settings[k] for k in ('count', 'angle')} if settings['form'] == 'tongue' else {}),
                    previous=previous, frame=frame, facts={'length': last-first,
                        'start': [first, 0., 0.], 'end': [last, 0., 0.], 'section': profile.properties()}))
            else:
                processes.append(api._call(process_key, key, 'structural-stock-fit', {
                    'keepRegions': [contact['near']], 'receivers': contact['fitReceivers'],
                    'keepConnectedTo': _wall_witness(member)}, previous=previous,
                    facts={'length': length, 'start': start, 'end': end, 'section': member['profile'].properties()}))
            previous = process_key
        if holes[key]:
            processes.append(api._call(key+'.apertures', key, 'structural-apertures',
                {'holes': holes[key]}, previous=previous,
                facts={'length': length, 'start': start, 'end': end, 'section': member['profile'].properties()}))
    result = {'schema': 'icax.manufacturing-model', 'schemaVersion': 4,
              'connections': connections, 'processes': processes}
    api.validate_manufacturing_model(result)
    return result
