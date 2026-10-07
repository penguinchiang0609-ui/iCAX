"""Local fold processes over material data; no finished-product shape contract.

Stations and the returned row-major transforms use the straight stock's frame.
A host can execute and namespace any number of these results on one stock.
Material grouping, allocation and product-to-stock mapping belong to the host.
"""
import copy
import importlib.util
import math
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
TOOLS = {
    "segmented-bend": "segmented-bend",
    "node-v-notch-integrated": "v-notch-sharp",
    "node-embedded-arc-integrated": "embedded-arc-notch",
    "node-edge-arc-integrated": "edge-arc-groove",
    "flexible-slit-bend-integrated": "flexible-slit-bend",
}
FOLD_IDS = frozenset({"bend", *TOOLS})


def _load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


_punch = _load(ROOT / "_shared/punch_tool_runtime.py", "assembly_fold_punch")
_sections = _load(ROOT / "_shared/section_geometry.py", "assembly_fold_sections")
_profiles = _load(ROOT / "_shared/assembly_applicability_geometry.py", "assembly_fold_profiles")
_curves = _load(ROOT / "_shared/profile_recognition_geometry.py", "assembly_fold_curves")
_rect = _load(ROOT / "profile/rect/profile.py", "assembly_fold_rect_boundaries")


def _number(value, label):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError(f"{label}必须是有限数值")
    return float(value)


def _vector(value, label):
    if not isinstance(value, (list, tuple)) or len(value) != 3:
        raise ValueError(f"{label}必须是三维坐标")
    return [_number(v, label) for v in value]


def _dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def _cross(a, b):
    return [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]]


def _matrix(frame):
    x, y, z, o = (frame[key] for key in ("xAxis", "yAxis", "zAxis", "origin"))
    return [x[0], y[0], z[0], o[0], x[1], y[1], z[1], o[1],
            x[2], y[2], z[2], o[2], 0, 0, 0, 1]


def _point(matrix, point):
    return [sum(matrix[4*i+j]*point[j] for j in range(3))+matrix[4*i+3] for i in range(3)]


def _multiply(a, b):
    return [sum(a[4*i+k]*b[4*k+j] for k in range(4)) for i in range(4) for j in range(4)]


def _rotation(axis, angle, hinge):
    """Rigid rotation about a declared centre, in row-major stock space.

    A distributed-root arc labels its equivalent final-pose centre explicitly;
    that centre is not a physical material hinge.
    """
    x, y, z = axis
    c, s, t = math.cos(angle), math.sin(angle), 1-math.cos(angle)
    rows = [[t*x*x+c, t*x*y-s*z, t*x*z+s*y],
            [t*x*y+s*z, t*y*y+c, t*y*z-s*x],
            [t*x*z-s*y, t*y*z+s*x, t*z*z+c]]
    result = []
    for i, row in enumerate(rows):
        result.extend([*row, hinge[i]-_dot(row, hinge)])
    return [*result, 0, 0, 0, 1]


