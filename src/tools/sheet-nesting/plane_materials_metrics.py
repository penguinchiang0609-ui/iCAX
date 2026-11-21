"""Neutral report measurements of original geometry at returned native poses.

No geometry, pose, sheet condition or nesting decision is changed here. Roll
lengths start at coordinate zero, include the original trailing edge margin,
and retain each layout's full fixed width. The million-unit test cap does not
identify a roll; only the source stock declaration can do that.
"""
from __future__ import annotations

import math


def source_quantity_rows(task):
    """Original DXF/contour demand, with native component counts kept separate.

    One copy of a DXF containing seven disconnected components means one source
    file copy and seven physical pieces. It must never become seven DXF copies
    in the competitor import list. Original/native records remain unchanged.
    """
    native=task.get("parts",[])
    originals=task.get("sourceParts")
    rows=[]
    for index,part in enumerate(originals if originals is not None else native):
        if originals is None:
            components=[part] if task.get("status") in {"ready","runnable"} else []
        else:
            prefix=f"P{index:04d}"
            components=[value for value in native if value.get("id")==prefix or value.get("id","").startswith(prefix+"_C")]
        row=dict(part)
        row["nativePartIds"]=[value["id"] for value in components if isinstance(value.get("id"),str)]
        row["materialComponentCount"]=len(components) if components else None
        quantities=[value.get("quantity") for value in components]
        row["nativePieceQuantity"]=sum(quantities) if quantities and all(type(value) is int and value>=0 for value in quantities) else None
        rows.append(row)
    return rows


def _number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def _positive(value):
    return _number(value) and value > 0


def _unlimited(value):
    return value is None or (_number(value) and value < 0)


def _roll_axis(task):
    source = task.get("sourceStock")
    if not isinstance(source, dict) or str(source.get("kind", "")).lower() != "roll":
        return None
    width, height = source.get("width"), source.get("height")
    if _positive(width) and _unlimited(height):
        return "y"
    if _positive(height) and _unlimited(width):
        return "x"
    return None


def _ring_area(ring):
    if not isinstance(ring, list) or len(ring) < 3:
        raise ValueError("原始多边形轮廓不足三个顶点。")
    if any(not isinstance(point, (list, tuple)) or len(point) < 2 or not all(_number(value) for value in point[:2]) for point in ring):
        raise ValueError("原始轮廓含无效坐标。")
    # Subtract an original anchor to avoid cancelling large translated values.
    ax, ay = ring[0][:2]
    return abs(math.fsum((ring[i][0] - ax) * (ring[(i + 1) % len(ring)][1] - ay)
                         - (ring[(i + 1) % len(ring)][0] - ax) * (ring[i][1] - ay)
                         for i in range(len(ring)))) / 2


def geometry_area(geometry):
    """Net area of one represented original; never multiplies demand quantity."""
    radius = geometry.get("radius", 0)
    outer = math.pi * radius * radius if _positive(radius) else _ring_area(geometry.get("outer"))
    area = outer - math.fsum(_ring_area(ring) for ring in geometry.get("holes", []))
    if not _number(area) or area < 0:
        raise ValueError("原始材料净面积无效。")
    return area


def geometry_bounds(geometry, placement):
    """World bounds from original outer vertices or an analytic circle."""
    if not isinstance(placement, dict):
        raise ValueError("原生零件位姿无效。")
    pose = placement.get("transform", placement.get("pose", placement))
    if not isinstance(pose, dict):
        raise ValueError("原生零件位姿无效。")
    x, y = pose.get("x"), pose.get("y")
    angle = pose.get("angle", pose.get("angleDegrees", 0))
    mirrored = pose.get("mirrored", False)
    if type(mirrored) is not bool:
        raise ValueError("原生零件镜像状态必须是布尔值。")
    if not all(_number(value) for value in (x, y, angle)):
        raise ValueError("原生零件位姿无效。")
    angle = math.remainder(angle, 360)
    quarter = {0: (1, 0), 90: (0, 1), -90: (0, -1), 180: (-1, 0), -180: (-1, 0)}
    if angle in quarter:
        c, s = quarter[angle]
    else:
        radians = angle * math.pi / 180
        c, s = math.cos(radians), math.sin(radians)

    def world(point):
        if not isinstance(point, (list, tuple)) or len(point) < 2 or not all(_number(value) for value in point[:2]):
            raise ValueError("原始轮廓含无效坐标。")
        local_y = -point[1] if mirrored else point[1]
        return x + point[0] * c - local_y * s, y + point[0] * s + local_y * c

    radius = geometry.get("radius", 0)
    if _positive(radius):
        cx, cy = world(geometry.get("center", [0, 0]))
        return cx - radius, cy - radius, cx + radius, cy + radius
    ring = geometry.get("outer")
    if not isinstance(ring, list) or len(ring) < 3:
        raise ValueError("原始外轮廓缺失，无法量取占用长度。")
    points = [world(point) for point in ring]
    return min(point[0] for point in points), min(point[1] for point in points), max(point[0] for point in points), max(point[1] for point in points)


