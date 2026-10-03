"""A template-owned, uncalibrated target shape for the flexible slit bend.

The cutting tool remains the authority for slit count, pitch and validation.
This sweep only illustrates the specified centreline arc and slit locations;
it does not predict the material's actual formed shape or reproduce the cuts.
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
        raise ValueError("柔性缝目标示意目前仅支持方矩管")
    section = geometry.standard_section(requests[0])
    geometry.require(section is not None, "柔性缝目标示意目前仅支持方矩管")
    geometry.require(geometry.sections_match(*requests), "两段管型截面必须一致，且壁厚须小于截面半宽")
    geometry.centered_section(section, "柔性缝折弯")
    geometry.uniform_section(section, "柔性缝折弯")
    width, depth, wall = (section[key] for key in ("width", "depth", "wall"))
    return {
        "width": width,
        "depth": depth,
        "wall": wall,
        "outerRadius": section["outerRadii"][0],
        "innerRadius": section["innerRadii"][0],
        "lengthA": _positive(requests[0]["length"], "前段长度"),
        "lengthB": _positive(requests[1]["length"], "后段长度"),
    }


def _layout(plan, section):
    operation = next(
        (item for item in plan["resolvedWorkflow"]["partOperations"]
         if item.get("resourceRef", {}).get("id") == "flexible-slit-bend"), None)
    if operation is None:
        raise ValueError("柔性缝模板缺少对应的单件槽口工艺")
    tool_path = Path(__file__).resolve().parents[2] / "mold" / "flexible-slit-bend" / "tool.py"
    spec = importlib.util.spec_from_file_location("icax_flexible_slit_cut_layout", tool_path)
    if spec is None or spec.loader is None:
        raise ValueError("无法读取柔性缝的下料规则")
    tool = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(tool)
    width, depth, wall = section["width"], section["depth"], section["wall"]
    analysis = {
        "outside": {"min": [-width / 2, -depth / 2], "max": [width / 2, depth / 2]},
        "inside": {"min": [-width / 2 + wall, -depth / 2 + wall]},
        "wallThickness": wall,
        "tolerance": 1e-6,
    }
    parameters = operation["toolParameters"]
    calculation = tool.generate(parameters, {"analysis": analysis})["calculation"]
    count = calculation["slitCount"]
    pitch = calculation["slitPitch"]
    width = _positive(parameters["slitWidth"], "割缝宽度")
    mode = parameters["slitMode"]
    u_span = _positive(parameters["uSpan"], "U 槽开口宽度") if mode == "u" else 0.0
    envelope = (count - 1) * pitch + width + u_span
    flexible_length = calculation["flexibleLength"]
    margin = (flexible_length - envelope) / 2
    if margin < -1e-6:
        raise ValueError("柔性缝包络超出目标曲率区")
    return {
        "calculation": calculation,
        "parameters": parameters,
        "operation": operation,
        "patternLength": envelope,
        "patternMargin": max(0.0, margin),
    }


def build_plan(plan):
    section = _section(plan)
    layout = _layout(plan, section)
    flexible_length = layout["calculation"]["flexibleLength"]
    length = section["lengthA"] + flexible_length + section["lengthB"]
    blank = plan["manufacturingParts"][0]
    blank["request"]["length"] = length
    # The tool's stations are centred on the blank. Move their centre if the
    # two design straight lengths differ; the curved zone remains R * beta.
    station = (section["lengthA"] - section["lengthB"]) / 2
    for feature in blank["request"]["features"]:
        if feature.get("toolRef", {}).get("id") == "flexible-slit-bend":
            feature["station"] = station
    layout["operation"]["placement"]["station"] = station
    for check in plan["resolvedWorkflow"]["checks"]:
        if check.get("id") == f"operation-span:{layout['operation']['id']}":
            check.update({
                "minimum": station,
                "maximum": station,
                "partLength": length,
                "status": "pass" if abs(station) <= length / 2 else "fail",
                "blocking": abs(station) > length / 2,
                "detail": f"加工中心 {station:.4f} mm，下料件半长 {length / 2:.4f} mm",
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
    return [
        (cy + radius * math.cos(math.radians(start + 90 * i / steps)),
         cz + radius * math.sin(math.radians(start + 90 * i / steps)))
        for cy, cz, start in corners for i in range(steps + 1)
    ]


def _profile_at(s, centers, slit_width, mode):
    """A shallow visual witness on the target sweep, not the cut geometry."""
    half = slit_width / 2
    amount = 0.0
    for center in centers:
        relative = abs(s - center) / half
        if relative >= 1:
            continue
        if mode == "narrow":
            amount = max(amount, math.sqrt(max(0.0, 1 - relative * relative)))
        else:
            amount = max(amount, min(1.0, (1 - relative) * 5))
    return amount


def build_formed_preview(plan):
    section = _section(plan)
    layout = _layout(plan, section)
    calculation, parameters = layout["calculation"], layout["parameters"]
    angle = math.radians(calculation["targetBendAngle"])
    radius = calculation["targetCenterlineRadius"]
    flexible_length = calculation["flexibleLength"]
    if radius <= section["depth"] / 2:
        raise ValueError("目标中心线半径过小，方矩管内侧可能自交，无法生成可信的成形示意")
    count, pitch, mode = calculation["slitCount"], calculation["slitPitch"], parameters["slitMode"]
    slit_width = _positive(parameters["slitWidth"], "割缝宽度")
    u_span = _positive(parameters["uSpan"], "U 槽开口宽度") if mode == "u" else 0.0
    length_a, length_b = section["lengthA"], section["lengthB"]
    total_length = length_a + flexible_length + length_b
    slit_centers = [length_a + flexible_length / 2 + (i - (count - 1) / 2) * pitch
                    for i in range(count)]
    groove_centers = ([center + sign * u_span / 2 for center in slit_centers for sign in (-1, 1)]
                      if mode == "u" else slit_centers)

    def point(s, lateral=0.0, radial=0.0):
        if s <= length_a:
            x, z, theta = s - length_a, 0.0, 0.0
        elif s <= length_a + flexible_length:
            theta = (s - length_a) / radius
            x, z = radius * math.sin(theta), radius * (1 - math.cos(theta))
        else:
            theta = angle
            tail = s - length_a - flexible_length
            x = radius * math.sin(angle) + tail * math.cos(angle)
            z = radius * (1 - math.cos(angle)) + tail * math.sin(angle)
        return [x - radial * math.sin(theta), lateral, z + radial * math.cos(theta)]

    # Both long straight arms stay straight. Curved rings sample the complete
    # target R*beta zone, including the uncut margins before/after the slits.
    samples = {0.0, length_a, length_a + flexible_length, total_length}
    arc_steps = max(24, math.ceil(math.degrees(angle) / 3), math.ceil(flexible_length / 4))
    samples.update(length_a + flexible_length * i / arc_steps for i in range(1, arc_steps))
    for center in groove_centers:
        for offset in (-slit_width / 2, -slit_width * 0.4, 0, slit_width * 0.4, slit_width / 2):
            sample = center + offset
            if length_a < sample < length_a + flexible_length:
                samples.add(sample)
    stations = sorted(samples)
    outer = _rounded_rectangle(section["width"], section["depth"], section["outerRadius"])
    inner = _rounded_rectangle(section["width"] - 2 * section["wall"],
                               section["depth"] - 2 * section["wall"], section["innerRadius"])
    ring_size = len(outer)
    positions, indices = [], []
    top = section["depth"] / 2
    wall = section["wall"]

    def add_ring(contour, s, outer_surface):
        first = len(positions) // 3
        groove = _profile_at(s, groove_centers, slit_width, mode) if outer_surface else 0.0
        for lateral, radial in contour:
            # The small top-face witness distinguishes slit positions and
            # modes. It is intentionally shallower than the real through cut.
            top_weight = max(0.0, min(1.0, (radial - (top - wall)) / wall))
            witness = groove * wall * 0.6 * top_weight
            positions.extend(point(s, lateral, radial - witness))
        return first

    outer_rings = [add_ring(outer, s, True) for s in stations]
    inner_rings = [add_ring(inner, s, False) for s in stations]

    def wall_strip(near, far, inward=False):
        for index in range(ring_size):
            following = (index + 1) % ring_size
            if inward:
                indices.extend((near + index, far + following, near + following,
                                near + index, far + index, far + following))
            else:
                indices.extend((near + index, near + following, far + following,
                                near + index, far + following, far + index))

    for index in range(len(stations) - 1):
        wall_strip(outer_rings[index], outer_rings[index + 1])
        wall_strip(inner_rings[index], inner_rings[index + 1], True)
    for index in range(ring_size):
        following = (index + 1) % ring_size
        indices.extend((outer_rings[0] + index, inner_rings[0] + index,
                        inner_rings[0] + following, outer_rings[0] + index,
                        inner_rings[0] + following, outer_rings[0] + following))
        end = len(stations) - 1
        indices.extend((outer_rings[end] + index, outer_rings[end] + following,
                        inner_rings[end] + following, outer_rings[end] + index,
                        inner_rings[end] + following, inner_rings[end] + index))

    anchors = {
        "straightStart": point(0),
        "curveStart": point(length_a),
        "slitCenters": [point(center) for center in slit_centers],
        "curveEnd": point(length_a + flexible_length),
        "straightEnd": point(total_length),
        "planeNormal": [0, 1, 0],
    }
    return {
        "kind": 1, "positions": positions, "indices": indices,
        "metadata": {
            "previewKind": "target-shape", "approximate": True,
            "formingValidation": "calibration-required",
            "bendProcess": "flexible-slit-bend", "slitMode": mode,
            "angle": calculation["targetBendAngle"], "bendRadius": radius,
            "slitCount": count, "slitPitch": pitch, "slitWidth": slit_width,
            "flexibleLength": flexible_length,
            "patternLength": layout["patternLength"],
            "patternMargin": layout["patternMargin"],
            "blankLength": total_length,
            "contourVertexCount": ring_size, "stationCount": len(stations),
            "witnessNote": "浅纹仅标示槽位；实际槽形请查看下料件。成形角度和半径须试样标定。",
            "annotations": {
                "kind": "flexible-slit-target",
                "values": {
                    "angle": calculation["targetBendAngle"], "bendRadius": radius,
                    "slitCount": count, "slitPitch": pitch, "slitMode": mode,
                    "flexibleLength": flexible_length,
                    "patternLength": layout["patternLength"],
                },
                "anchors": anchors,
            },
        },
    }
