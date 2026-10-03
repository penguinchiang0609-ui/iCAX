"""Template-owned target shape for a tube bent at each segmented V slot.

The blank length and the target polygon use the exact station spacing reported
by the segmented-bend cutting tool. This is a geometric target preview: spring
back, local plastic deformation and closure of the cut faces are not solved.
"""

import importlib.util
import math
from pathlib import Path

_geometry_spec = importlib.util.spec_from_file_location(
    "icax_formed_section_geometry", Path(__file__).resolve().parents[2] / "_shared" / "assembly_applicability_geometry.py")
geometry = importlib.util.module_from_spec(_geometry_spec)
_geometry_spec.loader.exec_module(geometry)


def _finite_positive(value, label):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or value <= 0:
        raise ValueError(f"{label}必须为正数")
    return float(value)


def _section(plan):
    first, second = plan["designParts"]
    requests = (first["request"], second["request"])
    if any(request["profileRef"] != {"scope": "system", "id": "rect"} for request in requests):
        raise ValueError("分段折弯示意目前仅支持方矩管")
    section = geometry.standard_section(requests[0])
    geometry.require(section is not None, "分段折弯示意目前仅支持方矩管")
    geometry.require(geometry.sections_match(*requests), "两段管型截面必须一致，且壁厚须小于截面半宽")
    geometry.centered_section(section, "分段开槽折弯")
    geometry.uniform_section(section, "分段开槽折弯")
    width, depth, wall = (section[key] for key in ("width", "depth", "wall"))
    return {
        "width": width, "depth": depth, "wall": wall,
        "outerRadius": section["outerRadii"][0],
        "innerRadius": section["innerRadii"][0],
        "lengthA": _finite_positive(first["request"]["length"], "前段长度"),
        "lengthB": _finite_positive(second["request"]["length"], "后段长度"),
    }


def _layout(plan, section):
    operation = next((item for item in plan["resolvedWorkflow"]["partOperations"]
                      if item.get("resourceRef", {}).get("id") == "segmented-bend"), None)
    if operation is None:
        raise ValueError("分段折弯模板缺少对应的单件槽口工艺")
    tool_parameters = operation["toolParameters"]
    tool_path = Path(__file__).resolve().parents[2] / "mold" / "segmented-bend" / "tool.py"
    spec = importlib.util.spec_from_file_location("icax_segmented_bend_cut_layout", tool_path)
    if spec is None or spec.loader is None:
        raise ValueError("无法读取分段槽口的下料规则")
    tool = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(tool)
    width, depth, wall = section["width"], section["depth"], section["wall"]
    analysis = {
        "outside": {"min": [-width / 2, -depth / 2], "max": [width / 2, depth / 2]},
        "inside": {"min": [-width / 2 + wall, -depth / 2 + wall]},
        "wallThickness": wall,
    }
    # Calling the cutting tool keeps the target-shape stations synchronized
    # with its chord/tangent model, tolerance-driven count and K allowance.
    calculation = tool.generate(tool_parameters, {"analysis": analysis})["calculation"]
    return calculation


