"""Declare machining on the grille's actual, continuous straight members.

Display supplies the intrinsic sections.  The shared manufacturing executor
prepares the requested axial stock and owns every Boolean operation.
"""
from __future__ import annotations

from copy import deepcopy
from functools import lru_cache
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import sys

from icax_template_sdk.manufacturing import validate_manufacturing_model


EPS = 1.0e-7


@lru_cache(maxsize=2)
def _geometry_queries(name):
    path = Path(__file__).resolve().parents[2] / "_shared" / (name + ".py")
    identity = "icax_minimal_grille_query_" + name + "_" + hashlib.sha256(path.read_bytes()).hexdigest()[:16]
    if identity not in sys.modules:
        spec = importlib.util.spec_from_file_location(identity, path)
        module = importlib.util.module_from_spec(spec)
        sys.modules[identity] = module
        spec.loader.exec_module(module)
    return sys.modules[identity]


@lru_cache(maxsize=128)
def _section_evidence(encoded, width, depth, nominal_wall):
    """Prove real side-wall extents from exact contours, including snapshots."""
    contours = json.loads(encoded)
    boundary = _geometry_queries("profile_recognition_geometry")
    geometry = _geometry_queries("section_geometry")
    raw = boundary.bounds(boundary.contour(contours[0], EPS))
    expected = (-width / 2, -depth / 2, width / 2, depth / 2)
    if any(abs(a - b) > EPS for a, b in zip(raw, expected)):
        raise ValueError("插接截面实际轮廓须与构件中心和截面尺寸一致")
    section = geometry.from_profile({"contours": contours}, tolerance=EPS)
    lateral = geometry.closed_shell_metrics(section, rotation=90)
    front = geometry.closed_shell_metrics(section)
    if (lateral["circularShell"] or front["circularShell"]
            or abs(lateral["wallThickness"] - front["wallThickness"]) > EPS
            or abs(lateral["wallThickness"] - nominal_wall) > EPS):
        raise ValueError("插接管材须具有可证实的等厚平直壁面")
    def faces_for(metrics):
        outside, inside = metrics['outside'], metrics['inside']
        outer, inner = metrics['contours']
        faces = {}
        for side in ('min', 'max'):
            a = geometry.horizontal_support(outer, outside[side][1], EPS)
            b = geometry.horizontal_support(inner, inside[side][1], EPS)
            gap = geometry.parallel_gap(a, b)
            faces[side] = {'range': gap['range'], 'wall': gap['distance']}
        return faces
    return {"faces": faces_for(lateral), "frontFaces": faces_for(front),
            "wall": lateral["wallThickness"]}


def _profile_evidence(profile):
    return _section_evidence(json.dumps(profile.contours(), sort_keys=True, separators=(",", ":")),
                             profile.width, profile.depth, profile.wall)


@lru_cache(maxsize=128)
def _shell_envelope(encoded, width, depth, nominal_wall):
    """Read the actual shell envelope without requiring any planar wall span."""
    contours = json.loads(encoded)
    boundary = _geometry_queries('profile_recognition_geometry')
    if len(contours) != 2:
        raise ValueError('公母插接管材须有一个外轮廓和一个内轮廓')
    outside, inside = [boundary.bounds(boundary.contour(loop, EPS)) for loop in contours]
    expected = (-width/2, -depth/2, width/2, depth/2)
    if any(abs(a-b) > EPS for a, b in zip(outside, expected)):
        raise ValueError('插接截面实际轮廓须与构件中心和截面尺寸一致')
    walls = (inside[0]-outside[0], inside[1]-outside[1],
             outside[2]-inside[2], outside[3]-inside[3])
    if any(value <= EPS or abs(value-nominal_wall) > EPS for value in walls):
        raise ValueError('插接管材实际轮廓与壁厚参数不一致')
    return {'wall': nominal_wall}


def _profile_shell(profile):
    return _shell_envelope(json.dumps(profile.contours(), sort_keys=True, separators=(',', ':')),
                           profile.width, profile.depth, profile.wall)


