"""A grille with local handle clearances, independent of opening window sashes.

The layout describes physical stocks. A change in a semantic region never
splits a horizontal stock; only a real post or an open handle slot does.
"""
from __future__ import annotations

from copy import deepcopy
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import sys

from icax_template_sdk import NeutralModel, to_display_model

TEMPLATE_ID = 'minimal-protective-grille'
TEMPLATE_VERSION = '2.5.0'
ROOT = Path(__file__).resolve().parent
SHARED = ROOT.parents[1] / '_shared'
EPS = 1.e-7


def _load(path):
    name = 'icax_grille_' + path.stem + '_' + hashlib.sha256(path.read_bytes()).hexdigest()[:16]
    if name not in sys.modules:
        spec = importlib.util.spec_from_file_location(name, path)
        module = importlib.util.module_from_spec(spec)
        sys.modules[name] = module
        spec.loader.exec_module(module)
    return sys.modules[name]


def _number(p, key, minimum=0., maximum=10000.):
    value = p[key]
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) \
            or value < minimum or value > maximum:
        raise ValueError(f'{key} 必须为 {minimum:g}～{maximum:g} 的有限数值')
    return float(value)


def _choice(p, key, values):
    if p[key] not in values:
        raise ValueError(f'{key} 选项无效')
    return p[key]


def _count(p, key):
    value = _number(p, key, 0, 500)
    if value != int(value):
        raise ValueError(f'{key} 必须是整数')
    return int(value)


