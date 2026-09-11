import math


def _number(value, name):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError(f"{name} 必须是有限数值")
    return float(value)


def _line(start, end):
    return {"kind": "line", "start": list(start), "end": list(end)}


def _arc(start, middle, end):
    return {"kind": "arc", "start": list(start), "middle": list(middle), "end": list(end)}


def _wall(context, width, height):
    values = []
    feature = context.get("feature")
    if isinstance(feature, dict) and isinstance(feature.get("section"), dict):
        values.extend(feature["section"].get(key) for key in ("wall", "thickness", "wallThickness"))
    if isinstance(context.get("section"), dict):
        values.extend(context["section"].get(key) for key in ("wall", "thickness", "wallThickness"))
    for value in values:
        if isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value) and value > 0:
            return min(float(value), min(width, height) / 2.0)
    return max(0.1, min(width, height) * 0.05)


def _calculate_side_arc_non_arc_run(cut_depth, bend_degrees):
    angle = math.radians(max(1.0, min(179.0, bend_degrees)))
    tangent = math.tan(angle)
    if abs(tangent) <= 1e-9:
        return cut_depth * 100.0
    run = cut_depth / tangent
    return 0.0 if abs(run) <= 1e-6 else run


def _resolve_side_arc_radius(context, bottom_y, bend_degrees):
    physical_cut_height = abs(context["physical_top_y"] - bottom_y)
    sweep = math.radians(max(1.0, min(179.0, bend_degrees)))
    return max(context["wall"], physical_cut_height / max(1.0 - math.cos(sweep), 0.001))


def side_arc_v_groove_cutter(context, keep_left_arc, bend_degrees=90):
    """Build a box cutter with one side removed by a cylindrical cutter.

    The normal edge-arc groove is not a closed, over-extended arc profile.
    It is the same solid construction used by the manufacturing operation:
    a rectangular (or angled-V) stock prism minus a cylinder.  The cylinder
    is tangent to the bottom edge, so only the required circular side wall
    remains in the resulting cutter.
    """
    arc_sweep = math.radians(max(1.0, min(179.0, bend_degrees)))
    physical_top_y = context["physical_top_y"]
    bottom_y = context["bottom_y"]
    radius = _resolve_side_arc_radius(context, bottom_y, bend_degrees)
    arc_dx = radius * math.sin(arc_sweep)
    arc_length = radius * arc_sweep
    center_y = bottom_y + radius
    center_x = 0.0
    physical_run = _calculate_side_arc_non_arc_run(abs(physical_top_y - bottom_y), bend_degrees)

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
        _line(right_bottom, non_arc_bottom),
        _line(non_arc_bottom, left_cut_top),
        _line([right_bottom[0], physical_top_y], right_bottom),
    ]}
    return {"base": base, "cylinder": {"center": [right_bottom[0], center_y], "radius": radius}}


def generate(p, context):
    bounds = context.get("bounds")
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
    angle = _number(p["angle"], "V 槽夹角")
    if bridge <= 0 or bridge >= section_height:
        raise ValueError("底部保留厚度须大于 0 且小于主管截面高度")
    if angle <= 0 or angle >= 180:
        raise ValueError("V 槽夹角必须大于 0 且小于 180°")
    if not isinstance(p["leftArc"], bool):
        raise ValueError("左圆弧必须是开关")
    half_height = section_height / 2.0
    bottom_y, physical_top_y = -half_height + bridge, half_height
    wall = _wall(context, section_width, section_height)
    shape_context = {"wall": wall, "bottom_y": bottom_y, "physical_top_y": physical_top_y}
    span, origin, nodes = section_width + 2.0, [0.0, (section_width + 2.0) / 2.0, 0.0], []

    def prism(key, shape, center=(0.0, 0.0)):
        nodes.append({"key": key + "-profile", "operator": "profile2d", "arguments": {
            "placement": {"origin": [center[0], origin[1], center[1]], "xAxis": [1, 0, 0], "yAxis": [0, 0, 1]},
            "contours": [shape]}})
        nodes.append({"key": key, "operator": "extrude", "inputs": [key + "-profile"],
                      "arguments": {"vector": [0, -span, 0]}})

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
        circle = {"kind": "path", "segments": [
            _arc([-radius, 0], [0, -radius], [radius, 0]),
            _arc([radius, 0], [0, radius], [-radius, 0]),
        ]}
        prism("relief", circle, (0.0, bottom_y + relief_lift))
        cutters.append("relief")
    if not isinstance(p["bottomCut"], bool):
        raise ValueError("底部切除必须是开关")
    if p["bottomCut"]:
        width = _number(p["bottomCutWidth"], "底部切除宽度")
        if width <= 0:
            raise ValueError("底部切除宽度必须大于 0")
        half = width / 2.0
        points = [[-half, -half_height - 1.0], [half, -half_height - 1.0],
                  [half, bottom_y + 1.0], [-half, bottom_y + 1.0]]
        prism("bottom-cut", {"kind": "path", "segments": [_line(a, b) for a, b in zip(points, points[1:] + points[:1])]})
        cutters.append("bottom-cut")

    output = "notch"
    if len(cutters) > 1:
        nodes.append({"key": "tool", "operator": "boolean", "inputs": cutters,
                      "arguments": {"operation": "union"}})
        output = "tool"
    return {"mode": "solid", "coordinateSpace": "part-local", "outputKey": output,
            "model": {"schema": "icax.neutral-model", "schemaVersion": 1,
                      "template": {"id": "edge-arc-groove", "version": "1.2.0",
                                   "packageDigest": "self-contained"},
                      "geometry": nodes}}
