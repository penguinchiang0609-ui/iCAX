"""Read-only geometric queries for assembly input checks.

No profile generator or manufacturing-plan builder is loaded here. Template
rules decide which measurements they require and what constitutes a fit.
"""
from functools import wraps
import math


def checked(function):
    """Expose a small immutable-input applicability contract."""
    @wraps(function)
    def run(process_input, parameters):
        try:
            function(process_input, parameters)
            return {"applicable": True, "reason": ""}
        except (ValueError, KeyError, TypeError, OverflowError) as error:
            return {"applicable": False, "reason": str(error) or "局部加工输入无效"}
    return run


def require(condition, reason):
    if not condition:
        raise ValueError(reason)


def number(value, label, minimum=None):
    require(not isinstance(value, bool) and isinstance(value, (int, float))
            and math.isfinite(value), f"{label}必须是有限数值")
    require(minimum is None or value >= minimum, f"{label}不能小于 {minimum}")
    return float(value)


def positive(value, label):
    value = number(value, label)
    require(value > 0, f"{label}必须大于零")
    return value


def span(product, span_id):
    result = product["spans"][span_id]
    positive(result["length"], f"{span_id} 长度")
    return result


def part(process_input, role):
    """Read an explicitly mapped local participant, never a product shape."""
    result = process_input["parts"][role]
    positive(result["length"], f"{role} 长度")
    return result


def same_section(first, second, label):
    require(sections_match(first, second), f"{label}必须采用相同管型及截面参数")


def sections_match(first, second):
    """Compare measured geometry; inactive section drafts stay untouched."""
    if first["profileRef"] != second["profileRef"]:
        return False
    if first["parameters"] == second["parameters"]:
        return True
    a, b = standard_section(first), standard_section(second)
    # Unknown sections cannot be inferred from their arbitrary parameters.
    return a is not None and b is not None and a == b


def _radii(parameters, flag, uniform, prefix, limit):
    values = ([parameters[f"{prefix}{index}"] for index in range(1, 5)]
              if parameters.get(flag, False) else [parameters.get(uniform, 0)] * 4)
    result = [number(value, "截面圆角", 0) for value in values]
    require(all(radius <= limit for radius in result), "截面圆角超出截面范围")
    return result


def standard_section(part):
    """Measure known section parameters; unknown profile geometry stays unknown."""
    ref, p = part["profileRef"], part["parameters"]
    if ref.get("scope") != "system" or ref.get("id") not in ("rect", "round") or p.get("materialBoundary"):
        return None
    kind = ref["id"]
    width = positive(p["width"], "截面宽度/外径")
    depth = positive(p["depth"], "截面高度") if kind == "rect" else width
    wall = positive(p["wallThickness"], "截面壁厚")
    require(2 * wall < min(width, depth), "壁厚必须小于截面短边的一半")
    x = number(p.get("innerOffsetX", 0), "内孔横向偏心量")
    y = number(p.get("innerOffsetY", 0), "内孔纵向偏心量")
    require(abs(x) < wall and abs(y) < wall, "内孔偏心量不能穿过外壁")
    if kind == "round":
        require(math.hypot(x, y) < wall, "圆管内孔偏心量不能穿过外壁")
        outer_radii, inner_radii = [width / 2] * 4, [width / 2 - wall] * 4
    else:
        outer_radii = _radii(p, "useOuterRadii", "cornerRadius", "outerRadius", min(width, depth) / 2)
        inner_radii = _radii(p, "useInnerRadii", "innerRadius", "innerRadius", min(width, depth) / 2 - wall)
    return {"kind": kind, "width": width, "depth": depth, "wall": wall,
            "offsetX": x, "offsetY": y, "outerRadii": outer_radii, "innerRadii": inner_radii,
            "flatWidth": width - 2 * max(outer_radii), "flatDepth": depth - 2 * max(outer_radii),
            "innerWidth": width - 2 * wall, "innerDepth": depth - 2 * wall}


def rectangular_section(part, label):
    section = standard_section(part)
    require(section is not None and section["kind"] == "rect", f"{label}当前仅支持系统方矩管截面")
    require(section["flatWidth"] > 0 and section["flatDepth"] > 0, f"{label}需要四边平直的方矩管")
    return section


def centered_section(section, label):
    require(abs(section["offsetX"]) <= 1e-6 and abs(section["offsetY"]) <= 1e-6,
            f"{label}当前需要居中内孔")


def uniform_section(section, label):
    require(max(section["outerRadii"]) - min(section["outerRadii"]) <= 1e-6
            and max(section["innerRadii"]) - min(section["innerRadii"]) <= 1e-6,
            f"{label}当前需要统一的内外圆角")


