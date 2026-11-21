"""One continuous drying rail, suspended from two real existing side stocks."""
import math

EPS = 1.e-7


def extend(parameters, members, connections, add, rail, geometry, catalogue,
           *, width, height, front_outer_y, top_rear_outer_y):
    enabled = parameters.get('dryingRodEnabled', 'no')
    if enabled not in ('yes', 'no'):
        raise ValueError('晾衣杆开关必须为是或否')
    if enabled != 'yes':
        return {'enabled': False}
    profile = catalogue.load_profile(parameters, 'drying')
    if profile.kind not in ('rect', 'round', 'rect-bar'):
        raise ValueError('晾衣杆须选择方矩管、圆管或扁钢')
    def number(key):
        value = parameters[key]
        if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or value < 0:
            raise ValueError(key+' 必须为非负有限数值')
        return float(value)
    rear_offset, drop = number('dryingRodRearOffset'), number('dryingRodDrop')
    y, z = top_rear_outer_y-rear_offset, height-drop
    if (width <= 2*profile.width+EPS or z-profile.width/2 <= EPS
            or z+profile.width/2 >= height+EPS
            or y-profile.depth/2 <= front_outer_y+EPS
            or y+profile.depth/2 >= top_rear_outer_y-EPS):
        raise ValueError('晾衣杆须位于成品内部，请调整距后侧、距顶部或用料尺寸')
    original_members = list(members)
    first_connection = len(connections)
    arms, contacts = [], []
    for side, x, label in (('left', profile.width/2, '左'), ('right', width-profile.width/2, '右')):
        start = (x, y, z-profile.width/2)
        candidates = []
        # Intersect the actual section in the actual finite stock. The nearest
        # physical side stock wins, so an arm never runs through existing B rails
        # merely because the upper perimeter was initially preferred.
        for host in original_members:
            if host['key'] != 'frame.return_'+side+'.top' and host['role'] != 'inner.return_'+side:
                continue
            try:
                roots = geometry.ray_intersections(host, start, (0., 0., 1.))
                seed = geometry.add(start, (0., 0., 1.), sum(roots)/2)
                point = geometry.contact_offset(host, seed, (0., 0., -1.), allow_off_axis=True)
            except ValueError:
                continue
            if roots[0] <= EPS < roots[1]:
                raise ValueError(label+'晾衣杆吊臂与现有侧杆重叠，请调整距顶部')
            if point[2] <= start[2]+EPS:
                continue
            candidates.append((point[2], host['key'], host, point))
        if not candidates:
            raise ValueError(label+'晾衣杆吊臂上方没有可实际接触的侧框，请调整距后侧或用料尺寸')
        _, _, host, point = min(candidates, key=lambda value: value[:2])
        if point[2] <= z+profile.width/2+EPS:
            raise ValueError(label+'晾衣杆与现有侧杆过近，无法形成有效吊臂，请调整距顶部')
        key = 'frame.drying.'+side+'.0001'
        arm = add(key, label+'晾衣杆吊臂', 'frame.drying.'+side, profile,
                  start, point, side, 'drying')
        arms.append(arm)
        connection = dict(key='joint.'+key+'.end', hostKey=host['key'], branchKey=key,
                          branchEnd='end', point=point, normal=(0., 0., -1.), jointMode='butt_weld')
        connections.append(connection)
        contacts.append(dict(armKey=key, hostKey=host['key'], point=list(point)))
    rod = rail('frame.drying.rod.0001', '通长晾衣横杆', 'frame.drying.rod', profile,
               arms[0], (arms[0]['start'][0], y, z), arms[1], (arms[1]['start'][0], y, z),
               'drying', profile_role='drying')
    for connection in connections[first_connection:]:
        connection['jointMode'] = 'butt_weld'
    # The production native executor checks the finished, coped material, not
    # an axis/bounding-box approximation. No existing member is moved or cut.
    drying_keys = [rod['key'], *[arm['key'] for arm in arms]]
    pairs = [{'items': [key, other['key']], 'maximumIntersectionVolumeMm3': 1.e-6}
             for key in drying_keys for other in original_members]
    pairs += [{'items': [drying_keys[a], drying_keys[b]], 'maximumIntersectionVolumeMm3': 1.e-6}
              for a in range(3) for b in range(a+1, 3)]
    connections[first_connection]['nonInterferencePairs'] = pairs
    return dict(enabled=True, y=y, z=z, rearOffset=rear_offset, drop=drop,
                rodKey=rod['key'], armKeys=[arm['key'] for arm in arms], topContacts=contacts,
                rearDatum='topRearOuterY', continuousRod=True, physicalStockCount=3)
