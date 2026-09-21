"""Distributed-flexure slots. Geometry is exact; forming remains calibrated."""
import math


def analyze(parameters, section, context):
    try:
        data = section_geometry.closed_shell_metrics(
            section, context.get("placement", {}).get("rotation", 0))
        return {"applicable": True, "reason": "", "data": data,
                "derivedParameters": {"wallThickness": data["wallThickness"]}}
    except ValueError as error:
        return {"applicable": False, "reason": str(error), "data": {}}


def _number(value, name):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError(f"{name}必须是有限数值")
    return float(value)


def _line(start, end):
    return {"kind": "line", "start": list(start), "end": list(end)}


def _rect(left, bottom, right, top):
    points = [[left, bottom], [right, bottom], [right, top], [left, top]]
    return {"kind": "path", "segments": [_line(a, b) for a, b in zip(points, points[1:] + points[:1])]}


def generate(p, context):
    data = context["analysis"]
    lo, hi = data["outside"]["min"], data["outside"]["max"]
    mode = p["slitMode"]
    if mode not in ("straight", "u", "narrow"):
        raise ValueError("柔性槽类型无效")
    angle = math.radians(_number(p["angle"], "目标折弯角"))
    radius = _number(p["bendRadius"], "目标中心线半径")
    count = p["slitCount"]
    width = _number(p["slitWidth"], "割缝宽度")
    root_clearance = _number(p["rootClearance"], "槽根到内底面距离")
    minimum_land = _number(p["minimumLand"], "最小筋宽")
    u_span = _number(p["uSpan"], "U 槽开口宽度") if mode == "u" else 0.0
    if not 0 < angle < math.pi or radius <= 0 or isinstance(count, bool) or int(count) != count or not 2 <= count <= 64:
        raise ValueError("折弯角、半径或槽数无效")
    if min(width, minimum_land) <= 0 or root_clearance < 0 or (mode == "u" and u_span <= width):
        raise ValueError("割缝、筋宽、槽根距离或 U 槽宽度无效")
    count = int(count)
    flexible_length = radius * angle
    pitch = flexible_length / (count + 1)
    if pitch - width < minimum_land:
        raise ValueError("柔性槽间剩余筋宽不足，请增大半径、减少槽数或减小割缝")
    root = data["inside"]["min"][1] + root_clearance
    top = hi[1]
    if root >= hi[1] - data["tolerance"]:
        raise ValueError("柔性槽根超出主管顶部")
    if mode == "narrow" and top - root <= width / 2.0:
        raise ValueError("窄缝高度不足以形成圆头")

    nodes, cutters = [], []
    span = hi[0] - lo[0]

    def prism(key, station, contour):
        nodes.extend([
            {"key": key + "-profile", "operator": "profile2d", "arguments": {
                "placement": {"origin": [station, hi[0], 0], "xAxis": [1, 0, 0], "yAxis": [0, 0, 1]},
                "contours": [contour]}},
            {"key": key, "operator": "extrude", "inputs": [key + "-profile"],
             "arguments": {"vector": [0, -span, 0]}},
        ])
        cutters.append(key)

    for index in range(count):
        station = (index - (count - 1) / 2.0) * pitch
        if mode == "straight":
            prism(f"slit-{index}", station, _rect(-width / 2.0, root, width / 2.0, top))
        elif mode == "narrow":
            shoulder = root + width / 2.0
            contour = {"kind": "path", "segments": [
                _line([-width / 2.0, top], [width / 2.0, top]),
                _line([width / 2.0, top], [width / 2.0, shoulder]),
                {"kind": "arc", "start": [width / 2.0, shoulder], "middle": [0, root],
                 "end": [-width / 2.0, shoulder]},
                _line([-width / 2.0, shoulder], [-width / 2.0, top]),
            ]}
            prism(f"slit-{index}", station, contour)
        else:
            arm = u_span / 2.0
            prism(f"u-{index}-left", station, _rect(-arm - width / 2.0, root, -arm + width / 2.0, top))
            prism(f"u-{index}-right", station, _rect(arm - width / 2.0, root, arm + width / 2.0, top))
            prism(f"u-{index}-base", station, _rect(-arm - width / 2.0, root,
                                                    arm + width / 2.0, root + width))
    nodes.append({"key": "flexible-slots", "operator": "compound", "inputs": cutters, "arguments": {}})
    return {"mode": "solid", "coordinateSpace": "part-local", "outputKey": "flexible-slots",
            "calculation": {
                "geometryMode": {"straight": "StraightSlit", "u": "USlit", "narrow": "NarrowSlit"}[mode],
                "targetBendAngle": math.degrees(angle), "targetCenterlineRadius": radius,
                "distributedCurvature": 1.0 / radius, "flexibleLength": flexible_length,
                "slitCount": count, "slitPitch": pitch, "remainingLand": pitch - width,
                "rootReference": "inner", "formingValidation": "calibration-required",
                "warning": "目标角度来自等曲率分配，不代表未标定材料会自由弯到该角度",
            },
            "model": {"schema": "icax.neutral-model", "schemaVersion": 1,
                      "template": {"id": "flexible-slit-bend", "version": "1.1.0",
                                   "packageDigest": "self-contained"},
                      "geometry": nodes}}