def _frame(geometry, station, angle):
    source = geometry.get("frame")
    if source is None:
        source = {"origin": [station, 0, 0], "xAxis": [1, 0, 0],
                  "yAxis": [0, 1, 0], "zAxis": [0, 0, 1]}
    if isinstance(source, (list, tuple)):
        if len(source) != 16:
            raise ValueError("加工坐标矩阵必须有 16 项")
        m = [_number(v, "加工坐标矩阵") for v in source]
        if any(abs(m[12+i]-v) > 1e-8 for i, v in enumerate((0, 0, 0, 1))):
            raise ValueError("加工坐标矩阵不是刚体矩阵")
        source = {"origin": [m[3], m[7], m[11]], "xAxis": [m[0], m[4], m[8]],
                  "yAxis": [m[1], m[5], m[9]], "zAxis": [m[2], m[6], m[10]]}
    if not isinstance(source, dict):
        raise ValueError("加工坐标必须是完整局部坐标或刚体矩阵")
    frame = {key: _vector(source.get(key), f"加工坐标 {key}")
             for key in ("origin", "xAxis", "yAxis", "zAxis")}
    axes = [frame[key] for key in ("xAxis", "yAxis", "zAxis")]
    if (any(abs(_dot(a, a)-1) > 1e-7 for a in axes)
            or any(abs(_dot(axes[i], axes[j])) > 1e-7 for i in range(3) for j in range(i))
            or math.dist(_cross(axes[0], axes[1]), axes[2]) > 1e-7):
        raise ValueError("加工坐标必须为正交、单位、右手坐标")
    if math.dist(frame["xAxis"], [1, 0, 0]) > 1e-7:
        raise ValueError("当前折弯刀具的加工轴必须与直下料轴一致，不能忽略斜轴坐标")
    if math.dist(frame["origin"], [station, 0, 0]) > 1e-7:
        raise ValueError("加工坐标原点必须与下料起点基准的 station 一致")
    # A negative fold closes the groove on the opposite wall. Keep the
    # magnitude positive for the unchanged cutters and reverse their face.
    if angle < 0:
        frame["yAxis"] = [-v for v in frame["yAxis"]]
        frame["zAxis"] = [-v for v in frame["zAxis"]]
    rotation = math.degrees(math.atan2(frame["yAxis"][2], frame["yAxis"][1]))
    return frame, rotation


def _profile_from_analysis(section):
    contours = []
    for loop in section["contours"]:
        segments = []
        for edge in loop["edges"]:
            kind = edge["kind"]
            if kind == "line":
                segments.append({"kind": "line", "start": list(edge["start"]), "end": list(edge["end"])})
            elif kind == "circleArc":
                first, last = edge["first"], edge["last"]
                # A full circle is split into two exact arcs; a three-point
                # arc whose start equals end would be ambiguous.
                pieces = 2 if abs(last-first) >= math.tau-1e-8 else 1
                for i in range(pieces):
                    a, b = first+(last-first)*i/pieces, first+(last-first)*(i+1)/pieces
                    segments.append({"kind": "arc", "start": _sections.arc_point(edge, a),
                                     "middle": _sections.arc_point(edge, (a+b)/2),
                                     "end": _sections.arc_point(edge, b)})
            else:
                raise ValueError("此截面需提供原始精确 profile 轮廓，不能把曲线近似成直线")
        contours.append({"kind": "path", "closed": loop["closed"], "segments": segments})
    return {"contours": contours}


def _section(part):
    section = part.get("section")
    if isinstance(section, dict) and section.get("schema") == "icax.mold-section":
        analysis = _sections.local_section(section)
        return _profile_from_analysis(analysis), analysis
    if isinstance(section, dict):
        profile = section.get("profile", section)
        if isinstance(profile, dict) and isinstance(profile.get("contours"), list):
            return copy.deepcopy(profile), _sections.from_profile(profile)
    measured = _profiles.standard_section(part)
    if measured is None:
        raise ValueError("当前管材需提供精确 section，不能根据未知管型名称猜测截面")
    if measured["kind"] == "round":
        profile = {"contours": [
            {"kind": "circle", "center": [0, 0], "radius": measured["width"]/2},
            {"kind": "circle", "center": [measured["offsetX"], measured["offsetY"]],
             "radius": measured["width"]/2-measured["wall"]}]}
    else:
        profile = {"contours": [
            _rect.box(measured["width"], measured["depth"], rs=measured["outerRadii"]),
            _rect.box(measured["innerWidth"], measured["innerDepth"],
                      measured["offsetX"], measured["offsetY"], rs=measured["innerRadii"])]}
    return profile, _sections.from_profile(profile)


