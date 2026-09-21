import math
import copy

def analyze(parameters, section, context):
    try:
        local = section_geometry.local_section(section, context.get("placement", {}).get("rotation", 0))
        loops = local["contours"]
        outer = [loop for loop in loops if not loop["inner"]]
        inner = [loop for loop in loops if loop["inner"]]
        if len(outer) != 1 or len(inner) != 1 or not all(loop["closed"] for loop in loops):
            raise section_geometry.SectionError("槽口需要一个闭合外轮廓和一个闭合内轮廓")
        outside, inside = section_geometry.bounds(outer[0]), section_geometry.bounds(inner[0])
        tolerance = section_geometry.number(local.get("tolerance", 0.001))
        if tolerance <= 0:
            raise section_geometry.SectionError("截面容差无效")
        # A circular shell has no flat support. Determine concentric walls from
        # analytic arcs, never from a profile ID or a nominal tube type label.
        circles=[]
        for loop in (outer[0],inner[0]):
            edges=loop['edges']
            if not all(e['kind']=='circleArc' for e in edges):break
            center=edges[0]['center'];radius=edges[0]['radius']
            if any(math.dist(e['center'],center)>tolerance or abs(e['radius']-radius)>tolerance for e in edges):break
            if abs(sum(abs(e['last']-e['first']) for e in edges)-math.tau)*radius>tolerance:break
            circles.append((center,radius))
        if len(circles)==2:
            wall=circles[0][1]-circles[1][1]
            if wall<=tolerance or math.dist(circles[0][0],circles[1][0])>tolerance:
                raise section_geometry.SectionError("圆弧截面必须为同心、正壁厚闭口轮廓")
            data={"wallThickness":wall,"circularShell":True,"bounds":{
                "min":[context['bounds']['min'][0],*outside['min']],
                "max":[context['bounds']['max'][0],*outside['max']]}}
            return {"applicable":True,"reason":"","data":data,"derivedParameters":{"wallThickness":wall}}
        bottom = section_geometry.horizontal_support(outer[0], outside["min"][1], tolerance)
        inner_bottom = section_geometry.horizontal_support(inner[0], inside["min"][1], tolerance)
        top = section_geometry.horizontal_support(outer[0], outside["max"][1], tolerance)
        inner_top = section_geometry.horizontal_support(inner[0], inside["max"][1], tolerance)
        bottom_wall = section_geometry.parallel_gap(bottom, inner_bottom)
        top_wall = section_geometry.parallel_gap(inner_top, top)
        wall = inside["min"][1] - outside["min"][1]
        upper_wall = outside["max"][1] - inside["max"][1]
        if min(wall, upper_wall) <= tolerance or abs(wall - upper_wall) > tolerance:
            raise section_geometry.SectionError("当前槽口要求上下基准壁具有相同的有效壁厚")
        if not outside["min"][0] < inside["min"][0] < inside["max"][0] < outside["max"][0]:
            raise section_geometry.SectionError("内轮廓必须位于外轮廓范围内")
        data = {"wallThickness": wall, "bottomFace": bottom, "innerBottomFace": inner_bottom,
                "topFace": top, "innerTopFace": inner_top,
                "bottomWallSpan": bottom_wall["range"], "topWallSpan": top_wall["range"],
                "bounds": {"min": [context["bounds"]["min"][0], *outside["min"]],
                           "max": [context["bounds"]["max"][0], *outside["max"]]}}
        return {"applicable": True, "reason": "", "data": data, "derivedParameters": {"wallThickness": wall}}
    except section_geometry.SectionError as error:
        return {"applicable": False, "reason": str(error), "data": {}}




def _number(value, name):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError(f"{name} 必须是有限数值")
    return float(value)


