import math

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
        if parameters.get("arcDefinition")=="side90":
            for loop in loops:
                lines=[e for e in loop['edges'] if e['kind']=='line']
                horizontal=sum(abs(e['start'][1]-e['end'][1])<=tolerance for e in lines)
                vertical=sum(abs(e['start'][0]-e['end'][0])<=tolerance for e in lines)
                if horizontal!=2 or vertical!=2 or len(lines)!=4:
                    raise section_geometry.SectionError("侧90°展开槽需要具有两对正交平直壁的矩形闭口截面")
                for edge in loop['edges']:
                    if edge['kind']=='line':continue
                    if edge['kind']!='circleArc' or abs(abs(edge['last']-edge['first'])-math.pi/2)*edge['radius']>tolerance:
                        raise section_geometry.SectionError("侧90°展开槽的截面角部须为四分之一圆弧")
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


def _line(start, end):
    return {"kind": "line", "start": list(start), "end": list(end)}


def _arc(start, middle, end):
    return {"kind": "arc", "start": list(start), "middle": list(middle), "end": list(end)}


def _calculate_side_arc_non_arc_run(cut_depth, bend_degrees):
    angle = math.radians(bend_degrees)
    tangent = math.tan(angle)
    if abs(tangent) <= 1e-12:
        raise ValueError("边弧槽角度退化，无法计算直边")
    run = cut_depth / tangent
    return 0.0 if abs(run) <= 1e-6 else run


def _resolve_side_arc_radius(context, bottom_y, bend_degrees):
    physical_cut_height = abs(context["physical_top_y"] - bottom_y)
    sweep = math.radians(bend_degrees)
    return physical_cut_height / (2.0 * math.sin(sweep / 2.0) ** 2)


def side_arc_v_groove_cutter(context, keep_left_arc, bend_degrees=90):
    """Build a box cutter with one side removed by a cylindrical cutter.

    The normal edge-arc groove is not a closed, over-extended arc profile.
    It is the same solid construction used by the manufacturing operation:
    a rectangular (or angled-V) stock prism minus a cylinder.  The cylinder
    is tangent to the bottom edge, so only the required circular side wall
    remains in the resulting cutter.
    """
    arc_sweep = math.radians(bend_degrees)
    physical_top_y = context["physical_top_y"]
    bottom_y = context["bottom_y"]
    radius = _resolve_side_arc_radius(context, bottom_y, bend_degrees)
    arc_dx = radius * math.sin(arc_sweep)
    allowance = context.get("bend_allowance", 0.0)
    arc_length = radius * arc_sweep + allowance
    center_y = bottom_y + radius
    center_x = -allowance / 2 if keep_left_arc else allowance / 2
    physical_run = _calculate_side_arc_non_arc_run(abs(physical_top_y - bottom_y), bend_degrees)
    # At height z above the root, the remaining cutter width is
    # L + z*cot(theta) - sqrt(2*R*z-z*z). Its minimum over [0,H]
    # occurs at H=R*(1-cos(theta)). A positive base area alone is
    # insufficient: obtuse angles can make the straight flank cross the arc.
    top_gap = arc_length + physical_run - arc_dx
    if top_gap <= 1e-6:
        raise ValueError("边弧槽直边与圆弧相交或相切，顶部槽宽不足；请减小角度或重新设置补偿")

    if keep_left_arc:
        left_bottom = [center_x - arc_dx, bottom_y]
        non_arc_bottom = [left_bottom[0] + arc_length, bottom_y]
        right_cut_top = [non_arc_bottom[0] + physical_run, physical_top_y]
        base = {"kind": "path", "segments": [
            _line([left_bottom[0], physical_top_y], right_cut_top),
            _line(right_cut_top, non_arc_bottom),
            _line(non_arc_bottom, left_bottom),
            _line(left_bottom, [left_bottom[0], physical_top_y]),
        ]}
        return {"base": base, "cylinder": {"center": [left_bottom[0], center_y], "radius": radius}}

    right_bottom = [center_x + arc_dx, bottom_y]
    non_arc_bottom = [right_bottom[0] - arc_length, bottom_y]
    left_cut_top = [non_arc_bottom[0] - physical_run, physical_top_y]
    base = {"kind": "path", "segments": [
        _line(left_cut_top, [right_bottom[0], physical_top_y]),
        _line([right_bottom[0], physical_top_y], right_bottom),
        _line(right_bottom, non_arc_bottom),
        _line(non_arc_bottom, left_cut_top),
    ]}
    return {"base": base, "cylinder": {"center": [right_bottom[0], center_y], "radius": radius}}