def _number(value, label, minimum=0.0):
    if (isinstance(value, bool) or not isinstance(value, (int, float))
            or not math.isfinite(value) or value < minimum):
        raise ValueError(label + "必须是有效数值")
    return float(value)


def _point(value, label):
    if not isinstance(value, (list, tuple)) or len(value) != 3:
        raise ValueError(label + "必须为三维坐标")
    return tuple(_number(v, label, -math.inf) for v in value)


def _dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def _axis(member):
    start, end = (_point(member[name], "构件端点") for name in ("start", "end"))
    length = math.dist(start, end)
    if length <= EPS:
        raise ValueError("构件长度必须大于零")
    axis = tuple((end[i] - start[i]) / length for i in range(3))
    if min(math.dist(axis, (1, 0, 0)), math.dist(axis, (0, 0, 1))) > EPS:
        raise ValueError("极简防护网构件必须沿正向横轴或竖轴")
    return start, end, axis, length


def _frame(member):
    start, _, axis, _ = _axis(member)
    # Local X is always axial.  The male-head provider's local X/Z contour
    # consequently works on horizontal bars and on vertical short bars alike.
    across = (-axis[2], 0.0, axis[0])
    return {"origin": list(start), "xAxis": list(axis),
            "yAxis": [0.0, 1.0, 0.0], "zAxis": list(across)}


def _call(key, item_key, template_id, geometry, *, parameters=None,
          previous=None, frame=None, facts=None):
    data = deepcopy(facts or {})
    if frame is not None:
        data["initialGeometry"] = {"itemPlacement": deepcopy(frame)}
    binding = {"scope": "manufacturing", "itemKey": item_key,
               "state": {"after": previous} if previous else "initial"}
    if data:
        binding["data"] = data
    return {"key": key, "kind": "assembly-process", "definition": {
        "templateId": template_id,
        "processInput": {"schema": "icax.assembly-process-input", "schemaVersion": 2,
                         "parts": {"stock": binding}, "geometry": deepcopy(geometry)},
        "parameters": deepcopy(parameters or {}), "processDrafts": {},
        "targets": {"stock": item_key}, "dependencies": [previous] if previous else []}}


def _validate_contact(connection, members):
    host, branch = members[connection["hostKey"]], members[connection["branchKey"]]
    hs, _, ha, hl = _axis(host)
    bs, be, ba, _ = _axis(branch)
    end = connection["branchEnd"]
    if end not in ("start", "end"):
        raise ValueError("连接端必须为 start 或 end")
    point = _point(connection["point"], "连接点")
    normal = _point(connection["normal"], "连接面法向")
    expected = ba if end == "start" else tuple(-v for v in ba)
    if (math.dist(point, bs if end == "start" else be) > EPS
            or math.dist(normal, expected) > EPS or abs(_dot(ha, ba)) > EPS):
        raise ValueError("连接必须位于分支实际端点，且与接收管正交")
    delta = tuple(point[i] - hs[i] for i in range(3))
    station = _dot(delta, ha)
    if (station < -EPS or station > hl + EPS or abs(delta[1]) > EPS
            or abs(_dot(delta, normal) - host["profile"].width / 2) > EPS):
        raise ValueError("连接点不在接收管实际外表面")
    return host, branch, point, normal, station