def _rounded_bottom_rectangle(length, height, radius, bottom):
    """Release opening whose bottom edge is the original V apex."""
    length = _number(length, "释放孔长度")
    height = _number(height, "释放孔高度")
    radius = _number(radius, "释放孔圆角 R")
    if length <= 0 or height <= 0:
        raise ValueError("释放孔长度和高度必须大于 0")
    if radius < 0 or radius > min(length, height) / 2:
        raise ValueError("释放孔圆角须介于 0 和短边的一半之间")
    left, right = -length / 2, length / 2
    top = bottom + height
    if radius <= 1e-9:
        points = [[left, top], [right, top], [right, bottom], [left, bottom]]
        return {"kind": "path", "segments": [
            {"kind": "line", "start": a, "end": b}
            for a, b in zip(points, points[1:] + points[:1])
        ]}
    diagonal = radius / math.sqrt(2)
    br_center = [right - radius, bottom + radius]
    bl_center = [left + radius, bottom + radius]
    tr_center = [right - radius, top - radius]
    tl_center = [left + radius, top - radius]
    segments = [
        {"kind": "line", "start": [left + radius, bottom], "end": [right - radius, bottom]},
        {"kind": "arc", "start": [right - radius, bottom],
         "middle": [br_center[0] + diagonal, br_center[1] - diagonal],
         "end": [right, bottom + radius]},
        {"kind": "line", "start": [right, bottom + radius], "end": [right, top - radius]},
        {"kind": "arc", "start": [right, top - radius],
         "middle": [tr_center[0] + diagonal, tr_center[1] + diagonal],
         "end": [right - radius, top]},
        {"kind": "line", "start": [right - radius, top], "end": [left + radius, top]},
        {"kind": "arc", "start": [left + radius, top],
         "middle": [tl_center[0] - diagonal, tl_center[1] + diagonal],
         "end": [left, top - radius]},
        {"kind": "line", "start": [left, top - radius], "end": [left, bottom + radius]},
        {"kind": "arc", "start": [left, bottom + radius],
         "middle": [bl_center[0] - diagonal, bl_center[1] - diagonal],
         "end": [left + radius, bottom]},
    ]
    return {"kind": "path", "segments": [segment for segment in segments
            if segment["kind"] != "line" or math.dist(segment["start"], segment["end"]) > 1e-9]}


def _rounded_v_points(left_angle, right_angle, radius, bottom, top):
    """Preserve the original root-relief arcs and the short root between them."""
    radius = _number(radius, "圆角半径 R")
    if radius < 0:
        raise ValueError("圆角半径不能小于 0")
    left = math.radians(left_angle)
    right = math.radians(right_angle)
    sin_left, sin_right = math.sin(left), math.sin(right)
    cos_left, cos_right = math.cos(left), math.cos(right)
    tan_left, tan_right = math.tan(left), math.tan(right)
    if radius <= 1e-9:
        return None

    # Root clearance, not a tangent-development or formed-bend construction.
    # Keep the original side arcs and their short connecting root unchanged.
    left_center_x = -radius * tan_left
    right_center_x = radius * tan_right
    left_slope = [left_center_x + radius * sin_left, bottom + radius * (1 - cos_left)]
    right_slope = [right_center_x - radius * sin_right, bottom + radius * (1 - cos_right)]
    left_bottom = [left_center_x, bottom]
    right_bottom = [right_center_x, bottom]
    if max(left_slope[1], right_slope[1]) >= top:
        raise ValueError("圆角半径过大，请减小圆角半径")
    left_top = [left_slope[0] - (top - left_slope[1]) * tan_left, top]
    right_top = [right_slope[0] + (top - right_slope[1]) * tan_right, top]
    left_middle = [left_center_x + radius * math.sin(left / 2),
                   bottom + radius * (1 - math.cos(left / 2))]
    right_middle = [right_center_x - radius * math.sin(right / 2),
                    bottom + radius * (1 - math.cos(right / 2))]
    return (left_slope, right_slope, left_bottom, right_bottom,
            left_top, right_top, left_middle, right_middle)


def _line(start, end):
    return {"kind": "line", "start": list(start), "end": list(end)}


def _arc(start, middle, end):
    return {"kind": "arc", "start": list(start), "middle": list(middle), "end": list(end)}


def _root_height(p, lo, wall):
    reference = p.get("bottomReference", "outer")
    if reference not in ("outer", "inner"):
        raise ValueError("留底基准无效")
    if reference == "inner" and wall <= 0:
        raise ValueError("内底面基准需要目标管型提供有效壁厚")
    leave_bottom = _number(p["leaveBottom"], "留底高度")
    if leave_bottom < 0:
        raise ValueError("留底高度不能小于 0")
    return lo[2] + leave_bottom + (wall if reference == "inner" else 0), reference


