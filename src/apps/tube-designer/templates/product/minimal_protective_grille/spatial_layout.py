"""Five-face minimal grille, expressed as physical straight stocks.

The front is the existing grille with a closed perimeter. The rear perimeter
is the installation frame. Single supports receive continuous rails; paired
supports terminate separate rail spans, with optional independent short links.
"""
from copy import deepcopy
import math

EPS = 1.e-7


def _number(p, key, minimum, maximum=10000.):
    value = p[key]
    if (isinstance(value, bool) or not isinstance(value, (float, int))
            or not math.isfinite(value) or not minimum <= value <= maximum):
        raise ValueError(f'{key} 必须为 {minimum:g}～{maximum:g} 的有限数值')
    return float(value)


def section_frame(start, end):
    axis = tuple((b-a)/math.dist(start, end) for a, b in zip(start, end))
    if abs(axis[0]) > 1-EPS:
        x, y = (0., 0., 1.), (0., -1., 0.)
    elif abs(axis[2]) > 1-EPS:
        x, y = (1., 0., 0.), (0., 1., 0.)
    else:
        # The width lies in the vertical return plane; depth is across X.
        x, y = (0., -axis[2], axis[1]), (1., 0., 0.)
    return dict(originAtStart=list(start), xAxis=list(x), yAxis=list(y),
                zAxis=list(axis), centerlineUV=[0., 0.])


def support_positions(width, frame_width, inner_width, maximum, openings, support_width=None, minimum_groups=0):
    """Equal bays first, with posts moved clear of local handle openings.

    The reported limit is a maximum centre distance, including the side posts.
    Handle clearances never get a post silently drawn through them.
    """
    left, right = frame_width/2, width-frame_width/2
    support_width = frame_width if support_width is None else support_width
    edge_separation = (frame_width+support_width)/2
    bays = max(1+minimum_groups, math.ceil((right-left)/maximum-EPS))
    forbidden = sorted((o['left']-support_width/2-inner_width-EPS,
                        o['right']+support_width/2+inner_width+EPS) for o in openings)
    def legal(x):
        return (left+edge_separation+EPS < x < right-edge_separation-EPS
                and all(not a < x < b for a, b in forbidden))
    def move(x, a, b):
        candidates = [x, *[v for pair in forbidden for v in pair]]
        before = edge_separation if abs(a-left) <= EPS else support_width
        after = edge_separation if abs(b-right) <= EPS else support_width
        candidates = [v for v in candidates if a+before+EPS < v < b-after-EPS and legal(v)]
        if not candidates:
            raise ValueError('当前把手净口与竖杆最大中心距冲突，请增大中心距或调整把手位置')
        return min(candidates, key=lambda v: (abs(v-x), v))
    posts = sorted(set(move(left+(right-left)*i/bays, left, right) for i in range(1, bays)))
    for _ in range(500):
        anchors = [left, *posts, right]
        gaps = [(b-a, a, b) for a, b in zip(anchors, anchors[1:])]
        gap, a, b = max(gaps)
        if gap <= maximum+EPS:
            if any(b-a <= (edge_separation if i in (0,len(anchors)-2) else support_width)+EPS
                   for i,(a,b) in enumerate(zip(anchors, anchors[1:]))):
                raise ValueError('竖杆与把手附近构件过近，请调整间距或把手位置')
            return posts
        value = move((a+b)/2, a, b)
        if any(abs(value-old) <= EPS for old in posts):
            raise ValueError('当前把手位置无法满足竖杆最大中心距')
        posts = sorted([*posts, value])
    raise ValueError('竖杆数量过多，请增大间距')


def _front_members(front):
    members = deepcopy(front['members'])
    openings = front['facts']['clearOpenings']
    center = front['facts']['centralMullion']
    if center is not None:
        # Keep the central handle divider local to the openings. A full-height
        # equal-section B post would either split the rails or be severed by
        # their through apertures. Neither is an acceptable hidden substitution.
        central = next(m for m in members if m['role'] == 'inner.center')
        members = [m for m in members if m['role'] != 'inner.center']
        if front['facts']['handleClosure'] == 'closed':
            item = deepcopy(central)
            item['key'] = 'inner.center.0001'
            item['start'] = (center, 0., openings[0]['bottom'])
            item['end'] = (center, 0., openings[0]['top'])
            item['name'] = '双把手中间竖短杆'
            members.append(item)
        groups = {}
        for m in members:
            if (m['role'].startswith('inner.') and m['role'] not in ('inner.handle_jamb', 'inner.center', 'inner.middle')
                    and abs(m['start'][2]-m['end'][2]) < EPS):
                groups.setdefault((m['role'], m['start'][2]), []).append(m)
        for rows in groups.values():
            if len(rows) == 2:
                a, b = sorted(rows, key=lambda m: m['start'][0])
                a['end'] = b['end']
                members.remove(b)
    for m in members:
        m['face'] = 'front'
        if m['role'].startswith('frame.'):
            m['role'] = m['role'].replace('frame.', 'frame.front_', 1)
        m['sectionFrame'] = section_frame(m['start'], m['end'])
    return members