def calculate_metrics(task, benchmark, material_area=None):
    """Return report-only areas and source-declared roll usage, using mm units.

    ``material_area`` is the actual NativeValidation material area when the
    caller has it. Missing values remain None. Partial/invalid outputs can be
    measured, but these measurements do not change their completion verdict.
    """
    metrics = dict(materialArea=None, sheetArea=None, rollUsage=None,
                   materialAreaSource=None, metricsIssues=[])
    layouts = benchmark.get("layouts")
    if not isinstance(layouts, list):
        layouts = []
    valid_sheets = bool(layouts) and all(isinstance(layout, dict) and _positive(layout.get("width")) and _positive(layout.get("height")) for layout in layouts)
    if valid_sheets:
        metrics["sheetArea"] = math.fsum(layout["width"] * layout["height"] for layout in layouts)
    validation = benchmark.get("validation")
    candidates = ((material_area, "native-validation"),
                  (validation.get("materialArea") if isinstance(validation, dict) else None, "native-validation"),
                  (benchmark.get("materialArea"), benchmark.get("materialAreaSource") or "reported-material-area"))
    for area, source in candidates:
        if _number(area) and area >= 0:
            metrics["materialArea"], metrics["materialAreaSource"] = float(area), source
            break
    if metrics["materialArea"] is None and metrics["sheetArea"] is not None:
        utilization = benchmark.get("utilization")
        if _number(utilization) and utilization >= 0:
            metrics["materialArea"] = utilization * metrics["sheetArea"]
            metrics["materialAreaSource"] = "legacy-sheet-utilization"
    problem = benchmark.get("problem")
    originals = {part.get("id", part.get("ID")): part.get("geometry") for part in problem.get("parts", [])} if isinstance(problem, dict) else {}
    if metrics["materialArea"] is None and layouts:
        try:
            placed_areas = []
            for layout in layouts:
                for placement in layout.get("placements", []):
                    part_id = placement.get("part", placement.get("partId", placement.get("partID")))
                    geometry = originals.get(part_id)
                    if not isinstance(geometry, dict):
                        raise ValueError("缺少已排原件几何，无法计算净面积。")
                    placed_areas.append(geometry_area(geometry))
            if placed_areas:
                metrics["materialArea"] = math.fsum(placed_areas)
                metrics["materialAreaSource"] = "original-geometry"
        except (ValueError, TypeError, KeyError) as error:
            metrics["metricsIssues"].append(str(error))
    axis = _roll_axis(task)
    if axis is None:
        return metrics
    if not valid_sheets:
        metrics["metricsIssues"].append("缺少实际板材尺寸，无法计算卷料占用段。")
        return metrics
    rules = task.get("rules") or {}
    margin = rules.get("edgeMarginMm", rules.get("edgeMargin", problem.get("edgeMargin") if isinstance(problem, dict) else None))
    if not _number(margin) or margin < 0:
        metrics["metricsIssues"].append("原边距缺失或无效，无法计算卷料占用长度。")
        return metrics
    lengths, used_areas = [], []
    try:
        for layout in layouts:
            maxima = []
            for placement in layout.get("placements", []):
                part_id = placement.get("part", placement.get("partId", placement.get("partID")))
                geometry = originals.get(part_id)
                if not isinstance(geometry, dict):
                    raise ValueError("缺少已排原件外轮廓，无法计算卷料占用长度。")
                bounds = geometry_bounds(geometry, placement)
                maxima.append(bounds[2] if axis == "x" else bounds[3])
            length = max(0, max(maxima) + margin) if maxima else 0.0
            fixed_width = layout["height"] if axis == "x" else layout["width"]
            lengths.append(length)
            used_areas.append(fixed_width * length)
    except (ValueError, TypeError, KeyError) as error:
        metrics["metricsIssues"].append(str(error))
        return metrics
    used_area = math.fsum(used_areas)
    area = metrics["materialArea"]
    metrics["rollUsage"] = dict(axis=axis, lengths=lengths, totalLength=math.fsum(lengths), usedArea=used_area,
                                utilization=area / used_area if area is not None and used_area > 0 else None)
    return metrics