def _circle_wrap_geometry(p, lo, hi, wall):
    """V groove inverse-developed around a final circular object."""
    diameter = _number(p["enclosedDiameter"], "包围对象直径")
    clearance = _number(p["radialClearance"], "径向装配间隙")
    beta = math.radians(_number(p["angle"], "折弯角"))
    k = _number(p["kFactor"], "K 因子")
    if diameter <= 0 or clearance < 0 or not 0 < beta < math.pi or not 0 <= k <= 1:
        raise ValueError("包围直径、间隙、折弯角或 K 因子无效")
    radius = diameter / 2.0 + clearance
    alpha = beta / 2.0
    gamma = math.pi - alpha
    root, root_reference = _root_height(p, lo, wall)
    physical_top = hi[2]
    minimum_height = radius * (1.0 + math.cos(alpha))
    if root + minimum_height >= physical_top - 1e-6:
        raise ValueError("包围圆 V 所需高度超出当前截面，请减小包围直径、间隙或槽根留量")

    layer_offset = k * wall
    developed = (radius + layer_offset) * beta
    # The cutter already overlaps the tube below the outer face.  Keeping its
    # profile on the physical face avoids showing a false 1 mm protrusion in
    # the resource preview while retaining a valid subtracting volume.
    top = physical_top
    left_bottom = [-developed / 2.0, root]
    right_bottom = [developed / 2.0, root]

    def left_point(u):
        return [-developed / 2.0 - radius * math.sin(u),
                root + radius * (1.0 - math.cos(u))]

    def right_point(u):
        return [developed / 2.0 + radius * math.sin(u),
                root + radius * (1.0 - math.cos(u))]

    left_tip, right_tip = left_point(gamma), right_point(gamma)
    left_top = [-developed / 2.0 - (top - root - radius) * math.tan(alpha), top]
    right_top = [developed / 2.0 + (top - root - radius) * math.tan(alpha), top]
    contour = {"kind": "path", "segments": [
        _line(left_top, left_tip),
        _arc(left_tip, left_point(gamma / 2.0), left_bottom),
        _line(left_bottom, right_bottom),
        _arc(right_bottom, right_point(gamma / 2.0), right_tip),
        _line(right_tip, right_top),
        _line(right_top, left_top),
    ]}
    span = hi[1] - lo[1]
    nodes = [
        {"key": "circle-wrap-profile", "operator": "profile2d", "arguments": {
            "placement": {"origin": [0, hi[1], 0], "xAxis": [1, 0, 0], "yAxis": [0, 0, 1]},
            "contours": [contour]}},
        {"key": "circle-wrap", "operator": "extrude", "inputs": ["circle-wrap-profile"],
         "arguments": {"vector": [0, -span, 0]}},
    ]
    return {"mode": "solid", "coordinateSpace": "part-local", "outputKey": "circle-wrap",
            "calculation": {
                "geometryMode": "CircleWrap", "bendAngle": math.degrees(beta),
                "finalIncludedAngle": 180.0 - math.degrees(beta),
                "enclosedDiameter": diameter, "radialClearance": clearance,
                "targetRadius": radius, "retainedArcAngle": math.degrees(gamma),
                "effectiveLayerOffset": layer_offset, "developedBandLength": developed,
                "minimumRequiredHeight": minimum_height,
                "rootReference": root_reference, "formingValidation": "reference-closure-only",
                "calibrationRequired": True,
            },
            "model": {"schema": "icax.neutral-model", "schemaVersion": 1,
                      "template": {"id": "v-notch-sharp", "version": "4.0.0",
                                   "packageDigest": "self-contained"},
                      "geometry": nodes}}