def build_plan(plan):
    section = _section(plan)
    layout = _layout(plan, section)
    # The cut envelope extends half an opening beyond both extreme slot
    # centres. A/B measure material outside that envelope, not the distances
    # to its centre slots. This is a cut-stock datum, not a neutral-layer
    # bending allowance.
    plan["manufacturingParts"][0]["request"]["length"] = (
        section["lengthA"] + layout["patternLength"] + section["lengthB"])
    # The tool's stations are centred on the stock by default. If the two
    # straight lengths differ, shift that station pattern to preserve both.
    station = (section["lengthA"] - section["lengthB"]) / 2
    for feature in plan["manufacturingParts"][0]["request"]["features"]:
        if feature.get("toolRef", {}).get("id") == "segmented-bend":
            feature["station"] = station
    for operation in plan["resolvedWorkflow"]["partOperations"]:
        if operation.get("resourceRef", {}).get("id") == "segmented-bend":
            operation["placement"]["station"] = station
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
    layout = _layout(plan, section)
    count = layout["segmentCount"]
    pitch = layout["slotPitch"]
    half_opening = layout["singleNotchOpening"] / 2
    angle = math.radians(layout["bendAngle"])
    delta = angle / count
    if pitch <= 0 or math.cos(delta / 2) <= 0:
        raise ValueError("分段槽距或单槽折角无效")
    rotation = math.radians(plan["parameters"].get("planeRotation", 0)
                            if plan["parameters"].get("bendPlane") == "spatial" else 0)
    cr, sr = math.cos(rotation), math.sin(rotation)

    def world(x, lateral, radial):
        return [x, lateral * cr - radial * sr, lateral * sr + radial * cr]

    # Every slot closes by delta. Adjacent hinge centres are separated by the
    # cutting tool's actual pitch, so changing slot count or spacing model
    # changes both the blank and the many-facet target shape.
    hinges = [(0.0, 0.0)]
    for index in range(1, count):
        x, z = hinges[-1]
        theta = index * delta
        hinges.append((x + pitch * math.cos(theta), z + pitch * math.sin(theta)))
    last_x, last_z = hinges[-1]
    stations = [(-section["lengthA"] - half_opening, 0.0, 0.0, 1.0)]
    stations.extend((x, z, (index + 0.5) * delta, 1 / math.cos(delta / 2))
                    for index, (x, z) in enumerate(hinges))
    stations.append((last_x + (section["lengthB"] + half_opening) * math.cos(angle),
                     last_z + (section["lengthB"] + half_opening) * math.sin(angle), angle, 1.0))
    outer = _rounded_rectangle(section["width"], section["depth"], section["outerRadius"])
    inner = _rounded_rectangle(section["width"] - 2 * section["wall"],
                               section["depth"] - 2 * section["wall"], section["innerRadius"])
    vertex_count = len(outer)
    positions, indices = [], []

    def ring(contour, station):
        first = len(positions) // 3
        cx, cz, theta, miter_scale = station
        sn, cs = math.sin(theta), math.cos(theta)
        for lateral, radial in contour:
            positions.extend(world(cx - radial * sn * miter_scale, lateral,
                                   cz + radial * cs * miter_scale))
        return first

    outer_rings = [ring(outer, station) for station in stations]
    inner_rings = [ring(inner, station) for station in stations]

    def wall_strip(near, far, inward=False):
        for index in range(vertex_count):
            following = (index + 1) % vertex_count
            if inward:
                indices.extend((near + index, far + following, near + following,
                                near + index, far + index, far + following))
            else:
                indices.extend((near + index, near + following, far + following,
                                near + index, far + following, far + index))

    for station in range(len(stations) - 1):
        wall_strip(outer_rings[station], outer_rings[station + 1])
        wall_strip(inner_rings[station], inner_rings[station + 1], True)
    for index in range(vertex_count):
        following = (index + 1) % vertex_count
        indices.extend((outer_rings[0] + index, inner_rings[0] + index, inner_rings[0] + following,
                        outer_rings[0] + index, inner_rings[0] + following, outer_rings[0] + following))
        end = len(stations) - 1
        indices.extend((outer_rings[end] + index, outer_rings[end] + following,
                        inner_rings[end] + following, outer_rings[end] + index,
                        inner_rings[end] + following, inner_rings[end] + index))

    anchors = {
        "straightStart": world(-section["lengthA"] - half_opening, 0, 0),
        "firstUncutEdge": world(-half_opening, 0, 0),
        "firstHinge": world(hinges[0][0], 0, hinges[0][1]),
        "secondHinge": world(hinges[1][0], 0, hinges[1][1]),
        "lastHinge": world(last_x, 0, last_z),
        "hingeCenters": [world(x, 0, z) for x, z in hinges],
        "lastUncutEdge": world(last_x + half_opening * math.cos(angle), 0,
                               last_z + half_opening * math.sin(angle)),
        "straightEnd": world(last_x + (section["lengthB"] + half_opening) * math.cos(angle), 0,
                             last_z + (section["lengthB"] + half_opening) * math.sin(angle)),
        "planeNormal": world(0, 1, 0),
    }
    return {
        "kind": 1, "positions": positions, "indices": indices,
        "metadata": {
            "previewKind": "target-shape", "approximate": True,
            "formingValidation": "not-performed", "bendProcess": "segmented-bend",
            "angle": layout["bendAngle"], "section": section,
            "segmentCount": count, "singleNotchAngle": layout["singleNotchAngle"],
            "slotPitch": pitch, "singleNotchOpening": layout["singleNotchOpening"],
            "patternLength": layout["patternLength"], "spacingModel": layout["spacingModel"],
            "blankLength": plan["manufacturingParts"][0]["request"]["length"],
            "contourVertexCount": vertex_count, "stationCount": len(stations),
            "annotations": {"kind": "segmented-bend-target", "values": {
                "angle": layout["bendAngle"], "segmentCount": count, "pitch": pitch,
                "singleNotchAngle": layout["singleNotchAngle"]}, "anchors": anchors},
        },
    }
