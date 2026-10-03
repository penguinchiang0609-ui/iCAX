"""Template-owned target envelope for an embedded-arc V-notch fold.

The cutting tool supplies the actual V-base envelope and hinge-root height.
Its retained circle is a local cutting detail, not a finished bend radius.
This closed sweep shows nominal flank closure; real tongue interference and
plastic forming remain to be checked on a trial part. The true notch is shown
on the separate native-cut blank.
"""

import importlib.util
import math
from pathlib import Path

_geometry_spec = importlib.util.spec_from_file_location(
    "icax_formed_section_geometry", Path(__file__).resolve().parents[2] / "_shared" / "assembly_applicability_geometry.py")
geometry = importlib.util.module_from_spec(_geometry_spec)
_geometry_spec.loader.exec_module(geometry)


def _positive(value, label):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or value <= 0:
        raise ValueError(f"{label}必须为正数")
    return float(value)


def _section(plan):
    first, second = plan["designParts"]
    requests = (first["request"], second["request"])
    if any(request["profileRef"] != {"scope": "system", "id": "rect"} for request in requests):
        raise ValueError("嵌入圆弧折弯目标示意目前仅支持方矩管")
    section = geometry.standard_section(requests[0])
    geometry.require(section is not None, "嵌入圆弧折弯目标示意目前仅支持方矩管")
    geometry.require(geometry.sections_match(*requests), "两逻辑段必须为相同的闭口方矩管，且壁厚须小于截面半宽")
    geometry.centered_section(section, "嵌入圆弧开槽折弯")
    geometry.uniform_section(section, "嵌入圆弧开槽折弯")
    width, depth, wall = (section[key] for key in ("width", "depth", "wall"))
    return {
        "width": width, "depth": depth, "wall": wall,
        "outerRadius": section["outerRadii"][0],
        "innerRadius": section["innerRadii"][0],
        "lengthA": _positive(requests[0]["length"], "前段长度"),
        "lengthB": _positive(requests[1]["length"], "后段长度"),
    }


def _cut_layout(plan, section):
    operation = next(
        (item for item in plan["resolvedWorkflow"]["partOperations"]
         if item.get("resourceRef", {}).get("id") == "embedded-arc-notch"), None)
    if operation is None:
        raise ValueError("嵌入圆弧装配模板缺少对应的单件槽口工艺")
    tool_path = Path(__file__).resolve().parents[2] / "mold" / "embedded-arc-notch" / "tool.py"
    spec = importlib.util.spec_from_file_location("icax_embedded_arc_cut_layout", tool_path)
    if spec is None or spec.loader is None:
        raise ValueError("无法读取嵌入圆弧槽的下料规则")
    tool = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(tool)
    width, depth, wall = section["width"], section["depth"], section["wall"]
    analysis = {
        "outside": {"min": [-width / 2, -depth / 2], "max": [width / 2, depth / 2]},
        "inside": {"min": [-width / 2 + wall, -depth / 2 + wall]},
        "wallThickness": wall,
        "tolerance": 1e-6,
    }
    result = tool.generate(operation["toolParameters"], {"analysis": analysis})
    # Read the generated cutter's V-base rather than recoding its optional
    # left/right and male/female top-step dimensions in the assembly template.
    base = next((node for node in result["model"]["geometry"]
                 if node.get("key") == "v-base-profile"), None)
    if base is None:
        raise ValueError("嵌入圆弧槽未返回 V 形切口轮廓")
    xs = [float(segment[key][0])
          for contour in base["arguments"]["contours"]
          for segment in contour["segments"]
          for key in ("start", "end") if key in segment]
    left, right = min(xs), max(xs)
    if not (left < 0 < right):
        raise ValueError("嵌入圆弧槽的切口包络无效")
    calculation = result["calculation"]
    root = float(calculation["circleCenter"][1])
    if not -depth / 2 < root < depth / 2:
        raise ValueError("嵌入圆弧槽根位置超出管截面")
    angle = float(calculation["bendAngle"])
    if abs(angle - float(plan["parameters"]["angle"])) > 1e-6:
        raise ValueError("槽口开角与目标折角不一致")
    return {
        "operation": operation, "calculation": calculation,
        "leftCutReach": -left, "rightCutReach": right,
        "root": root, "angle": angle,
    }


def build_plan(plan):
    section = _section(plan)
    layout = _cut_layout(plan, section)
    left, right = layout["leftCutReach"], layout["rightCutReach"]
    length = section["lengthA"] + left + right + section["lengthB"]
    # A and B are the uncut straight stock beyond the cutter's extreme top
    # edges. This also handles the asymmetric optional male/female top step.
    station = (section["lengthA"] + left - section["lengthB"] - right) / 2
    blank = plan["manufacturingParts"][0]
    blank["request"]["length"] = length
    for feature in blank["request"]["features"]:
        if feature.get("toolRef", {}).get("id") == "embedded-arc-notch":
            feature["station"] = station
    layout["operation"]["placement"]["station"] = station
    for check in plan["resolvedWorkflow"]["checks"]:
        if check.get("id") == f"operation-span:{layout['operation']['id']}":
            minimum, maximum = station - left, station + right
            passes = minimum >= -length / 2 - 1e-6 and maximum <= length / 2 + 1e-6
            check.update({
                "minimum": minimum, "maximum": maximum, "partLength": length,
                "status": "pass" if passes else "fail", "blocking": not passes,
                "detail": f"切口范围 {minimum:.4f}～{maximum:.4f} mm，下料件半长 {length / 2:.4f} mm",
            })
    plan["previewAnnotations"].append({
        "id": "retained-local-arc-radius", "view": "blank", "kind": "value-note",
        "label": "槽内保留圆弧 R（非成形半径）",
        "value": layout["calculation"]["arcRadius"], "unit": "mm",
    })
    return plan