def _planar_layout(p):
    """Resolve local clear openings and the minimum straight stock graph."""
    catalogue = _load(SHARED / 'tube_profile_catalog.py')
    frame, inner = (catalogue.load_profile(p, role) for role in ('frame', 'inner'))
    if frame.kind not in ('rect-bar', 'rect', 'round') or inner.kind not in ('rect-bar', 'rect', 'round'):
        raise ValueError('极简防护网的边框和内杆须选择扁钢、方矩管或圆管')
    width, height = _number(p, 'width', 100), _number(p, 'height', 100)
    fw, bw = frame.width, inner.width
    if width <= 2 * fw + bw or height <= 2 * fw + 3 * bw:
        raise ValueError('成品尺寸不足以容纳边框及内杆')
    frame_type = _choice(p, 'frameType', ('two_vertical', 'closed_frame', 'bottom_rail'))
    enabled = _choice(p, 'handleEnabled', ('yes', 'no')) == 'yes'
    mode = _choice(p, 'barLayoutMode', ('center_spacing', 'maximum_gap', 'fixed_count'))
    maximum_gap = _number(p, 'maximumClearGap', 1, 2000) if mode == 'maximum_gap' else None
    center_spacing = _number(p, 'barCenterSpacing', bw+EPS, 2000) if mode == 'center_spacing' else None
    bottom_frame, top_frame = frame_type != 'two_vertical', frame_type == 'closed_frame'
    low, high = (fw if bottom_frame else 0.), (height-fw if top_frame else height)
    top_inactive = top_frame or (mode == 'center_spacing' and not enabled and not bottom_frame)
    first = None if top_inactive else height - _number(p, 'firstBarTopOffset', bw/2)
    last = None if bottom_frame else _number(p, 'lastBarBottomOffset', bw/2)
    if first is not None and not low+bw/2 < first <= high-bw/2+EPS:
        raise ValueError('首根横杆顶部偏移超出可排布范围')
    if last is not None and not low+bw/2-EPS <= last < high-bw/2:
        raise ValueError('末根横杆底部偏移超出可排布范围')

    members, connections, openings = [], [], []
    by_key = {}

    def add(key, name, role, profile, x1, z1, x2, z2):
        if (x2-x1)+(z2-z1) <= EPS or x1 != x2 and z1 != z2:
            raise ValueError('当前尺寸产生零长或非正交杆件：' + name)
        item = dict(key=key, name=name, role=role, profile=profile,
                    start=(float(x1), 0., float(z1)), end=(float(x2), 0., float(z2)))
        members.append(item)
        by_key[key] = item
        return item

    side, closure, hb, ht, center = None, None, None, None, None
    if enabled:
        side = _choice(p, 'handleSide', ('left', 'right', 'center_double'))
        closure = _choice(p, 'handleClosure', ('closed', 'open_slot'))
        hb = _number(p, 'handleBottom')
        ht = hb + _number(p, 'handleHeight', 1)
        hw = _number(p, 'handleWidth', 1)
        if hb-bw <= low+EPS or ht+bw >= high-EPS:
            raise ValueError('把手净空及上下横杆须完整位于成品内侧')
        if last is not None and last+bw/2 >= hb-bw-EPS \
                or first is not None and first-bw/2 <= ht+bw+EPS:
            raise ValueError('首末横杆与把手边界横杆冲突，请调整把手高度或首末偏移')
        if side == 'left':
            openings = [dict(left=fw, right=fw+hw, bottom=hb, top=ht)]
        elif side == 'right':
            openings = [dict(left=width-fw-hw, right=width-fw, bottom=hb, top=ht)]
        else:
            center = width/2 + _number(p, 'handleCenterOffset', -10000, 10000)
            openings = [dict(left=center-bw/2-hw, right=center-bw/2, bottom=hb, top=ht),
                        dict(left=center+bw/2, right=center+bw/2+hw, bottom=hb, top=ht)]
        for opening in openings:
            if opening['left'] < fw-EPS or opening['right'] > width-fw+EPS:
                raise ValueError('把手净宽或中间偏移超出边框内侧')
        if side in ('left', 'right') and width-2*fw-hw <= bw+EPS \
                or side == 'center_double' and (openings[0]['left']-fw <= bw+EPS
                                               or width-fw-openings[1]['right'] <= bw+EPS):
            raise ValueError('把手口外侧须留出竖短杆和有效横杆长度')

    # Open slots alone cut the corresponding upright; all other uprights stay
    # physically continuous. The split interval is exactly the clear opening.
    for label, x in (('left', fw/2), ('right', width-fw/2)):
        intervals = [(0., height)]
        if enabled and closure == 'open_slot' and side == label:
            intervals = [(0., hb), (ht, height)]
        for i, (a, b) in enumerate(intervals, 1):
            add(f'frame.{label}.{i:04d}', ('左' if label == 'left' else '右')+'边框',
                f'frame.{label}', frame, x, a, x, b)
    if bottom_frame:
        add('frame.bottom.0001', '下边框', 'frame.bottom', frame, fw, fw/2, width-fw, fw/2)
    if top_frame:
        add('frame.top.0001', '上边框', 'frame.top', frame, fw, height-fw/2, width-fw, height-fw/2)

    # A double handle has a real central mullion separating the two panels.
    if center is not None:
        intervals = [(low, high)] if closure == 'closed' else [(low, hb), (ht, high)]
        for i, (a, b) in enumerate(intervals, 1):
            add(f'inner.center.{i:04d}', '中竖管', 'inner.center', inner, center, a, center, b)

    def rows(a, b, count_key, bottom=None, top=None):
        if mode == 'center_spacing':
            lo, hi = a+bw/2, b-bw/2
            if hi < lo-EPS:
                return []
            if bottom is not None:
                return [bottom+i*center_spacing for i in range(max(0,int(math.floor((hi-bottom)/center_spacing+EPS)))+1)]
            if top is not None:
                return sorted(top-i*center_spacing for i in range(max(0,int(math.floor((top-lo)/center_spacing+EPS)))+1))
            n = int(math.floor((hi-lo)/center_spacing+EPS))+1
            start = (lo+hi-(n-1)*center_spacing)/2
            return [start+i*center_spacing for i in range(n)]
        fixed = []
        if bottom is not None:
            fixed.append(bottom)
            a = bottom+bw/2
        if top is not None:
            fixed.append(top)
            b = top-bw/2
        available = b-a
        if available < -EPS:
            raise ValueError('排杆区域过小或首末横杆重叠')
        n = (_count(p, count_key)-len(fixed) if mode == 'fixed_count' else
             max(0, math.ceil((available-maximum_gap)/(bw+maximum_gap)-EPS)))
        if n < 0:
            raise ValueError('横杆根数不能少于首末边界横杆数量')
        if n*bw > available+EPS:
            raise ValueError('横杆根数超过当前区域容量')
        gap = (available-n*bw)/(n+1)
        if n and gap <= EPS:
            raise ValueError('横杆之间须保留净间距')
        return sorted(fixed+[a+gap+bw/2+i*(gap+bw) for i in range(n)])

    row_sets = []
    if enabled:
        row_sets = [('lower', rows(low, hb-bw, 'lowerBarCount', bottom=last)),
                    ('lower_boundary', [hb-bw/2]),
                    ('middle', rows(hb, ht, 'middleBarCount')),
                    ('upper_boundary', [ht+bw/2]),
                    ('upper', rows(ht+bw, high, 'upperBarCount', top=first))]
        if side == 'left':
            jambs = [openings[0]['right']+bw/2]
        elif side == 'right':
            jambs = [openings[0]['left']-bw/2]
        else:
            jambs = [openings[0]['left']-bw/2, openings[1]['right']+bw/2]
        for i, x in enumerate(jambs, 1):
            add(f'inner.handle_jamb.{i:04d}', '把手竖短杆', 'inner.handle_jamb', inner, x, hb, x, ht)
    else:
        row_sets = [('full', rows(low, high, 'barCount', bottom=last, top=first))]

    for zone, centers in row_sets:
        for i, z in enumerate(centers, 1):
            spans = [(fw, width-fw)] if center is None else [(fw, center-bw/2), (center+bw/2, width-fw)]
            if zone == 'middle':
                if side == 'left':
                    spans = [(openings[0]['right']+bw, width-fw)]
                elif side == 'right':
                    spans = [(fw, openings[0]['left']-bw)]
                else:
                    spans = [(fw, openings[0]['left']-bw), (openings[1]['right']+bw, width-fw)]
            # In an open double slot the central upright is absent locally.
            # Boundary rails still touch the retained upper/lower central stock.
            for j, (a, b) in enumerate(spans, 1):
                add(f'inner.{zone}.{i:04d}.{j:02d}',
                    '把手上横杆' if zone == 'upper_boundary' else '把手下横杆' if zone == 'lower_boundary' else '内横杆',
                    'inner.'+zone, inner, a, z, b, z)

    # Recover actual end-to-face contacts from the emitted stocks. Endpoints
    # never become a split instruction for their continuous receiving stock.
    for branch in members:
        vertical = branch['start'][0] == branch['end'][0]
        for end in ('start', 'end'):
            point = branch[end]
            candidates = []
            for host in members:
                if host is branch or (host['start'][0] == host['end'][0]) == vertical:
                    continue
                h, a, b = host['profile'].width/2, host['start'], host['end']
                if vertical:
                    touching = a[0]-EPS <= point[0] <= b[0]+EPS and abs(abs(point[2]-a[2])-h) <= EPS
                    normal = (0., 0., 1. if point[2] > a[2] else -1.)
                else:
                    touching = a[2]-EPS <= point[2] <= b[2]+EPS and abs(abs(point[0]-a[0])-h) <= EPS
                    normal = (1. if point[0] > a[0] else -1., 0., 0.)
                if touching:
                    candidates.append((host, normal))
            if len(candidates) > 1:
                raise ValueError('构件端部接收管不唯一：'+branch['name'])
            if candidates:
                host, normal = candidates[0]
                connections.append(dict(key=f'joint.{branch["key"]}.{end}', hostKey=host['key'],
                    branchKey=branch['key'], branchEnd=end, point=point, normal=normal))

    if len(members) > 1500:
        raise ValueError('当前排杆数量过多，请增大间距')
    installation = _load(ROOT / 'installation_holes.py').plan(p, members)
    return dict(members=members, connections=connections, facts=dict(
        frameType=frame_type, handleEnabled=enabled, handleSide=side, handleClosure=closure,
        barLayoutMode=mode, barCenterSpacing=center_spacing,
        clearOpenings=openings, continuousHorizontalRows=center is None,
        centralMullion=center, mergedCentralSlot=bool(center is not None and closure == 'open_slot'),
        rowCenters={zone:zs for zone,zs in row_sets},
        memberCount=len(members), contactCount=len(connections), installationHoles=installation))