def _cutter_interval(output):
    """Exact axial envelope of the cutter graph; subtractors do not enlarge it."""
    nodes = {node["key"]: node for node in output["model"]["geometry"]}
    cache = {}

    def bounds(key):
        if key in cache:
            return cache[key]
        node = nodes[key]
        op, args = node["operator"], node.get("arguments", {})
        if op == "profile2d":
            boxes = [_curves.bounds(_curves.contour(c, 1e-7)) for c in args["contours"]]
            placement = args["placement"]
            xs = [placement["origin"][0]+placement["xAxis"][0]*x+placement["yAxis"][0]*y
                  for box in boxes for x in (box[0], box[2]) for y in (box[1], box[3])]
            result = [min(xs), max(xs)]
        elif op == "extrude":
            lo, hi = bounds(node["inputs"][0])
            dx = args["vector"][0]
            result = [lo+min(0, dx), hi+max(0, dx)]
        elif op in ("boolean", "compound"):
            children = node["inputs"]
            if op == "boolean" and args.get("operation") in ("subtract", "difference", "intersection", "intersect"):
                children = children[:1]
            boxes = [bounds(child) for child in children]
            result = [min(b[0] for b in boxes), max(b[1] for b in boxes)]
        else:
            raise ValueError(f"尚未支持此折弯刀具的轴向包络算子：{op}")
        cache[key] = result
        return result

    return bounds(output["outputKey"])


def _edge_root_arc_final_pose(cutter, parameters, root, beta, matrix, axis):
    """Read the unchanged frozen cutter's root band and solve its final pose.

    The flat root band becomes the retained-side circular arc. Only the rigid
    outgoing tube's final transform is solved here, not plastic deformation or
    a collision-free intermediate motion. K remains a separate material amount.
    """
    nodes = {node['key']: node for node in cutter['model']['geometry']}
    base = nodes['notch-base-profile']['arguments']
    circle = nodes['arc-cylinder-profile']['arguments']
    for arguments in (base, circle):
        placement = arguments['placement']
        if (math.dist(placement['xAxis'], [1., 0., 0.]) > 1.e-7
                or math.dist(placement['yAxis'], [0., 0., 1.]) > 1.e-7):
            raise ValueError('边弧根部需要实际刀口的轴向圆柱基准')
    base_origin = base['placement']['origin']
    root_edges = [edge for contour in base['contours'] for edge in contour['segments']
                  if edge['kind'] == 'line'
                  and abs(edge['start'][1]+base_origin[2]-root) < 1.e-7
                  and abs(edge['end'][1]+base_origin[2]-root) < 1.e-7]
    if len(root_edges) != 1:
        raise ValueError('边弧刀口必须具有唯一的平根展开段')
    a, b = sorted(point[0]+base_origin[0] for point in (root_edges[0]['start'], root_edges[0]['end']))
    arcs = [edge for contour in circle['contours'] for edge in contour['segments']]
    if len(arcs) != 2 or any(edge['kind'] != 'arc' for edge in arcs):
        raise ValueError('边弧刀口必须保留实际圆柱圆弧')
    circles = [_curves.arc(edge['start'], edge['middle'], edge['end']) for edge in arcs]
    radius = circles[0]['radius']
    centre = [circles[0]['center'][0]+circle['placement']['origin'][0],
              circles[0]['center'][1]+circle['placement']['origin'][2]]
    if (radius <= 0 or abs(circles[1]['radius']-radius) > 1.e-7
            or math.dist(circles[0]['center'], circles[1]['center']) > 1.e-7
            or abs(centre[1]-root-radius) > 1.e-7
            or abs(centre[0]-(a if parameters['leftArc'] else b)) > 1.e-7):
        raise ValueError('边弧平根与实际圆柱切口的圆弧基准不一致')
    compensation = (beta*parameters['kFactor']*parameters['wallThickness']
                    if parameters.get('bendCompensation', False) else 0.)
    if abs((b-a)-(radius*beta+compensation)) > 1.e-7:
        raise ValueError('边弧平根展开长度与实际圆弧及独立K补偿不一致')
    cosine, sine = math.cos(beta), math.sin(beta)
    endpoint = [a+radius*sine, 0., root+radius*(1.-cosine)]
    tx = endpoint[0]-(cosine*b-sine*root)
    tz = endpoint[2]-(sine*b+cosine*root)
    # The nominal incoming centreline z=0 meets the transformed outgoing one.
    outgoing = -tz/sine
    corner = tx+cosine*outgoing
    reserves = {'incoming': -corner-compensation/2., 'outgoing': outgoing-compensation/2.}
    denominator = (1.-cosine)**2+sine*sine
    equivalent = [((1.-cosine)*tx-sine*tz)/denominator, 0.,
                  (sine*tx+(1.-cosine)*tz)/denominator]
    equivalent_stock = _point(matrix, equivalent)
    return {'targetTransform': _rotation(axis, beta, equivalent_stock),
            'equivalentCentre': equivalent_stock,
            'materialCornerReserves': reserves, 'materialLengthAddition': compensation,
            'rootArc': {'model': 'distributed-root-arc-final-pose', 'radius': radius,
                'angle': math.degrees(beta), 'flatRootInterval': [a, b],
                'rootHeight': root, 'rootArcLength': radius*beta,
                'compensationLength': compensation,
                'fixedArcSide': 'incoming' if parameters['leftArc'] else 'outgoing',
                'flatRootStart': _point(matrix, [a, 0., root]),
                'flatRootEnd': _point(matrix, [b, 0., root]),
                'formedRootEnd': _point(matrix, endpoint),
                'localRigidTranslation': [tx, 0., tz],
                'validation': 'final-pose-only',
                'limitations': ['plastic root deformation and intermediate sweep are not evaluated']}}