def _rounded_rectangle(width, depth, radius, steps=5):
    half_width, half_depth = width / 2, depth / 2
    radius = max(0.0001, min(radius, half_width, half_depth))
    corners = (
        (half_width - radius, half_depth - radius, 0),
        (-half_width + radius, half_depth - radius, 90),
        (-half_width + radius, -half_depth + radius, 180),
        (half_width - radius, -half_depth + radius, 270),
    )
    return [(cy + radius * math.cos(math.radians(start + 90 * i / steps)),
             cz + radius * math.sin(math.radians(start + 90 * i / steps)))
            for cy, cz, start in corners for i in range(steps + 1)]


def build_formed_preview(plan):
    section = _section(plan)
    layout = _cut_layout(plan, section)
    angle = math.radians(layout["angle"])
    sine, cosine = math.sin(angle), math.cos(angle)
    tangent = math.tan(angle / 2)
    root = layout["root"]
    left, right = layout["leftCutReach"], layout["rightCutReach"]
    length_a, length_b = section["lengthA"], section["lengthB"]
    outer = _rounded_rectangle(section["width"], section["depth"], section["outerRadius"])
    inner = _rounded_rectangle(section["width"] - 2 * section["wall"],
                               section["depth"] - 2 * section["wall"], section["innerRadius"])
    ring_size = len(outer)
    positions, indices = [], []

    def rotated(x, radial):
        return [x * cosine - (radial - root) * sine, 0,
                root + x * sine + (radial - root) * cosine]

    def ring(contour, station):
        first = len(positions) // 3
        for lateral, radial in contour:
            if station == 0:
                point = [-left - length_a, lateral, radial]
            elif station == 1:
                # A flank of the nominal V cut meets the opposite flank on a
                # miter plane through the actual retained-bottom hinge root.
                point = [(root - radial) * tangent, lateral, radial]
            else:
                point = rotated(right + length_b, radial)
                point[1] = lateral
            positions.extend(point)
        return first

    outer_rings = [ring(outer, station) for station in range(3)]
    inner_rings = [ring(inner, station) for station in range(3)]

    def wall_strip(near, far, inward=False):
        for index in range(ring_size):
            following = (index + 1) % ring_size
            if inward:
                indices.extend((near + index, far + following, near + following,
                                near + index, far + index, far + following))
            else:
                indices.extend((near + index, near + following, far + following,
                                near + index, far + following, far + index))

    for station in range(2):
        wall_strip(outer_rings[station], outer_rings[station + 1])
        wall_strip(inner_rings[station], inner_rings[station + 1], True)
    for index in range(ring_size):
        following = (index + 1) % ring_size
        indices.extend((outer_rings[0] + index, inner_rings[0] + index,
                        inner_rings[0] + following, outer_rings[0] + index,
                        inner_rings[0] + following, outer_rings[0] + following))
        indices.extend((outer_rings[2] + index, outer_rings[2] + following,
                        inner_rings[2] + following, outer_rings[2] + index,
                        inner_rings[2] + following, inner_rings[2] + index))

    far = rotated(right + length_b, 0)
    uncut_right = rotated(right, 0)
    anchors = {
        "straightStart": [-left - length_a, 0, 0],
        "firstUncutEdge": [-left, 0, 0],
        "hinge": [0, 0, root],
        "lastUncutEdge": uncut_right,
        "straightEnd": far,
        "planeNormal": [0, 1, 0],
    }
    return {
        "kind": 1, "positions": positions, "indices": indices,
        "metadata": {
            "previewKind": "target-shape", "approximate": True,
            "formingValidation": "not-performed",
            "bendProcess": "embedded-arc-notch", "angle": layout["angle"],
            "section": section, "hinge": anchors["hinge"],
            "centerlineCorner": [root * tangent, 0, 0],
            "arcRadius": layout["calculation"]["arcRadius"],
            "retainedArcIsLocalCut": True,
            "leftCutReach": left, "rightCutReach": right,
            "patternLength": left + right,
            "cutStation": (length_a + left - length_b - right) / 2,
            "blankLength": plan["manufacturingParts"][0]["request"]["length"],
            "contourVertexCount": ring_size, "stationCount": 3,
            "witnessNote": "保留圆弧只见于下料件；目标外形为名义槽边闭合示意，需试样核对圆弧干涉与成形。",
            "annotations": {
                "kind": "embedded-arc-target",
                "values": {"angle": layout["angle"],
                           "localArcRadius": layout["calculation"]["arcRadius"],
                           "leftCutReach": left, "rightCutReach": right,
                           "patternLength": left + right},
                "anchors": anchors,
            },
        },
    }
