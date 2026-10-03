"""Place the two manufactured miter faces at one finished L corner."""

import math
import importlib.util
from pathlib import Path

_spec = importlib.util.spec_from_file_location(
    "icax_miter_section_queries", Path(__file__).resolve().parents[2] / "_shared" /
    "assembly_applicability_geometry.py")
geometry = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(geometry)


def _half_span(part, plane_rotation):
    request = part["request"]
    profile = request["profileRef"]
    section = request["parameters"]
    if profile == {"scope": "system", "id": "round"}:
        diameter = float(section["width"])
        if not math.isfinite(diameter) or diameter <= 0:
            raise ValueError("斜接示例的圆管外径无效")
        return diameter / 2
    if profile != {"scope": "system", "id": "rect"}:
        raise ValueError("斜接示例当前支持圆管和方矩管截面")
    width = float(section["width"])
    depth = float(section["depth"])
    radius = float(section["cornerRadius"])
    if not all(math.isfinite(value) for value in (width, depth, radius)) or not 0 <= radius < min(width, depth) / 2:
        raise ValueError("斜接示例的方矩管尺寸无效")
    # Exact support of the rounded outer rectangle in the miter direction.
    turn = math.radians(plane_rotation)
    return ((width / 2 - radius) * abs(math.sin(turn))
            + (depth / 2 - radius) * abs(math.cos(turn)) + radius)


def _translate(matrix, displacement):
    for offset, value in zip((3, 7, 11), displacement):
        matrix[offset] += value


def build_plan(plan):
    designs = {part["role"]: part for part in plan["designParts"]}
    first, second = designs["memberA"], designs["memberB"]
    if not geometry.sections_match(first["request"], second["request"]):
        raise ValueError("斜接示例的两根管须采用相同截面，才能使对切端面贴合")

    angle = math.radians(plan["parameters"]["jointAngle"])
    plane_rotation = math.radians(plan["parameters"]["planeRotation"])
    half_span = _half_span(first, plan["parameters"]["planeRotation"])
    if "processInput" in plan or "finishedProduct" in plan:
        # Input lengths end at the desired centreline corner. The long point
        # of each miter needs extra stock, rather than shortening that input.
        extension = half_span * math.tan(angle / 2)
        for part in plan["manufacturingParts"]:
            part["request"]["length"] += extension
            if part["sourceRole"] == "memberB":
                axis = [second["matrix"][index] for index in (0, 4, 8)]
                for key in ("matrix", "explodedMatrix", "compareMatrix"):
                    _translate(part[key], [-extension * value for value in axis])
        return plan
    # With the long-point datum, the miter face centre sits one half-span back
    # from each stock end. Move B so its cut face meets A's cut face.
    displacement = [
        -half_span * math.sin(angle),
        half_span * (1 - math.cos(angle)) * math.sin(plane_rotation),
        -half_span * (1 - math.cos(angle)) * math.cos(plane_rotation),
    ]
    # Registration belongs to the manufactured blanks. The desired product
    # geometry is an input and may not be moved by a manufacturing process.
    for part in plan["manufacturingParts"]:
        if part["sourceRole"] == "memberB":
            for key in ("matrix", "explodedMatrix", "compareMatrix"):
                _translate(part[key], displacement)
    return plan