def _joint_settings(parameters, through=False):
    # A through tongue ends at the receiver's opposite outer face. Its length
    # comes from that actual profile, never from the inactive single-wall draft.
    length = None if through else _number(parameters["maleHeadLength"], "公头长度", EPS)
    form = parameters.get('maleEndForm','tongue')
    if form not in ('tongue','full_section'):
        raise ValueError('公头形式无效')
    if form == 'full_section':
        return {'form':form,'length':length,'width':None,'corner':'square','size':0.,'web':0.,
                'clearance':_number(parameters['fullHeadTotalClearance'],'母口总间隙')}
    width = _number(parameters["maleHeadWidth"], "公头宽度", EPS)
    corner = parameters["maleCornerType"]
    if corner not in ("square", "chamfer", "round"):
        raise ValueError("公头端角形式无效")
    size = 0.0 if corner == "square" else _number(parameters["maleCornerSize"], "端角尺寸", EPS)
    if size and ((length is not None and size >= length / 2 - EPS) or size >= width / 2 - EPS):
        raise ValueError("端角尺寸必须小于公头长度和宽度的一半")
    return {"form":form,"length": length, "width": width, "corner": corner, "size": size,
            'count': {'two':2,'four':4}[parameters.get('maleTabCount','two')],
            'angle': _number(parameters.get('maleTabAngle',0), '插舌周向角', -math.inf),
            "clearanceDepth": _number(parameters["holeTotalClearanceDepth"], "母孔深向总间隙"),
            "clearanceHeight": _number(parameters["holeTotalClearanceHeight"], "母孔宽向总间隙"),
            "web": _number(parameters["minimumFeatureWeb"], "公头肩部余量", EPS)}


def _receiver_holes(host, branch, point, normal, station, settings, through):
    hp, bp = host["profile"], branch["profile"]
    if not hp.hollow or not bp.hollow:
        raise ValueError("公母插接须选择中空管材")
    length, width, web = settings["length"], settings["width"], settings["web"]
    host_evidence, branch_evidence = _profile_shell(hp), _profile_shell(bp)
    if not through and not host_evidence["wall"] < length < hp.width - host_evidence["wall"]:
        raise ValueError("单边公头须穿过接收侧壁，且不能进入对侧管壁")
    if settings['size'] and settings['size'] >= length / 2 - EPS:
        raise ValueError("端角尺寸必须小于公头长度和宽度的一半")
    full = settings['form'] == 'full_section'
    if not full and (width <= 2 * branch_evidence["wall"] + EPS or width > bp.width - 2 * web + EPS):
        raise ValueError("公头宽度须保留有效壁料和两侧肩部")
    hole_depth = bp.depth + settings['clearance'] if full else bp.depth + settings["clearanceDepth"]
    hole_height = bp.width + settings['clearance'] if full else width + settings["clearanceHeight"]
    contours = ([bp.contours(clearance=settings['clearance']/2)[0]] if full else
                _geometry_queries('directional_tube_geometry').expanded_ring(
                    bp.contours(),settings['clearanceDepth']/2))
    faces = [(0., 1.)]
    if through:
        faces.append((hp.width, -1.))
    # The narrowed male tongue is contained by this exact rectangular slot.
    # A rounded slot must not clip the tongue's real outer corners.
    # Cut each receiving half-shell to the cavity centre. A nominal-wall-depth
    # slab leaves curved-wall material in a slot crossing a corner. No flat-span,
    # edge-web or slot-spacing heuristic decides whether this receiver survives:
    # the native manufacturing host validates its final exact BRep as one solid.
    return [{"width": hole_depth, "height": hole_height, "radius": 0.0,
             'contours':contours,
             **({} if full else {'directions':{
                 'count':settings['count'],'angle':settings['angle'],
                 'width':width+settings['clearanceHeight'],
                 'zAxis':[0.,1.,0.],'yAxis':list(_frame(branch)['zAxis']),
                 'reach':4*(bp.width+bp.depth+hp.width+hp.depth+1)}}),
             "placement": {"origin": [point[i] - offset * normal[i] + EPS * outward * normal[i]
                                      for i in range(3)],
                           "xAxis": [0.,0.,1.],
                           "yAxis": [0.,-1.,0.]},
             "vector": [-outward * v * (hp.width/2 + 2 * EPS) for v in normal]}
            for offset, outward in faces]