def return_positions(width, frame_width, bar_depth, maximum_gap, supports, support_width,
                     *, boundary_depth=None, rear_frame_width=0.):
    """Distribute returns without running through a front support's shoulders.

    The two edge returns are the boundary stocks. Keep uniform distribution
    when clear; otherwise place the minimum sequential rods within the allowed
    lanes at no more than the requested clear gap. No return is simply omitted.
    """
    # Virtual edge centres let the same exact clear-gap equation account for
    # a boundary stock whose transverse size differs from an inner B stock.
    left, right = ((frame_width/2, width-frame_width/2) if boundary_depth is None else
                   (boundary_depth-bar_depth/2, width-boundary_depth+bar_depth/2))
    available = right-left-bar_depth
    if available <= EPS:
        raise ValueError('左右边界回杆之间须保留有效净空，请增大成品宽度或减小 B 管截面深度')
    count = max(0, math.ceil((available-maximum_gap)/(bar_depth+maximum_gap)-EPS))
    gap = (available-count*bar_depth)/(count+1)
    uniform = [left+bar_depth+gap+i*(bar_depth+gap) for i in range(count)]
    reserve = (support_width+bar_depth)/2+1.e-5
    blocked = [(x-reserve,x+reserve) for x in supports]
    inset = max(frame_width, rear_frame_width, boundary_depth or 0.)
    minimum = inset+bar_depth/2+1.e-5
    maximum = width-inset-bar_depth/2-1.e-5
    def legal(x):
        return (minimum-EPS <= x <= maximum+EPS
                and all(not a < x < b for a,b in blocked))
    if all(legal(x) for x in uniform):
        return uniform, [gap]*(count+1)
    positions, current = [], left
    step = bar_depth+maximum_gap
    for _ in range(1500):
        if right-current <= step+EPS:
            centers = [left,*positions,right]
            return positions, [b-a-bar_depth for a,b in zip(centers,centers[1:])]
        target = current+step
        candidates = [target,*[a for a,b in blocked],maximum]
        choices = [x for x in candidates if current+bar_depth+EPS < x <= target+EPS and legal(x)]
        if not choices:
            raise ValueError('C 管支撑与顶底杆净距冲突，请增大净距或调整支撑位置/管型')
        current = max(choices)
        positions.append(current)
    raise ValueError('顶底回杆过多，请增大净距')


def _front_contacts(members):
    contacts = []
    for branch in members:
        vertical = abs(branch['start'][0]-branch['end'][0]) <= EPS
        for end in ('start', 'end'):
            point = branch[end]
            candidates = []
            for host in members:
                if host is branch or (abs(host['start'][0]-host['end'][0]) <= EPS) == vertical:
                    continue
                half, a, b = host['profile'].width/2, host['start'], host['end']
                if vertical:
                    touching = a[0]-EPS <= point[0] <= b[0]+EPS and abs(abs(point[2]-a[2])-half) <= EPS
                    normal = (0., 0., 1. if point[2] > a[2] else -1.)
                else:
                    touching = a[2]-EPS <= point[2] <= b[2]+EPS and abs(abs(point[0]-a[0])-half) <= EPS
                    normal = (1. if point[0] > a[0] else -1., 0., 0.)
                if touching:
                    candidates.append((host, normal))
            if len(candidates) > 1:
                raise ValueError('正面构件端部接收管不唯一：'+branch['name'])
            if candidates:
                host, normal = candidates[0]
                contacts.append(dict(key=f'joint.{branch["key"]}.{end}', hostKey=host['key'],
                                     branchKey=branch['key'], branchEnd=end, point=point, normal=normal))
    return contacts