def _tool_values(tool, values, drafts, angle, resolved):
    if resolved is not None:
        match = next((item for item in resolved if item.get("id") == "node-slot"), None)
        if match is None:
            match = next((item for item in resolved if item.get("tool", item.get("resourceRef", {}).get("id")) == tool), None)
        if match is None and len(resolved) == 1:
            match = resolved[0]
        if match is None or not isinstance(match.get("values"), dict):
            raise ValueError("折弯工艺缺少已解析的单件刀具参数")
        descriptor = match.get("descriptor")
        reference = copy.deepcopy(match.get("ref", {"id": match.get("tool", tool)}))
        user_root = match.get("user_root")
        if descriptor is None:
            descriptor, _, _, _, _ = _punch._package(ROOT / "mold" / tool)
        defaults = {item["key"]: item.get("defaultValue") for item in descriptor.get("parameters", [])}
        result = copy.deepcopy(match["values"])
    else:
        # Standalone callers may pass already normalized direct tool values.
        # The production runtime resolves conditional drafts with its existing
        # shared parameter-condition path and passes resolved_processes.
        descriptor, defaults, _, _, _ = _punch._package(ROOT / "mold" / tool)
        reference, user_root = {"id": tool}, None
        result = copy.deepcopy(defaults)
        source = (drafts or {}).get("node-slot", {}).get(tool, {})
        if not isinstance(source, dict):
            raise ValueError("刀具参数必须是对象")
        result.update(copy.deepcopy(source))
    result["angle"] = abs(angle)
    if "bendRadius" in defaults:
        result["bendRadius"] = values["bendRadius"]
    return descriptor, result, reference, user_root