def _curved_mate_end(host, branch, point, normal):
    """Observe the real receiver outer skin in the inserted stock's frame."""
    start, _, axis, length = _axis(branch)
    across = (-axis[2], 0., axis[0])
    basis = (axis,(0.,1.,0.),across)
    def local_position(p):
        delta = tuple(p[i]-start[i] for i in range(3))
        return [_dot(delta,b) for b in basis]
    def local_direction(v):return [_dot(v,b) for b in basis]
    host_start, _, host_axis, host_length = _axis(host)
    position = local_position(point)
    direction = local_direction(normal)[0]
    middle = position[0]-direction*host['profile'].width/2
    extent = 2*(length+host_length+host['profile'].width+branch['profile'].width+branch['profile'].depth)+10
    return {'station':position[0],'direction':direction,
            'envelope':{'placement':{'origin':local_position(host_start),
                                    'xAxis':local_direction((1.,0.,0.)),
                                    'yAxis':local_direction((0.,1.,0.))},
                        'contours':[host['profile'].contours()[0]],
                        'vector':[v*host_length for v in local_direction(host_axis)]},
            'nearRegion':{'center':[middle+direction*extent/2,0.,0.],
                          'xAxis':[1.,0.,0.],'yAxis':[0.,1.,0.],
                          'width':extent,'height':extent*2,'thickness':extent*2}}


def _installation_holes(parameters, layout, members):
    """Declare two different single-wall circles; preserve the screw bearing wall."""
    plan = layout['facts']['installationHoles']
    if not plan['enabled']:
        return {}
    large = _number(parameters['installHoleLargeDiameter'], '安装大孔直径', EPS)
    small = _number(parameters['installHoleSmallDiameter'], '安装小孔直径', EPS)
    web = _number(parameters['installHoleMinimumWeb'], '安装孔边最小剩余壁料', EPS)
    if large <= small + EPS:
        raise ValueError('安装大孔直径必须大于小孔直径，保留小孔侧的螺丝头承压壁')
    orientation = parameters.get('installHoleOrientation', 'side')
    if orientation not in ('side', 'front'):
        raise ValueError('安装孔方向只能选择侧装或正装')
    holes, used = {}, {}
    for group in plan['groups']:
        host = members[group['memberKey']]
        profile = host['profile']
        evidence = _profile_evidence(profile)
        z, station = group['point'][2], group['station']
        radius = large / 2
        if min(station, _axis(host)[3]-station) < radius+web-EPS:
            raise ValueError('安装孔距竖管端部或把手开槽断口不足，无法保留最小余料')
        for previous in used.setdefault(host['key'], []):
            if abs(z-previous) < large+web-EPS:
                raise ValueError('相邻安装孔重叠或孔间剩余壁料不足，请调整组数或上下端距')
        used[host['key']].append(z)
        inward = 1. if group['side'] == 'left' else -1.
        # The mounting access must remain clear of real rail/frame contacts,
        # including male shoulders outside the narrower half-hole itself.
        for contact in layout['connections']:
            if contact['hostKey'] != host['key'] or contact['normal'][0]*inward < 1.-EPS:
                continue
            branch = members[contact['branchKey']]
            if orientation == 'side' and abs(z-contact['point'][2]) < branch['profile'].width/2+radius+web-EPS:
                raise ValueError('安装孔与横杆接头或外框转角重叠，无法保留余料和安装通道，请调整组数或上下端距')
            if (orientation == 'front' and parameters.get('jointMode') in
                    ('male_female_half_hole', 'male_female_through_hole')
                    and branch['role'].startswith('inner.') and _axis(branch)[2][0] > 1.-EPS):
                # A front screw crosses the cavity on a Y axis. A short tongue
                # stays near the side wall; a long tongue can obstruct that path.
                tongue_length = (profile.width if parameters['jointMode'] == 'male_female_through_hole'
                                 else float(parameters['maleHeadLength']))
                dx = max(0., profile.width/2-tongue_length)
                head_width = (branch['profile'].width if parameters.get('maleEndForm') == 'full_section'
                              else float(parameters['maleHeadWidth']))
                dz = max(0., abs(z-contact['point'][2])-head_width/2)
                if math.hypot(dx, dz) < radius+web-EPS:
                    raise ValueError('正装安装孔的螺丝通过路径被公头遮挡，请调整公头长度或安装孔位置')
        large_normal = inward if orientation == 'side' else -1.
        coordinate = 0 if orientation == 'side' else 1
        half_span = profile.width/2 if orientation == 'side' else profile.depth/2
        for normal, diameter in ((large_normal, large), (-large_normal, small)):
            # Rotation=90 maps canonical +X to the min support; unrotated
            # front/back walls retain the canonical Y min/max convention.
            face = (evidence['faces']['min' if normal > 0 else 'max'] if orientation == 'side'
                    else evidence['frontFaces']['max' if normal > 0 else 'min'])
            r = diameter / 2
            if -r-web < face['range'][0]-EPS or r+web > face['range'][1]+EPS:
                raise ValueError('安装孔径超过当前安装方向的竖管真实平直区域，无法保留最小余料；请调整 A 管用料或孔径')
            origin = [host['start'][0], 0., z]
            origin[coordinate] += normal*(half_span+EPS)
            vector = [0.,0.,0.]
            vector[coordinate] = -normal*(face['wall']+2*EPS)
            holes.setdefault(host['key'], []).append({
                'diameter':diameter,
                'placement':{'origin':origin,'xAxis':([0.,1.,0.] if orientation == 'side' else [1.,0.,0.]),
                             'yAxis':[0.,0.,1.]}, 'vector':vector})
    return holes