def centered_envelope(part, axial_span, transverse_span, label):
    axial = positive(axial_span, f"{label}轴向包络")
    transverse = positive(transverse_span, f"{label}横向包络")
    require(axial <= part["length"] + 1e-7, f"{label}完整包络超出成品区域长度")
    section = standard_section(part)
    if section is not None:
        require(transverse <= section["width"] + 1e-7, f"{label}完整包络超出管材侧面宽度")


def axis(part, column=0):
    matrix = part["matrix"]
    require(isinstance(matrix, list) and len(matrix) == 16, "局部工艺需要明确的构件坐标")
    return [number(matrix[row * 4 + column], "构件坐标") for row in range(3)]


def origin(part):
    return [number(part["matrix"][index], "构件原点") for index in (3, 7, 11)]


def dot(first, second):
    return sum(a * b for a, b in zip(first, second))


def site(part):
    anchor = part.get("anchor")
    require(isinstance(anchor, dict) and anchor.get("kind") == "side"
            and anchor.get("reference") == "start", "局部加工必须提供明确的侧面定位基准")
    station = number(anchor["station"], "加工位置", 0)
    offset = number(anchor.get("offset", 0), "加工横移")
    face = anchor["face"]
    require(face in ("top", "bottom", "left", "right"), "加工侧面无效")
    # Blank coordinates use X along the tube, Y across its width and Z
    # through its depth, matching the native punch face convention.
    normal = axis(part, 2 if face in ("top", "bottom") else 1)
    across = axis(part, 1 if face in ("top", "bottom") else 2)
    along = axis(part)
    point = [o + a * station + b * offset for o, a, b in zip(origin(part), along, across)]
    rotation = math.radians(number(anchor.get("rotation", 0), "加工平面转角"))
    cosine, sine = math.cos(rotation), math.sin(rotation)
    return {"station": station, "offset": offset, "point": point, "normal": normal,
            "along": [a * cosine + b * sine for a, b in zip(along, across)],
            "across": [b * cosine - a * sine for a, b in zip(along, across)],
            "rotation": rotation}


def local_envelope(part, axial_span, transverse_span, label, radius=0):
    axial = positive(axial_span, f"{label}轴向包络")
    transverse = positive(transverse_span, f"{label}横向包络")
    location = site(part)
    cosine, sine = abs(math.cos(location["rotation"])), abs(math.sin(location["rotation"]))
    corner = number(radius, f"{label}包络圆角", 0)
    require(corner <= min(axial, transverse) / 2 + 1e-7, "加工包络圆角超过边长")
    axial_half = (axial / 2 - corner) * cosine + (transverse / 2 - corner) * sine + corner
    transverse_half = (transverse / 2 - corner) * cosine + (axial / 2 - corner) * sine + corner
    require(location["station"] - axial_half >= -1e-7
            and location["station"] + axial_half <= part["length"] + 1e-7,
            f"{label}完整包络超出母材长度")
    section = standard_section(part)
    if section is not None:
        face = part.get("anchor", {}).get("face", "top")
        span = section["width"] if face in ("top", "bottom") else section["depth"]
        require(abs(location["offset"]) + transverse_half <= span / 2 + 1e-7,
                f"{label}完整包络超出实际加工侧面")


def aligned_hole_sites(first, second, clearance, travel=0, travel_axis="x", count=1):
    a, b = site(first), site(second)
    require(abs(dot(a["normal"], b["normal"])) >= 1 - 1e-6, "配合孔的实际轴线方向不一致")
    if count > 1:
        require(abs(dot(a["along"], b["along"])) >= 1 - 1e-6, "成组配合孔的实际排列方向不一致")
    delta = [y - x for x, y in zip(a["point"], b["point"])]
    normal_distance = dot(delta, a["normal"])
    planar = [d - normal_distance * n for d, n in zip(delta, a["normal"])]
    direction = a["along"] if travel_axis == "x" else a["across"]
    moving = dot(planar, direction)
    residual = [d - moving * n for d, n in zip(planar, direction)]
    require(abs(moving) <= travel / 2 + clearance + 1e-6
            and math.sqrt(dot(residual, residual)) <= clearance + 1e-6,
            "实际孔位无法在配合间隙或允许调节范围内对齐")


def overlap_interval(first, second):
    along = axis(first)
    direction = dot(along, axis(second))
    require(abs(direction) >= 1 - 1e-6, "当前搭接开孔工艺要求局部构件轴线平行")
    delta = [b - a for a, b in zip(origin(first), origin(second))]
    start = dot(delta, along)
    end = start + direction * second["length"]
    return max(0, min(start, end)), min(first["length"], max(start, end))


def projection_half_span(section, degrees):
    if section["kind"] == "round":
        return section["width"] / 2
    angle = math.radians(number(degrees, "截面投影转角"))
    radius = section["outerRadii"][0]
    return ((section["width"] / 2 - radius) * abs(math.sin(angle))
            + (section["depth"] / 2 - radius) * abs(math.cos(angle)) + radius)