def generate(p, context):
    bounds = context.get("analysis", {}).get("bounds")
    if not isinstance(bounds, dict):
        raise ValueError("边弧槽缺少主管截面范围")
    lo, hi = bounds.get("min"), bounds.get("max")
    if (not isinstance(lo, (list, tuple)) or not isinstance(hi, (list, tuple)) or len(lo) != 3 or len(hi) != 3
            or any(isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value)
                   for value in [*lo, *hi])):
        raise ValueError("边弧槽主管截面范围无效")
    section_width, section_height = hi[1] - lo[1], hi[2] - lo[2]
    if section_width <= 0 or section_height <= 0:
        raise ValueError("边弧槽主管截面范围无效")
    bridge = _number(p["bridge"], "底部保留厚度")
    wall = _number(p.get("wallThickness", 0), "主管实际壁厚")
    if wall < 0 or wall >= min(section_width, section_height) / 2:
        raise ValueError("主管实际壁厚须小于截面短边的一半")
    reference = p.get("bottomReference", "outer")
    if reference not in ("outer", "inner"):
        raise ValueError("留底基准无效")
    if reference == "inner":
        if wall <= 0:
            raise ValueError("内底面基准需要填写主管实际壁厚")
        bridge += wall
    angle = _number(p["angle"], "V 槽夹角")
    if bridge <= 0 or bridge >= section_height:
        raise ValueError("底部保留厚度须大于 0 且小于主管截面高度")
    if angle <= 0 or angle >= 180:
        raise ValueError("V 槽夹角必须大于 0 且小于 180°")
    if not isinstance(p["leftArc"], bool):
        raise ValueError("左圆弧必须是开关")
    half_height = hi[2]
    bottom_y, physical_top_y = lo[2] + bridge, half_height
    shape_context = {"bottom_y": bottom_y, "physical_top_y": physical_top_y}
    compensate = p.get("bendCompensation", False)
    default_k = p.get("useDefaultKFactor", True)
    if not isinstance(compensate, bool) or not isinstance(default_k, bool):
        raise ValueError("K 因子选项必须是开关")
    if compensate:
        if wall <= 0:
            raise ValueError("K 因子展开补偿需要填写主管实际壁厚")
        k = 0.62 if default_k else _number(p.get("kFactor", 0.62), "K 因子")
        if not 0 <= k <= 1:
            raise ValueError("K 因子须介于 0 和 1 之间")
        shape_context["bend_allowance"] = k * wall * math.radians(angle)
    span, origin, nodes = section_width + 2.0, [0.0, hi[1] + 1.0, 0.0], []

    def prism(key, shape, center=(0.0, 0.0), cut_depth=0, side="positive"):
        if cut_depth < 0 or cut_depth > section_width:
            raise ValueError("释放孔切深须介于 0 和主管宽度之间（0 贯穿）")
        if side not in ("positive", "negative"):
            raise ValueError("释放孔进刀侧无效")
        start_y, vector_y = origin[1], -span
        if cut_depth > 0:
            direction = 1 if side == "positive" else -1
            start_y = (hi[1] + 1 if direction > 0 else lo[1] - 1)
            vector_y = -direction * (cut_depth + 1)
        nodes.append({"key": key + "-profile", "operator": "profile2d", "arguments": {
            "placement": {"origin": [center[0], start_y, center[1]], "xAxis": [1, 0, 0], "yAxis": [0, 0, 1]},
            "contours": [shape]}})
        nodes.append({"key": key, "operator": "extrude", "inputs": [key + "-profile"],
                      "arguments": {"vector": [0, vector_y, 0]}})

    definition=p.get("arcDefinition","legacy")
    if definition not in ("legacy","side90"):raise ValueError("边弧几何定义无效")
    if definition=="side90":
        if abs(angle-90)>1e-8:raise ValueError("侧 90°槽的折弯开口角固定为 90°")
        radius=physical_top_y-bottom_y
        # Engineering development model, not a claim of validated plastic forming.
        allowance=shape_context.get("bend_allowance",0)*min(wall,bridge)/wall if wall>0 else 0
        length=math.pi*radius/2+allowance
        center=-length/2
        base=[center,bottom_y];arc_top=[center-radius,physical_top_y]
        mid=[center-radius/math.sqrt(2),bottom_y+radius*(1-1/math.sqrt(2))]
        right=length/2
        edges=[_line([center-radius,physical_top_y+1],[right,physical_top_y+1]),
               _line([right,physical_top_y+1],[right,bottom_y]),_line([right,bottom_y],base),
               _arc(base,mid,arc_top),_line(arc_top,[center-radius,physical_top_y+1])]
        if not p["leftArc"]:
            for edge in edges:
                for key in ("start","middle","end"):
                    if key in edge:edge[key][0]=-edge[key][0]
        prism("notch",{"kind":"path","segments":edges})
    else:
        side_cutter = side_arc_v_groove_cutter(shape_context, p["leftArc"], angle)
        prism("notch-base", side_cutter["base"])
        cylinder = {"kind": "path", "segments": [
            _arc([-side_cutter["cylinder"]["radius"], 0], [0, -side_cutter["cylinder"]["radius"]],
                 [side_cutter["cylinder"]["radius"], 0]),
            _arc([side_cutter["cylinder"]["radius"], 0], [0, side_cutter["cylinder"]["radius"]],
                 [-side_cutter["cylinder"]["radius"], 0]),
        ]}
        prism("arc-cylinder", cylinder, side_cutter["cylinder"]["center"])
        nodes.append({"key": "notch", "operator": "boolean", "inputs": ["notch-base", "arc-cylinder"],
                      "arguments": {"operation": "subtract"}})
    cutters = ["notch"]
    relief_diameter = _number(p["reliefDiameter"], "附加释放孔直径")
    relief_lift = _number(p["reliefLift"], "附加释放孔中心上移")
    if relief_diameter < 0 or relief_lift < 0:
        raise ValueError("附加释放孔参数不能小于 0")
    if relief_diameter > 0:
        radius = relief_diameter / 2.0
        if bottom_y + relief_lift + radius >= physical_top_y:
            raise ValueError("释放孔超出主管顶部，请减小直径或上移量")
        if bottom_y + relief_lift - radius < lo[2] and not p["bottomCut"]:
            raise ValueError("释放孔将切穿底面，请减小直径、上移孔中心或明确开启底部切除")
        circle = {"kind": "path", "segments": [
            _arc([-radius, 0], [0, -radius], [radius, 0]),
            _arc([radius, 0], [0, radius], [-radius, 0]),
        ]}
        prism("relief", circle, (0.0, bottom_y + relief_lift),
              _number(p.get("reliefDepth", 0), "释放孔切深"), p.get("reliefSide", "positive"))
        cutters.append("relief")
    if not isinstance(p["bottomCut"], bool):
        raise ValueError("底部切除必须是开关")
    if p["bottomCut"]:
        width = _number(p["bottomCutWidth"], "底部切除宽度")
        if width <= 0:
            raise ValueError("底部切除宽度必须大于 0")
        half = width / 2.0
        points = [[-half, lo[2] - 1.0], [half, lo[2] - 1.0],
                  [half, bottom_y + 1.0], [-half, bottom_y + 1.0]]
        prism("bottom-cut", {"kind": "path", "segments": [_line(a, b) for a, b in zip(points, points[1:] + points[:1])]})
        cutters.append("bottom-cut")

    output = "notch"
    if len(cutters) > 1:
        nodes.append({"key": "tool", "operator": "boolean", "inputs": cutters,
                      "arguments": {"operation": "union"}})
        output = "tool"
    return {"mode": "solid", "coordinateSpace": "part-local", "outputKey": output,
            "calculation":{"bendAngle":angle,"finalIncludedAngle":180-angle,"arcDefinition":definition,
                "rootReference":reference,"hingeThickness":min(wall,bridge),
                "formingValidation":"not-performed","formulaSource":"engineering-derivation" if definition=='side90' else 'legacy-cutter'},
            "model": {"schema": "icax.neutral-model", "schemaVersion": 1,
                      "template": {"id": "edge-arc-groove", "version": "3.0.0",
                                   "packageDigest": "self-contained"},
                      "geometry": nodes}}