def extend(p, front, load, root):
    geometry = load(root / 'spatial_geometry.py')
    members = _front_members(front)
    balcony = p.get('layoutStyle') == 'balcony_closure'
    if balcony:
        # This open installation side has no full-height rear uprights.
        # Existing side/front mounting holes belong to real front uprights.
        for member in members:
            if member['role'] in ('frame.front_left', 'frame.front_right'):
                member['role'] = member['role'].replace('frame.front_', 'frame.', 1)
    connections = _front_contacts(members)
    by_key = {m['key']: m for m in members}
    frame = next(m['profile'] for m in members if m['role'].startswith('frame.'))
    catalogue = load(root.parents[1] / '_shared' / 'tube_profile_catalog.py')
    inner = catalogue.load_profile(p, 'inner')
    support = catalogue.load_profile(p, 'support')
    horizontal = catalogue.load_profile(p, 'projectionHorizontal')
    vertical = None if balcony else catalogue.load_profile(p, 'projectionVertical')
    if support.kind not in ('rect-bar', 'rect', 'round'):
        raise ValueError('支撑竖杆须选择扁钢、方矩管或圆管')
    if horizontal.kind not in ('rect-bar', 'rect', 'round') or (
            vertical is not None and vertical.kind not in ('rect-bar', 'rect', 'round')):
        raise ValueError('飘面骨架须选择方矩管、圆管或扁钢')
    width, height = float(p['width']), float(p['height'])
    depth = _number(p, 'balconyTopDepth' if balcony else 'projectionDepth', EPS)
    bottom_depth = _number(p, 'balconyBottomDepth', EPS) if balcony else depth
    drop = _number(p, 'balconyTopDrop' if balcony else 'topDrop', 0., 1000.)
    maximum = _number(p, 'supportMaxSpacing', 1.)
    return_gap = _number(p, 'returnMaximumClearGap', 1., 2000.)
    fw = frame.width
    front_outer_y = -frame.depth/2
    top_rear_outer_y, bottom_rear_outer_y = front_outer_y+depth, front_outer_y+bottom_depth
    rear_y, bottom_y = top_rear_outer_y-horizontal.depth/2, bottom_rear_outer_y-horizontal.depth/2
    upright_y = None if balcony else top_rear_outer_y-vertical.depth/2
    if min(rear_y-horizontal.depth/2, bottom_y-horizontal.depth/2,
           upright_y-vertical.depth/2 if vertical is not None else rear_y) <= frame.depth/2+EPS:
        raise ValueError('飘出外包进深不足以容纳前后骨架，请增大进深或减小截面')
    left_x, right_x = fw/2, width-fw/2
    front_top, front_bottom = height-fw/2, fw/2

    def add(key, name, role, profile, start, end, face, profile_role=None):
        if key in by_key or math.dist(start, end) <= EPS:
            raise ValueError('五面构件重复或没有有效长度：'+name)
        m = dict(key=key, name=name, role=role, profile=profile,
                 start=tuple(start), end=tuple(end), face=face,
                 sectionFrame=section_frame(start, end))
        if profile_role is not None:
            m['profileRole'] = profile_role
        by_key[key] = m
        members.append(m)
        return m

    def rail(key, name, role, profile, host_a, center_a, host_b, center_b, face, fit_hosts=None,
             profile_role=None):
        axis = geometry.unit(geometry.sub(center_b, center_a))
        start = geometry.contact_offset(host_a, center_a, axis, allow_off_axis=True)
        end = geometry.contact_offset(host_b, center_b, [-v for v in axis], allow_off_axis=True)
        if geometry.dot(geometry.sub(end, start), axis) <= EPS:
            raise ValueError('飘面杆件在两端接收管之间没有有效长度：'+name)
        m = add(key, name, role, profile, start, end, face, profile_role)
        for index, (tag, host, point, normal) in enumerate((('start',host_a,start,axis), ('end',host_b,end,[-v for v in axis]))):
            contact = dict(key=f'joint.{key}.{tag}', hostKey=host['key'], branchKey=key,
                           branchEnd=tag, point=point, normal=normal)
            if fit_hosts is not None and fit_hosts[index] is not None:
                contact['fitHostKeys'] = [fit_hosts[index]['key']]
            connections.append(contact)
        return m

    rear_sides = {}
    bottom_center = horizontal.width/2
    roof_front_center, roof_rear_center = height-horizontal.width/2, height+drop-horizontal.width/2
    boundary_xs = [horizontal.depth/2, width-horizontal.depth/2]
    if width-2*horizontal.depth <= EPS:
        raise ValueError('左右边界回杆之间须保留有效净空，请增大成品宽度或减小飘面横向骨架截面')
    if balcony:
        rear_bottom = add('frame.rear_bottom.0001','后侧下横框','frame.rear_bottom',horizontal,
                          (0.,bottom_y,bottom_center), (width,bottom_y,bottom_center), 'mounting', 'projectionHorizontal')
        rear_top = add('frame.rear_top.0001','后侧上横框','frame.rear_top',horizontal,
                       (0.,rear_y,roof_rear_center), (width,rear_y,roof_rear_center), 'mounting', 'projectionHorizontal')
    else:
        rear_left, rear_right = vertical.width/2, width-vertical.width/2
        for side, x, name in (('left',rear_left,'左安装竖框'), ('right',rear_right,'右安装竖框')):
            rear_sides[side] = add('frame.rear_'+side+'.0001', name, 'frame.'+side, vertical,
                                  (x,upright_y,0.), (x,upright_y,height+drop), 'mounting', 'projectionVertical')
        rear_bottom = rail('frame.rear_bottom.0001','安装侧下框','frame.rear_bottom',horizontal,
                           rear_sides['left'], (rear_left,bottom_y,bottom_center),
                           rear_sides['right'], (rear_right,bottom_y,bottom_center), 'mounting',
                           profile_role='projectionHorizontal')
        rear_top = rail('frame.rear_top.0001','安装侧上框','frame.rear_top',horizontal,
                        rear_sides['left'], (rear_left,rear_y,roof_rear_center),
                        rear_sides['right'], (rear_right,rear_y,roof_rear_center), 'mounting',
                        profile_role='projectionHorizontal')
    front_top_m, front_bottom_m = by_key['frame.top.0001'], by_key['frame.bottom.0001']

    support_style = p.get('supportStyle', 'single')
    if support_style not in ('single', 'double'):
        raise ValueError('支撑形式必须为单竖杆贯穿或双竖杆分段')
    paired = support_style == 'double'
    segmented = paired or support.kind == 'rect-bar'
    pair_gap = _number(p, 'supportPairClearGap', 1., 1000.) if paired else None
    group_width = 2*support.width+pair_gap if paired else support.width
    # A split rail beside a handle must remain a useful stock, not a numerical
    # sliver between a paired support and the handle jamb. Reserve one B width.
    group_centers = support_positions(width, fw, inner.width*(2 if segmented else 1), maximum,
                                     front['facts']['clearOpenings'], group_width, 1 if paired else 0)
    support_x = ([x+sign*(support.width+pair_gap)/2 for x in group_centers for sign in (-1,1)]
                 if paired else group_centers)
    front_rows = [m for m in members if m.get('face') == 'front' and m['role'].startswith('inner.')
                  and abs(m['start'][2]-m['end'][2]) < EPS]
    posts = []
    for i, x in enumerate(support_x, 1):
        post = rail(f'frame.support.{i:04d}', '正面支撑竖杆', 'frame.support', support,
                    front_bottom_m, (x,0.,front_bottom), front_top_m, (x,0.,front_top), 'front')
        posts.append(post)
        if segmented:
            continue
        for row in front_rows:
            if row['start'][0]+EPS < x < row['end'][0]-EPS:
                point = (x,0.,row['start'][2])
                if not post['start'][2]+EPS < point[2] < post['end'][2]-EPS:
                    raise ValueError('横杆穿接过于接近支撑竖杆端部，请调整排杆')
                connections.append(dict(key=f'cross.{post["key"]}.{row["key"]}', kind='cross-through',
                                        hostKey=post['key'], branchKey=row['key'], point=point))

    split_rows = False
    if segmented:
        # Flat C stock is never drilled for a continuous B crossing. One solid
        # post, or a paired support group, bounds independent welded B spans.
        # The bounding A rails and every C stock stay physically continuous.
        cutting_groups = list(zip(posts[::2], posts[1::2])) if paired else [(post, post) for post in posts]
        for row in front_rows:
            spans = [(row['start'], row['end'])]
            for left_post, right_post in cutting_groups:
                lx, rx = left_post['start'][0], right_post['start'][0]
                z = row['start'][2]
                left_end = tuple(geometry.contact_offset(left_post, (lx,0.,z), (-1.,0.,0.)))
                right_start = tuple(geometry.contact_offset(right_post, (rx,0.,z), (1.,0.,0.)))
                remaining = []
                for start, end in spans:
                    if end[0] <= left_end[0]+EPS or start[0] >= right_start[0]-EPS:
                        remaining.append((start,end))
                    elif start[0] < left_end[0]-EPS and end[0] > right_start[0]+EPS:
                        remaining.extend(((start,left_end),(right_start,end)))
                    else:
                        raise ValueError('分段支撑与把手端部冲突，请调整支撑间距或把手位置')
                spans = remaining
            if len(spans) == 1:
                continue
            split_rows = True
            members.remove(row)
            del by_key[row['key']]
            for index,(start,end) in enumerate(spans,1):
                segment = add(f'{row["key"]}.span.{index:04d}', row['name']+f'第{index}跨',
                              row['role'], row['profile'], start, end, 'front')
                segment['sourceRowKey'] = row['key']
        # Rebuild real front endpoint joints after replacing source rows.
        # Rear/return contacts created above are independent and retained.
        front_keys = {m['key'] for m in members if m.get('face') == 'front'}
        old_row_keys = {m['key'] for m in front_rows}
        connections = [c for c in connections if c['branchKey'] not in front_keys | old_row_keys]
        connections.extend(_front_contacts([m for m in members if m.get('face') == 'front']))
        for connection in connections:
            host = by_key[connection['hostKey']]
            if host['role'] == 'frame.support' and host['profile'].kind == 'rect-bar':
                connection['jointMode'] = 'butt_weld'

    link_centers = []
    link_count = 0
    if paired:
        link_values = {'supportLinkCount': p.get('supportLinkCount', 0),
                       'supportLinkBottomOffset': p.get('supportLinkBottomOffset', 100),
                       'supportLinkTopOffset': p.get('supportLinkTopOffset', 100)}
        count = _number(link_values, 'supportLinkCount', 0., 100.)
        if not count.is_integer():
            raise ValueError('双竖杆间短横杆数量必须为 0～100 的整数')
        link_count = int(count)
        if link_count:
            first = _number(link_values, 'supportLinkBottomOffset', 0., height)
            last = (height-_number(link_values, 'supportLinkTopOffset', 0., height)
                    if link_count > 1 else first)
            if (first-inner.width/2 <= fw+EPS
                    or last+inner.width/2 >= height-fw-EPS):
                raise ValueError('双竖杆间短横杆须位于前上下 A 材内侧并保留有效净空，请调整上下位置')
            if link_count > 1 and (last-first)/(link_count-1) <= inner.width+EPS:
                raise ValueError('双竖杆间短横杆相互重叠或没有有效净空，请减少数量或调整上下位置')
            link_centers = ([first] if link_count == 1 else
                            [first+(last-first)*i/(link_count-1) for i in range(link_count)])
            for group_index, (left_post, right_post) in enumerate(zip(posts[::2], posts[1::2]), 1):
                lx, rx = left_post['start'][0], right_post['start'][0]
                for index, z in enumerate(link_centers, 1):
                    rail(f'inner.support_link.{group_index:04d}.{index:04d}',
                         f'双竖杆间短横杆第{group_index}组第{index}根', 'inner.support_link', inner,
                         left_post, (lx,0.,z), right_post, (rx,0.,z), 'front')

    # A perimeter return is a horizontal skeleton stock, not an inner B rod.
    # Align its actual X/Z outside extent to the product, then intersect the
    # differently sized receivers on that same (possibly off-axis) line.
    row_centers = sorted(set(z for values in front['facts']['rowCenters'].values() for z in values))
    for side, front_x, boundary_x, name in (
            ('left',left_x,boundary_xs[0],'左'), ('right',right_x,boundary_xs[1],'右')):
        front_hosts = [m for m in members if m['role'] == ('frame.' if balcony else 'frame.front_')+side]
        def front_host(z):
            hosts = [m for m in front_hosts if m['start'][2]-EPS <= z <= m['end'][2]+EPS]
            if len(hosts) != 1:
                raise ValueError('侧面骨架的接收竖框缺失或不唯一，请调整把手位置或骨架截面')
            return hosts[0]
        for edge, z, rear_z, end_y, neighbour in (
                ('bottom',bottom_center,bottom_center,bottom_y,front_bottom_m),
                ('top',roof_front_center,roof_rear_center,rear_y,front_top_m)):
            rear_rail = rear_top if edge == 'top' else rear_bottom
            rear_host = rear_rail if balcony else rear_sides[side]
            rail(f'frame.return_{side}.{edge}', name+('顶' if edge == 'top' else '底')+'飘面边框',
                 'frame.return_'+side, horizontal,
                 front_host(z), (boundary_x,0.,z), rear_host, (boundary_x,end_y,rear_z), side,
                 (neighbour, None if balcony else rear_rail), profile_role='projectionHorizontal')
        # The existing side infill is still B material. No rear upright or
        # intermediate side stock is invented for the open balcony variant.
        if not balcony:
            # The two uprights can have different widths. Keep the side rod
            # level on a shared interior line instead of forcing the front
            # upright's centre through a narrower rear stock's outer edge.
            side_inset = max(inner.depth/2, min(frame.width, vertical.width)/2)
            side_x = side_inset if side == 'left' else width-side_inset
            for i, z in enumerate(row_centers, 1):
                hosts = [m for m in front_hosts if m['start'][2]-EPS <= z <= m['end'][2]+EPS]
                if not hosts:
                    continue
                if len(hosts) != 1:
                    raise ValueError('侧面回杆的接收竖框不唯一')
                rail(f'inner.return_{side}.{i:04d}', name+'飘面内横杆', 'inner.return_'+side, inner,
                     hosts[0], (side_x,0.,z), rear_sides[side], (side_x,upright_y,z), side)

    # Edge return rods already bound each roof/bottom face. Fill the actual
    # clear width between them, measuring the B section in the X direction.
    rafter_x, return_gaps = return_positions(width, fw, inner.depth, return_gap, support_x, support.width,
                                            boundary_depth=horizontal.depth,
                                            rear_frame_width=0. if balcony else vertical.width)
    return_inset = max(horizontal.width, inner.width)/2
    return_bottom = return_inset
    return_top, return_rear_top = height-return_inset, height+drop-return_inset
    for face, front_host, rear_host, z, rear_z, end_y in (
            ('top', front_top_m, rear_top, return_top, return_rear_top, rear_y),
            ('bottom', front_bottom_m, rear_bottom, return_bottom, return_bottom, bottom_y)):
        for i, x in enumerate(rafter_x, 1):
            if not front_host['start'][0]+EPS < x < front_host['end'][0]-EPS:
                raise ValueError('顶底排杆过于接近边角，请增大顶底杆净距或成品宽度')
            rail(f'inner.{face}_return.{i:04d}', ('顶' if face == 'top' else '底')+'飘面杆',
                 'inner.'+face+'_return', inner, front_host, (x,0.,z), rear_host, (x,end_y,rear_z), face)

    drying = load(root / 'drying_rod.py').extend(p, members, connections, add, rail, geometry, catalogue,
        width=width, height=height, front_outer_y=front_outer_y, top_rear_outer_y=top_rear_outer_y)
    if len(members) > 1500:
        raise ValueError('五面排杆数量过多，请增大间距')
    installation = load(root / 'installation_holes.py').plan(p, members)
    facts = deepcopy(front['facts'])
    facts.update(layoutStyle='balcony_closure' if balcony else 'five_face', frameType='closed_frame',
                 continuousHorizontalRows=not split_rows, centralMullion=None,
                 mountingPlaneY=rear_y, bottomMountingPlaneY=bottom_y,
                 frontOuterY=front_outer_y, topRearOuterY=top_rear_outer_y,
                 bottomRearOuterY=bottom_rear_outer_y,
                 rearTopRailY=rear_y, rearBottomRailY=bottom_y, rearUprightY=upright_y,
                 projectionBoundaryXs=boundary_xs, bottomCenterZ=bottom_center,
                 roofFrontCenterZ=roof_front_center, roofRearCenterZ=roof_rear_center,
                 returnBottomZ=return_bottom, returnFrontTopZ=return_top, returnRearTopZ=return_rear_top,
                 projectionDepth=depth, bottomProjectionDepth=bottom_depth, topDrop=drop,
                 supportPositions=support_x, supportGroupCenters=group_centers,
                 supportStyle=support_style, supportPairClearGap=pair_gap,
                 supportConnectionKind='segmented-weld' if segmented else 'through',
                 supportLinkCenters=link_centers, supportLinkCount=link_count,
                 supportMaximumCenterSpacing=maximum,
                 returnPositions=rafter_x, returnActualClearGap=max(return_gaps), returnClearGaps=return_gaps,
                 measurementDatum='width/frontHeight:front-frame-outside; projectionDepth:front/rear-frame-outside; supportSpacing:center',
                 memberCount=len(members), contactCount=len(connections), installationHoles=installation,
                 dryingRod=drying)
    return dict(members=members, connections=connections, facts=facts)