def manufacturing(parameters, layout):
    """Return v4 declarations without evaluating or embedding machining geometry."""
    mode = parameters.get("jointMode", "butt_weld")
    if mode not in ("butt_weld", "male_female_half_hole", "male_female_through_hole"):
        raise ValueError("内杆连接方式无效")
    members = {member["key"]: member for member in layout["members"]}
    if len(members) != len(layout["members"]) or not members:
        raise ValueError("极简防护网构件标识必须唯一且不为空")
    for member in members.values():
        _axis(member)
    through = mode == 'male_female_through_hole'
    settings = _joint_settings(parameters, through) if mode != "butt_weld" else None
    ends = {key: {} for key in members}
    head_lengths = {key:{} for key in members}
    mate_ends = {key:[] for key in members}
    connected_ends = {key: set() for key in members}
    holes = {key: [] for key in members}
    receiving = {key: [] for key in members}
    connections, seen = [], set()
    for connection in layout["connections"]:
        if connection["key"] in seen:
            raise ValueError("连接标识重复")
        seen.add(connection["key"])
        host, branch, point, normal, station = _validate_contact(connection, members)
        branch_key, host_key, end = branch["key"], host["key"], connection["branchEnd"]
        if end in connected_ends[branch_key]:
            raise ValueError("同一管材端部不能重复分配连接")
        connected_ends[branch_key].add(end)
        # This control belongs to frame-to-horizontal-infill connections.
        # Handle jambs, central B posts and A frame corners remain butt welds.
        inserted = (settings is not None and host["role"].startswith("frame.")
                    and branch["role"].startswith("inner.")
                    and _axis(branch)[2][0] > 1.0 - EPS)
        recipe = mode if inserted else "butt_weld"
        properties = {"recipe": recipe, "branchEnd": end, "contactPoint": list(point),
                      "faceNormal": list(normal), "participantRoles": ["receiver", "inserted"]}
        if inserted:
            joint = {**settings, 'length': host['profile'].width} if through else settings
            if joint['form'] == 'full_section':joint = {**joint,'width':branch['profile'].width}
            head_lengths[branch_key][end] = joint['length']
            # This is a construction envelope, not an extra purchased piece.
            # Native measured output bounds define the finished cut length.
            curved = host['profile'].kind == 'round'
            ends[branch_key][end] = max(joint['length'],host['profile'].width/2) if curved else joint['length']
            if curved:mate_ends[branch_key].append(_curved_mate_end(host,branch,point,normal))
            apertures = _receiver_holes(host, branch, point, normal, station, joint, through)
            for prior_station, prior_normal, prior_height, prior_length in receiving[host_key]:
                gap = abs(station - prior_station) - (apertures[0]["height"] + prior_height) / 2
                same_side = _dot(normal, prior_normal) > 0
                if not same_side and gap < -EPS and joint["length"] + prior_length >= host["profile"].width - EPS:
                    raise ValueError("相对公头在接收管内部相撞")
            receiving[host_key].append((station, normal, apertures[0]["height"], joint['length']))
            holes[host_key].extend(apertures)
            properties.update(halfHole=not through, insertionDepth=joint["length"],
                              maleEndForm=joint['form'],
                              totalClearanceDepth=(joint['clearance'] if joint['form']=='full_section' else joint['clearanceDepth']),
                              totalClearanceHeight=(joint['clearance'] if joint['form']=='full_section' else joint['clearanceHeight']),
                              manufacturingProcesses={
                                  host_key: [host_key + ".apertures"],
                                  branch_key: [branch_key + ".male-head"]})
            if through:
                properties.update(throughHole=True, flushWithOppositeOuterFace=True)
        connections.append({"key": connection["key"], "kind": "assembly" if inserted else "weld",
                            "items": [host_key, branch_key], "properties": properties})

    for key, installation in _installation_holes(parameters, layout, members).items():
        holes[key].extend(installation)

    processes = []
    for index, (key, member) in enumerate(members.items(), start=1):
        length = _axis(member)[3]
        first = -ends[key].get('start', 0.)
        last = length + ends[key].get('end', 0.)
        prepared = key + ".prepare"
        processes.append(_call(prepared, key, "profile-stock-preparation", {
            "axialInterval": {"startStation": first, "endStation": last},
            "direction": "forward", "status": "prepared", "ready": True}, parameters={
                "requestedPartNumber": member.get("partNumber", str(parameters.get("productCode", "JJ")) + f"-{index:03d}"),
                "reportingLengthPrecision": 3}))
        previous = prepared
        if settings and ends[key]:
            profile = member["profile"]
            # This product uses one actual A profile for every receiver. Prove
            # the common length before passing it to the shared head envelope.
            extension = next(iter(head_lengths[key].values()))
            if any(abs(value-extension) > EPS for value in head_lengths[key].values()):
                raise ValueError('同一内杆两端的公头长度不一致，当前统一 A 管用料无法生成')
            head_settings = {**settings, 'length': extension,
                             'width':profile.width if settings['form']=='full_section' else settings['width']}
            head_key = key + ".male-head"
            processes.append(_call(head_key, key, "structural-male-head", {
                "visibleLeft": 0.0, "visibleRight": length, "centerZ": 0.0,
                "profileWidth": profile.width, "profileDepth": profile.depth, "wall": profile.wall,
                **({'mateEnds':mate_ends[key]} if mate_ends[key] else {})},
                parameters={name: head_settings[name] for name in ("length", "width", "corner", "size", "form")}
                    | ({name:head_settings[name] for name in ('count','angle')} if settings['form']=='tongue' else {}),
                previous=previous, frame=_frame(member), facts={
                    "length": last - first, "start": [first, 0.0, 0.0], "end": [last, 0.0, 0.0],
                    "section": profile.properties()}))
            # The stock extends only at connected ends.  The unused opposite
            # envelope is outside that stock and cannot create a free-end head.
            previous = head_key
        if holes[key]:
            processes.append(_call(key + ".apertures", key, "structural-apertures",
                                   {"holes": holes[key]}, previous=previous, facts={
                                       "length": length, "start": list(member["start"]),
                                       "end": list(member["end"]), "section": member["profile"].properties()}))
    result = {"schema": "icax.manufacturing-model", "schemaVersion": 4,
              "connections": connections, "processes": processes}
    validate_manufacturing_model(result)
    return result