def layout(p):
    style = p.get('layoutStyle', 'planar')
    if style == 'planar':
        result = _planar_layout(p)
        result['facts']['layoutStyle'] = 'planar'
        return result
    if style not in ('five_face', 'balcony_closure'):
        raise ValueError('极简防护网款式无效')
    # Derived front-frame choices are internal, never returned as normalized
    # host input. Inactive planar drafts cannot affect a fixed five-face frame.
    front = _planar_layout({**p, 'frameType':'closed_frame', 'installHoleEnabled':'no'})
    return _load(ROOT / 'spatial_layout.py').extend(p, front, _load, ROOT)


def display(parameter_values):
    state = layout(parameter_values)
    model = NeutralModel(template_id=TEMPLATE_ID, template_version=TEMPLATE_VERSION,
                         package_digest='', parameters=deepcopy(parameter_values))
    builder = _load(SHARED / 'shared_tube_geometry.py').SharedTubeGeometry(model)
    keys = []
    for member in state['members']:
        key, profile = member['key'], member['profile']
        start, end = member['start'], member['end']
        horizontal = start[2] == end[2]
        # Keep canonical section axes and a right-handed frame for each stock.
        if 'sectionFrame' in member:
            x, y, axis = (member['sectionFrame'][name] for name in ('xAxis','yAxis','zAxis'))
        else:
            x, y, axis = (([0.,0.,1.],[0.,-1.,0.],[1.,0.,0.]) if horizontal else
                          ([1.,0.,0.],[0.,1.,0.],[0.,0.,1.]))
        raw = builder.emit_tube(key, profile_arguments={
            'placement':{'origin':list(start),'xAxis':x,'yAxis':y}, 'contours':profile.contours()},
            extrude_arguments={'vector':[end[i]-start[i] for i in range(3)]})
        section = model.geometry(key+'.section', 'profile2d', arguments={'contours':profile.contours()})
        properties = profile.properties()
        properties.update(sectionResource=section, sectionCoordinateMap=[[1,0],[0,1]])
        length = math.dist(start, end)
        category = member.get('profileRole') or ('support' if member['role'] == 'frame.support' else
                    'frame' if member['role'].startswith('frame.') else 'inner')
        keys.append(model.item(key, member['name'], representations={'display':raw}, properties={
            'partNumber':f'{parameter_values["productCode"]}-{len(keys)+1:03d}',
            'length':length, 'quantity':1, 'partKind':'tube', 'materialCategory':'tube', 'sourcing':'made',
            'categoryKey':'minimal-grille.'+category,
            'categoryName':{'frame':'正面边框','inner':'内杆','support':'支撑竖杆',
                            'projectionVertical':'飘面竖向骨架',
                            'projectionHorizontal':'飘面横向骨架',
                            'drying':'晾衣杆及吊臂'}[category],
            'tubeDesigner.profile':properties,
            'tubeDesigner.productRule':{'product':TEMPLATE_ID,'role':member['role']},
            'tubeDesigner.minimalGrille':deepcopy(state['facts']),
            'assemblyFrame.member':{'start':list(start),'end':list(end),'axisLength':length,
                'sectionFrame':{'originAtStart':list(start),'xAxis':x,'yAxis':y,'zAxis':axis,'centerlineUV':[0.,0.]}}
        }))
    model.output('display.default','display',keys)
    document = model.build()
    annotations = []
    enabled = state['facts']['handleEnabled']
    for i, key in enumerate(('handleEnabled','handleSide','handleClosure','barLayoutMode')):
        if key in ('handleSide','handleClosure') and not enabled:
            continue
        point = [float(parameter_values['width'])+120.,0.,float(parameter_values['height'])-i*80.]
        annotations.append({'id':TEMPLATE_ID+'.'+key,'parameter':key,'kind':'parameter',
            'start':point,'end':list(point),'offset':[0.,0.,0.], 'generatedValue':parameter_values[key]})
    if state['facts']['barLayoutMode'] == 'center_spacing':
        key = 'barCenterSpacing'
        point = [float(parameter_values['width'])+120.,0.,float(parameter_values['height'])-320.]
        annotations.append({'id':TEMPLATE_ID+'.'+key,'parameter':key,'kind':'parameter',
            'start':point,'end':list(point),'offset':[0.,0.,0.],'generatedValue':parameter_values[key]})
    if enabled:
        opening = state['facts']['clearOpenings'][0]
        a, b, bottom, top = (opening[k] for k in ('left','right','bottom','top'))
        dimensions = (
            ('handleWidth', [a,0.,bottom], [b,0.,bottom], [0.,0.,-65.]),
            ('handleHeight', [b,0.,bottom], [b,0.,top], [55.,0.,0.]),
            ('handleBottom', [a,0.,0.], [a,0.,bottom], [-75.,0.,0.]),
        )
        for key, start, end, offset in dimensions:
            annotations.append({'id':TEMPLATE_ID+'.'+key,'parameter':key,'kind':'linear',
                'start':start,'end':end,'offset':offset,'generatedValue':parameter_values[key]})
    # These are distances to real row centers, not floating parameter labels.
    # Anchor on the rightmost stock endpoint so selected profile widths and
    # split rows around a central mullion remain reflected in the dimensions.
    frame_type = state['facts']['frameType']
    row_centers = [z for group in state['facts']['rowCenters'].values() for z in group]
    boundary_dimensions = []
    if frame_type != 'closed_frame' and not (state['facts']['barLayoutMode'] == 'center_spacing'
                                            and not enabled and frame_type == 'two_vertical'):
        boundary_dimensions.append(('firstBarTopOffset', float(parameter_values['height']), max(row_centers)))
    if frame_type == 'two_vertical':
        boundary_dimensions.append(('lastBarBottomOffset', 0., min(row_centers)))
    for key, boundary, center in boundary_dimensions:
        end = list(max((member['end'] for member in state['members']
                        if member['role'].startswith('inner.')
                        and abs(member['start'][2]-center) <= EPS
                        and abs(member['end'][2]-center) <= EPS), key=lambda point: point[0]))
        start = [end[0], end[1], boundary]
        offset = [float(parameter_values['width'])-end[0]+75., 0., 0.]
        annotations.append({'id':TEMPLATE_ID+'.'+key,'parameter':key,'kind':'linear',
            'start':start,'end':end,'offset':offset,'generatedValue':parameter_values[key]})
    installation = state['facts']['installationHoles']
    for index, key in enumerate(('installHoleEnabled', 'installHoleCountPerSide')):
        if index and not installation['enabled']:
            continue
        point = [-150., 0., float(parameter_values['height'])-index*80.]
        annotations.append({'id':TEMPLATE_ID+'.'+key,'parameter':key,'kind':'parameter',
            'start':point,'end':list(point),'offset':[0.,0.,0.],
            'generatedValue':parameter_values[key]})
    if installation['enabled']:
        left_hosts = [m for m in state['members'] if m['role'] == 'frame.left']
        bottom_host = min(left_hosts, key=lambda m: m['start'][2])
        top_host = max(left_hosts, key=lambda m: m['end'][2])
        for key, boundary, center in (
                ('installHoleTopOffset', top_host['end'][2], installation['centers'][-1]),
                ('installHoleBottomOffset', bottom_host['start'][2], installation['centers'][0])):
            group = next(g for g in installation['groups']
                         if g['side'] == 'left' and abs(g['point'][2]-center) <= EPS)
            x, y = group['point'][:2]
            annotations.append({'id':TEMPLATE_ID+'.'+key,'parameter':key,'kind':'linear',
                'start':[x,y,boundary],'end':[x,y,center],'offset':[-x-75.,0.,0.],
                'generatedValue':parameter_values[key]})
    if state['facts'].get('layoutStyle') in ('five_face', 'balcony_closure'):
        facts = state['facts']
        rear_y = facts['mountingPlaneY']
        front_outer_y = facts['frontOuterY']
        top_rear_outer_y = facts['topRearOuterY']
        bottom_rear_outer_y = facts['bottomRearOuterY']
        w, h = float(parameter_values['width']), float(parameter_values['height'])
        balcony = facts['layoutStyle'] == 'balcony_closure'
        spatial_dimensions = (
            [('balconyTopDepth',[w,front_outer_y,h],[w,top_rear_outer_y,h],[75.,0.,75.]),
             ('balconyBottomDepth',[w,front_outer_y,0.],[w,bottom_rear_outer_y,0.],[75.,0.,-75.]),
             ('balconyTopDrop',[w,top_rear_outer_y,h],[w,top_rear_outer_y,h+facts['topDrop']],[75.,0.,0.])]
            if balcony else
            [('projectionDepth',[w,front_outer_y,0.],[w,top_rear_outer_y,0.],[75.,0.,-75.]),
             ('topDrop',[w,top_rear_outer_y,h],[w,top_rear_outer_y,h+facts['topDrop']],[75.,0.,0.])])
        for key, start, end, offset in spatial_dimensions:
            annotations.append({'id':TEMPLATE_ID+'.'+key,'parameter':key,'kind':'linear',
                                'start':start,'end':end,'offset':offset,'generatedValue':parameter_values[key]})
        key = 'dryingRodEnabled'
        point = [w+120.,rear_y,h-400.]
        annotations.append({'id':TEMPLATE_ID+'.'+key,'parameter':key,'kind':'parameter',
            'start':point,'end':list(point),'offset':[0.,0.,0.],
            'generatedValue':parameter_values[key]})
        if parameter_values[key] == 'yes':
            rod = next(m for m in state['members'] if m['key'] == 'frame.drying.rod.0001')
            y,z = rod['start'][1:]
            for key,start,end in (
                    ('dryingRodRearOffset',[w,top_rear_outer_y,z],[w,y,z]),
                    ('dryingRodDrop',[w,y,h],[w,y,z])):
                annotations.append({'id':TEMPLATE_ID+'.'+key,'parameter':key,'kind':'linear',
                    'start':start,'end':end,'offset':[75.,0.,0.],
                    'generatedValue':parameter_values[key]})
        if facts.get('supportStyle') == 'double':
            posts = sorted((m for m in state['members'] if m['role'] == 'frame.support'),
                           key=lambda m:m['start'][0])
            left, right = posts[:2]
            key = 'supportPairClearGap'
            annotations.append({'id':TEMPLATE_ID+'.'+key,'parameter':key,'kind':'linear',
                'start':[left['start'][0]+left['profile'].width/2,0.,h/2],
                'end':[right['start'][0]-right['profile'].width/2,0.,h/2],
                'offset':[0.,-75.,0.],'generatedValue':parameter_values[key]})
            center = facts['supportGroupCenters'][0]
            key = 'supportLinkCount'
            point = [center,-125.,h/2+100.]
            annotations.append({'id':TEMPLATE_ID+'.'+key,'parameter':key,'kind':'parameter',
                'start':point,'end':list(point),'offset':[0.,0.,0.],
                'generatedValue':parameter_values[key]})
            levels = facts.get('supportLinkCenters', [])
            if levels:
                dimensions = [('supportLinkBottomOffset',0.,levels[0])]
                if len(levels) > 1:
                    dimensions.append(('supportLinkTopOffset',h,levels[-1]))
                for key,boundary,level in dimensions:
                    annotations.append({'id':TEMPLATE_ID+'.'+key,'parameter':key,'kind':'linear',
                        'start':[center,0.,boundary],'end':[center,0.,level],
                        'offset':[0.,-125.,0.],'generatedValue':parameter_values[key]})
        # Maximum constraints are parameter annotations, not dimensions falsely
        # pretending the uniformly distributed actual gap equals the limit.
        for i, key in enumerate(('supportMaxSpacing','returnMaximumClearGap')):
            point = [w/2,rear_y/2,h+facts['topDrop']+100.+i*80.]
            annotations.append({'id':TEMPLATE_ID+'.'+key,'parameter':key,'kind':'parameter',
                                'start':point,'end':list(point),'offset':[0.,0.,0.],
                                'generatedValue':parameter_values[key]})
    document.setdefault('extensions',{})['tubeDesigner.specificationAnnotations'] = annotations
    return to_display_model(document)


def manufacturing(parameter_values):
    return _load(ROOT / 'manufacturing_rules.py').manufacturing(parameter_values, layout(parameter_values))