def _evaluate(template_id, process_input, values, drafts, resolved):
    if template_id not in FOLD_IDS:
        raise ValueError("此加工函数不支持当前折弯工艺")
    if (not isinstance(process_input, dict) or process_input.get("schema") != "icax.assembly-process-input"
            or process_input.get("schemaVersion") != 1):
        raise ValueError("折弯加工输入协议无效")
    parts, geometry = process_input.get("parts"), process_input.get("geometry")
    if not isinstance(parts, dict) or len(parts) != 1 or not isinstance(geometry, dict):
        raise ValueError("每个局部折弯实例需要一根真实下料和必要加工几何")
    role, part = next(iter(parts.items()))
    if not isinstance(part, dict):
        raise ValueError("下料数据必须是对象")
    length = _number(part.get("length"), "下料长度")
    station = _number(geometry.get("station"), "折弯轴向位置")
    angle = _number(geometry.get("angle"), "折弯转角")
    if not 0 < length <= 100000 or not 0 <= station <= length or not 0 < abs(angle) < 180:
        raise ValueError("下料长度、折弯位置或折弯转角超出范围")
    frame, rotation = _frame(geometry, station, angle)
    profile, section = _section(part)
    shell = _sections.closed_shell_metrics(section, rotation)
    depth = shell["outside"]["max"][1]-shell["outside"]["min"][1]
    matrix, beta = _matrix(frame), math.radians(abs(angle))
    base = {"applicable": True, "reason": "", "parameters": copy.deepcopy(values),
            "operations": [], "materialRequirements": [], "forming": [], "checks": []}
    axis = [-v for v in frame["yAxis"]]
    forming_extras = {}
    equivalent_centre = None
    if template_id == "bend":
        radius, factor = _number(values["bendRadius"], "折弯半径"), _number(values["bendFactor"], "折弯因子")
        if radius <= 0 or not 0 <= factor <= 1:
            raise ValueError("折弯半径或折弯因子无效")
        zone = (radius+factor*shell["wallThickness"])*beta
        interval = [station-zone/2, station+zone/2]
        calc = {"bendAngle": abs(angle), "bendRadius": radius, "bendFactor": factor,
                "wallThickness": shell["wallThickness"], "bendZoneLength": zone,
                "formingValidation": "not-performed"}
        validation, root, hinges = "not-performed", 0, []
        end_local = [-zone/2+radius*math.sin(beta), 0, radius*(1-math.cos(beta))]
        target = _rotation(axis, beta, [0, 0, 0])
        destination = _point(matrix, end_local)
        for i in range(3):
            target[4*i+3] = destination[i]-target[4*i]*interval[1]
        kind, pattern = "continuous-cold-bend", 0
    else:
        tool = TOOLS[template_id]
        descriptor, tool_values, reference, user_root = _tool_values(tool, values, drafts, angle, resolved)
        context = {"target": "part", "lengthUnit": "mm", "targetSection": profile,
                   "targetSectionAnalysis": section,
                   "bounds": {"min": [0, *shell["outside"]["min"]],
                              "max": [length, *shell["outside"]["max"]]},
                   "placement": {"rotation": rotation},
                   "feature": {"reference": "start", "station": station, "face": "top"}}
        snapshot = _punch._evaluate(reference, tool_values, context, user_root=user_root)
        snapshot["ref"] = {**copy.deepcopy(reference), **snapshot["ref"]}
        cutter, calc, p, analysis = (snapshot["geometry"], snapshot["geometry"]["calculation"],
                                    snapshot["parameters"], snapshot["context"]["analysis"])
        # A saved user resource keeps its own machining calculation. Its
        # identifier is not a reliable indication of the retained-root recipe.
        flexible = "targetCenterlineRadius" in calc and "flexibleLength" in calc
        calculated_angle = calc.get("bendAngle", calc.get("targetBendAngle"))
        if calculated_angle is None or abs(calculated_angle-abs(angle)) > 1e-7:
            raise ValueError("槽口实际开角与传入的折弯转角不一致")
        local_interval = _cutter_interval(cutter)
        pattern = local_interval[1]-local_interval[0]
        zone = calc.get("flexibleLength", calc.get("patternLength", pattern))
        # The process's affected material includes the uncut curvature margins,
        # not just the narrower physical slit envelope.
        interval = [station+local_interval[0], station+local_interval[1]]
        if flexible:
            interval = [station+min(local_interval[0], -zone/2),
                        station+max(local_interval[1], zone/2)]
        validation = calc.get("formingValidation", "not-performed")
        outside = analysis.get("outside")
        bottom = outside["min"][1] if outside else analysis["bounds"]["min"][2]
        if "leaveBottom" in p:
            root = bottom+p["leaveBottom"]+(p["wallThickness"] if p["bottomReference"] == "inner" else 0)
        elif "bridge" in p:
            root = bottom+p["bridge"]+(p["wallThickness"] if p["bottomReference"] == "inner" else 0)
        else:
            root = analysis["inside"]["min"][1]+p["rootClearance"]
        if flexible:
            radius = calc["targetCenterlineRadius"]
            if radius <= depth/2:
                raise ValueError("目标中心线半径过小，管材内侧可能自交")
            end_local = [-zone/2+radius*math.sin(beta), 0, radius*(1-math.cos(beta))]
            target = _rotation(axis, beta, [0, 0, 0])
            destination = _point(matrix, end_local)
            for i in range(3):
                target[4*i+3] = destination[i]-target[4*i]*interval[1]
            hinges, kind = [], "flexible-slit-bend"
        elif template_id == 'node-edge-arc-integrated':
            solved = _edge_root_arc_final_pose(cutter, p, root, beta, matrix, axis)
            target, equivalent_centre = solved['targetTransform'], solved['equivalentCentre']
            hinges, kind = [], 'distributed-root-arc'
            validation = 'not-performed'
            forming_extras = {key: copy.deepcopy(solved[key]) for key in
                              ('materialCornerReserves', 'materialLengthAddition', 'rootArc')}
            forming_extras['hingePointKind'] = 'equivalent-final-pose-centre'
        else:
            count = int(calc.get("segmentCount", 1))
            pitch = calc.get("slotPitch", calc.get("chordPitch", 0))
            hinges, target = [], [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
            for i in range(count):
                local_station = (i-(count-1)/2)*pitch
                hinge = _point(matrix, [local_station, 0, root])
                step = _rotation(axis, beta/count, hinge)
                target = _multiply(target, step)
                hinges.append({"station": station+local_station, "angle": angle/count,
                               "hingePoint": hinge, "axis": axis, "targetTransform": step})
            kind = "segmented-bend" if count > 1 else "node-groove-fold"
        feature = {"id": "fold-cut", "name": descriptor["displayName"], "enabled": True,
                   "toolTarget": "part", "toolRef": copy.deepcopy(snapshot["ref"]),
                   "toolParameters": copy.deepcopy(p), "reference": "start", "station": station,
                   "face": "top", "rotation": rotation, "offset": 0,
                   "arrayCount": 1, "arrayPitch": 0, "rowCount": 1, "rowPitch": 0}
        base["operations"].append({"id": "fold-cut", "kind": "cut", "role": role,
            "toolRef": copy.deepcopy(snapshot["ref"]), "toolParameters": copy.deepcopy(p),
            "requestFeature": feature, "placement": {"reference": "start", "station": station,
                "rotation": rotation, "frame": frame, "matrix": matrix},
            "cutter": {key: copy.deepcopy(cutter[key]) for key in ("mode", "coordinateSpace", "outputKey")},
            "interval": [station+local_interval[0], station+local_interval[1]]})
    if interval[0] < -1e-7 or interval[1] > length+1e-7:
        raise ValueError("折弯加工区超出下料两端，需由材料规划提供足够直段")
    base["materialRequirements"].append({"role": role, "interval": interval,
        "patternLength": pattern, "bendZoneLength": zone, "lengthAddition": 0,
        "stockLength": length, "datum": "stock-start"})
    base["forming"].append({"id": "fold", "role": role, "kind": kind, "station": station,
        "angle": angle, "frame": frame, "matrix": matrix, "axis": axis,
        "hingePoint": equivalent_centre if equivalent_centre is not None else _point(matrix, [0, 0, root]),
        "targetTransform": target, "localFolds": hinges, "validation": validation,
        "calculation": copy.deepcopy(calc), **forming_extras})
    base["checks"].append({"id": "material-range", "status": "pass", "interval": interval})
    return base


def evaluate_fold(template_id, process_input, normalized_process_values, process_drafts=None,
                  resolved_processes=None):
    """Evaluate one reusable fold; never mutate any supplied object.

    The host normalizes tool drafts using its shared condition machinery and
    passes ``resolved_processes=[{id, tool, values}]``. Returned material ranges
    refer to already allocated stock; they do not request duplicate material.
    """
    try:
        return _evaluate(template_id, copy.deepcopy(process_input),
                         copy.deepcopy(normalized_process_values), copy.deepcopy(process_drafts),
                         copy.deepcopy(resolved_processes))
    except (ValueError, KeyError, TypeError, OverflowError) as error:
        return {"applicable": False, "reason": str(error) or "折弯加工数据无效",
                "parameters": copy.deepcopy(normalized_process_values), "operations": [],
                "materialRequirements": [], "forming": [], "checks": []}