def generate(p, context):
    # Placement (station, rotation and array) belongs to the common layer.
    # This package only sizes a cutter around the current tube section.
    bounds = context.get("analysis", {}).get("bounds")
    if not isinstance(bounds, dict):
        raise ValueError("V 槽缺少主管截面范围")
    lo, hi = bounds.get("min"), bounds.get("max")
    if (not isinstance(lo, (list, tuple)) or not isinstance(hi, (list, tuple))
            or len(lo) != 3 or len(hi) != 3):
        raise ValueError("V 槽主管截面范围无效")
    if any(isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value)
           for value in [*lo, *hi]):
        raise ValueError("V 槽主管截面范围无效")
    section_width = hi[1] - lo[1]
    section_height = hi[2] - lo[2]
    if section_width <= 0 or section_height <= 0:
        raise ValueError("V 槽主管截面范围无效")

    segmented = p.get("segmentedBend", False)
    if not isinstance(segmented, bool):
        raise ValueError("分段折弯必须是开关")
    strategy = "sharp" if segmented else p["bottomStrategy"]
    if strategy not in ("sharp", "flat", "rounded", "relief"):
        raise ValueError("V 槽形式无效")
    wall = _number(p.get("wallThickness", 0), "主管实际壁厚")
    if wall < 0 or wall >= min(section_width, section_height) / 2:
        raise ValueError("主管实际壁厚须小于截面短边的一半")
    relief_shape = p.get("reliefShape", "roundedRectangle")
    if strategy == "relief" and relief_shape == "circleWrap":
        return _circle_wrap_geometry(p, lo, hi, wall)

    angle = _number(p["angle"], "V 槽夹角")
    total_angle=angle
    count=1;pitch=0;chord_error=0
    if segmented:
        count=p.get('segmentCount',6)
        radius=_number(p.get('centerlineRadius',50),'目标中心线半径')
        error_limit=_number(p.get('maximumChordError',0),'允许弓高误差')
        if error_limit<0:raise ValueError("允许弓高误差不能为负")
        if error_limit>0:
            if radius<=0:raise ValueError("中心线半径须大于 0")
            step=2*math.acos(max(-1,min(1,1-error_limit/radius)))
            if step<=1e-12:raise ValueError("弓高误差过小，无法在64槽内达到")
            count=max(2,math.ceil(math.radians(angle)/step))
        if isinstance(count,bool) or not isinstance(count,(int,float)) or int(count)!=count or not 2<=count<=64:
            raise ValueError("分段槽数须为 2 至 64 的整数")
        count=int(count)
        if radius<=0 or not 0<angle<180:raise ValueError("中心线半径须大于 0，总折弯角须介于 0 与 180°")
        angle/=count
        pitch=2*radius*math.sin(math.radians(angle)/2)
        chord_error=radius*(1-math.cos(math.radians(angle)/2))
    asymmetric = p["asymmetric"] and not segmented
    if not isinstance(asymmetric, bool):
        raise ValueError("非对称必须是开关")
    if asymmetric:
        left_angle = _number(p["leftAngle"], "左侧角度")
        right_angle = _number(p["rightAngle"], "右侧角度")
    else:
        left_angle = right_angle = angle / 2
    if left_angle <= 0 or right_angle <= 0 or left_angle >= 90 or right_angle >= 90:
        raise ValueError("V 槽两侧角度必须大于 0 且小于 90°")
    if left_angle + right_angle >= 179.999:
        raise ValueError("V 槽两侧角度之和必须小于 180°")
    left_tan = math.tan(math.radians(left_angle))
    right_tan = math.tan(math.radians(right_angle))

    half_height = hi[2]
    reference = p.get("bottomReference", "outer")
    if reference not in ("outer", "inner"):
        raise ValueError("留底基准无效")
    if reference == "inner" and wall <= 0:
        raise ValueError("内底面基准需要填写主管实际壁厚")
    leave_bottom = _number(p["leaveBottom"], "留底高度")
    if reference == "inner":
        leave_bottom += wall
    if leave_bottom < 0 or leave_bottom >= section_height:
        raise ValueError("留底高度须大于等于 0 且小于主管截面高度")
    sharp_bottom = lo[2] + leave_bottom
    top = half_height

    flat_width = 0.0
    bottom = sharp_bottom
    if strategy == "flat":
        flat_width = _number(p["flatWidth"], "平底宽度")
        if flat_width < 0:
            raise ValueError("平底宽度不能小于 0")

    male_female = p["maleFemale"] and not segmented
    if not isinstance(male_female, bool):
        raise ValueError("斜切公母必须是开关")
    male_size = _number(p["maleFemaleSize"], "公母尺寸") if male_female else 0.0
    if male_size < 0:
        raise ValueError("公母尺寸不能小于 0")
    if male_female:
        if male_size == 0:
            if wall <= 0:
                raise ValueError("自动公母尺寸需要填写主管实际壁厚")
            male_size = wall
        if wall > 0 and male_size < wall:
            raise ValueError("公母尺寸不能小于主管实际壁厚")

    contour = None
    round_radius = 0.0
    if strategy == "rounded":
        round_radius = _number(p["roundRadius"], "圆角半径 R")
        if round_radius < 0:
            raise ValueError("圆角半径不能小于 0")
        rounded = _rounded_v_points(left_angle, right_angle, round_radius, bottom, half_height)
        if rounded is not None:
            (left_slope, right_slope, left_bottom, right_bottom,
             left_top, right_top, left_middle, right_middle) = rounded
            min_side_depth = min(half_height - left_slope[1], half_height - right_slope[1])
            if male_female and male_size >= min_side_depth:
                raise ValueError("公母尺寸须小于圆角 V 槽斜边高度")
            if not male_female or male_size <= 1e-9:
                contour = {"kind": "path", "segments": [
                    {"kind": "line", "start": left_top, "end": right_top},
                    {"kind": "line", "start": right_top, "end": right_slope},
                    {"kind": "arc", "start": right_slope, "middle": right_middle, "end": right_bottom},
                    {"kind": "line", "start": right_bottom, "end": left_bottom},
                    {"kind": "arc", "start": left_bottom, "middle": left_middle, "end": left_slope},
                    {"kind": "line", "start": left_slope, "end": left_top},
                ]}
            else:
                transition = half_height - male_size
                left_transition = [left_slope[0] - (transition - left_slope[1]) * left_tan, transition]
                right_transition = [right_slope[0] + (transition - right_slope[1]) * right_tan, transition]
                contour = {"kind": "path", "segments": [
                    {"kind": "line", "start": left_transition, "end": [left_transition[0], top]},
                    {"kind": "line", "start": [left_transition[0], top], "end": [right_transition[0] + male_size, top]},
                    {"kind": "line", "start": [right_transition[0] + male_size, top], "end": [right_transition[0] + male_size, transition]},
                    {"kind": "line", "start": [right_transition[0] + male_size, transition], "end": right_transition},
                    {"kind": "line", "start": right_transition, "end": right_slope},
                    {"kind": "arc", "start": right_slope, "middle": right_middle, "end": right_bottom},
                    {"kind": "line", "start": right_bottom, "end": left_bottom},
                    {"kind": "arc", "start": left_bottom, "middle": left_middle, "end": left_slope},
                    {"kind": "line", "start": left_slope, "end": left_transition},
                ]}

    if contour is None:
        depth = top - bottom
        left_bottom = -flat_width / 2 if flat_width else 0.0
        right_bottom = flat_width / 2 if flat_width else 0.0
        left_top = left_bottom - depth * left_tan
        right_top = right_bottom + depth * right_tan
        if male_female and male_size >= half_height - bottom:
            raise ValueError("公母尺寸须小于 V 槽斜边高度")
        if not male_female or male_size <= 1e-9:
            points = [[left_top, top], [left_bottom, bottom]]
            if flat_width:
                points.append([right_bottom, bottom])
            points.append([right_top, top])
            contour = {"kind": "path", "segments": [
                {"kind": "line", "start": a, "end": b}
                for a, b in zip(points, points[1:] + points[:1])
            ]}
        else:
            transition = half_height - male_size
            left_transition = left_bottom - (transition - bottom) * left_tan
            right_transition = right_bottom + (transition - bottom) * right_tan
            points = [
                [left_transition, transition], [left_transition, top],
                [right_transition + male_size, top], [right_transition + male_size, transition],
                [right_transition, transition], [right_bottom, bottom],
            ]
            if flat_width:
                points.append([left_bottom, bottom])
            contour = {"kind": "path", "segments": [
                {"kind": "line", "start": a, "end": b}
                for a, b in zip(points, points[1:] + points[:1])
            ]}

    compensate = p.get("bendCompensation", False)
    if not isinstance(compensate, bool):
        raise ValueError("K 因子展开补偿必须是开关")
    if compensate and strategy == "rounded":
        if strategy != "rounded" or round_radius <= 0:
            raise ValueError("K 因子展开补偿需要圆角策略及大于 0 的刀口圆角")
        if wall <= 0:
            raise ValueError("K 因子展开补偿需要填写主管实际壁厚")
        k = _number(p.get("kFactor", 0.62), "K 因子")
        if not 0 <= k <= 1:
            raise ValueError("K 因子须介于 0 和 1 之间")
        # Delta L = K*T*theta (theta in radians). Split around the station:
        # Translate the existing root-relief arcs; do not redefine their shape.
        half_allowance = k * wall * math.radians(left_angle + right_angle) / 2
        for segment in contour["segments"]:
            for key in ("start", "middle", "end"):
                if key in segment:
                    x, z = segment[key]
                    segment[key] = [x + (half_allowance if x > 0 else -half_allowance), z]

    # Tool solids are also shown directly in the resource preview.  Keep the
    # transverse extent on the measured tube faces instead of using the old
    # one-millimetre boolean overshoot on both sides.
    span = section_width
    origin = [0, hi[1], 0]
    nodes = []

    def prism(key, shape, cut_depth=0, side="positive", transverse=None):
        if cut_depth < 0 or cut_depth > section_width:
            raise ValueError("释放孔切深须介于 0 和主管宽度之间（0 贯穿）")
        if side not in ("positive", "negative"):
            raise ValueError("释放孔进刀侧无效")
        local_origin = list(origin)
        vector = [0, -span, 0]
        if cut_depth > 0:
            direction = 1 if side == "positive" else -1
            local_origin[1] = (hi[1] if direction > 0 else lo[1])
            vector = [0, -direction * cut_depth, 0]
        if transverse is not None:
            local_origin[1] = transverse[1]
            vector = [0, transverse[0]-transverse[1], 0]
        nodes.append({"key": key + "-profile", "operator": "profile2d", "arguments": {"placement": {
            "origin": local_origin, "xAxis": [1, 0, 0], "yAxis": [0, 0, 1]}, "contours": [shape]}})
        nodes.append({"key": key, "operator": "extrude", "inputs": [key + "-profile"], "arguments": {
            "vector": vector}})

    prism("notch", contour)
    cutters = ["notch"]
    root_pattern = p.get("rootSlotPattern", False) and not segmented
    if not isinstance(root_pattern,bool):raise ValueError("根部三槽必须是开关")
    if root_pattern:
        interval=context.get("analysis",{}).get("bottomWallSpan")
        if not isinstance(interval,(list,tuple)) or len(interval)!=2:
            raise ValueError("根部释放槽需要从截面测得的平直铰链区间")
        a,b=interval;available=b-a
        mode=p.get("rootPatternMode","triple")
        kerf=_number(p.get("rootKerf",0),"桥宽核算割缝")
        minimum=_number(p.get("minimumBridge",1),"最小桥宽")
        if kerf<0 or minimum<=0:
            raise ValueError("最小桥宽须大于 0，割缝不能为负")
        if mode=="triple":
            lc,ls,wc,ws=[_number(p.get(key,default),key) for key,default in
                (("centerSlotLength",6),("sideSlotLength",3),("centerSlotWidth",1),("sideSlotWidth",1))]
            if min(lc,ls,wc,ws)<=0:
                raise ValueError("释放槽尺寸必须大于 0")
            if min(wc,ws)<=kerf:raise ValueError("释放槽宽必须大于割缝")
            if (available-lc-2*ls)/2-kerf < minimum:
                raise ValueError("根部三槽扣除割缝后的剩余桥宽不足")
            mid=(a+b)/2
            slots=(("root-center",wc,(mid-lc/2,mid+lc/2)),
                   ("root-left",ws,(a,a+ls)),("root-right",ws,(b-ls,b)))
        elif mode=="multi":
            bridge_count=p.get("bridgeCount",2)
            if isinstance(bridge_count,bool) or int(bridge_count)!=bridge_count or not 1<=bridge_count<=16:
                raise ValueError("连接桥数量必须为 1 至 16 的整数")
            bridge_count=int(bridge_count)
            bridge_width=_number(p.get("bridgeWidth",2),"连接桥名义宽度")
            axial_width=_number(p.get("multiSlotWidth",1),"多桥槽轴向宽度")
            if min(bridge_width,axial_width)<=0 or bridge_width-kerf<minimum:
                raise ValueError("多桥尺寸不足：连接桥扣除割缝后必须满足最小桥宽")
            slot_span=(available-bridge_count*bridge_width)/(bridge_count+1)
            if slot_span<=context.get("analysis",{}).get("tolerance",1e-6):
                raise ValueError("连接桥总宽度超过可用铰链区间")
            cursor=a;built=[]
            for index in range(bridge_count+1):
                limits=(cursor,cursor+slot_span)
                built.append((f"root-multi-{index}",axial_width,limits))
                cursor=limits[1]+(bridge_width if index<bridge_count else 0)
            slots=tuple(built)
        else:
            raise ValueError("根部释放槽布置无效")
        for key,width,limits in slots:
            shape=_rounded_bottom_rectangle(width,bottom-lo[2],0,lo[2])
            prism(key,shape,transverse=limits)
            cutters.append(key)
    if strategy == "relief":
        relief_kind=p.get('reliefShape','roundedRectangle')
        length=_number(p['reliefLength'],'释放孔长度')
        height=_number(p['reliefHeight'],'释放孔高度') if relief_kind!='circle' else length
        relief_radius=_number(p['reliefRadius'],'释放孔圆角') if relief_kind=='roundedRectangle' else 0
        if relief_kind=='circle':height=length;relief_radius=length/2
        elif relief_kind=='capsule':relief_radius=min(length,height)/2
        elif relief_kind!='roundedRectangle':raise ValueError("释放孔形状无效")
        if sharp_bottom + height >= half_height:
            raise ValueError("释放孔高度须小于可切除高度")
        relief = _rounded_bottom_rectangle(
            length, height, relief_radius, sharp_bottom)
        prism("relief", relief, _number(p.get("reliefDepth", 0), "释放孔切深"), p.get("reliefSide", "positive"))
        cutters.append("relief")

    output = "notch"
    if len(cutters) > 1:
        nodes.append({"key": "tool", "operator": "boolean", "inputs": cutters, "arguments": {"operation": "union"}})
        output = "tool"
    if segmented:
        opening=2*(half_height-bottom)*math.tan(math.radians(angle)/2)
        if pitch<=opening+1e-6:raise ValueError("槽距不大于单槽开口，分段槽会重叠；请增大目标半径")
        originals=nodes;nodes=[];outputs=[]
        for i in range(count):
            offset=(i-(count-1)/2)*pitch;prefix=f'segment-{i}-'
            for original in originals:
                node=copy.deepcopy(original);node['key']=prefix+node['key']
                if 'inputs' in node:node['inputs']=[prefix+k for k in node['inputs']]
                if node['operator']=='profile2d':node['arguments']['placement']['origin'][0]+=offset
                nodes.append(node)
            outputs.append(prefix+output)
        nodes.append({'key':'segmented-tool','operator':'compound','inputs':outputs,'arguments':{}})
        output='segmented-tool'
    return {"mode": "solid", "coordinateSpace": "part-local", "outputKey": output,
        "calculation":{"bendAngle":total_angle if segmented else left_angle+right_angle,
            "finalIncludedAngle":180-(total_angle if segmented else left_angle+right_angle),
            "singleNotchAngle":left_angle+right_angle,"segmentCount":count,"chordPitch":pitch,
            "chordError":chord_error,"rootReference":reference,"hingeThickness":min(wall,leave_bottom),
            "cutSurfaceMode":"FixedPlane","formingValidation":"not-performed"},"model": {
        "schema": "icax.neutral-model", "schemaVersion": 1,
        "template": {"id": "v-notch-sharp", "version": "4.0.0", "packageDigest": "self-contained"},
        "geometry": nodes}}
