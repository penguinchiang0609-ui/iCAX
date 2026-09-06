from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
import math
from pathlib import Path
from typing import Any


_TAU = math.tau
_UNIT_SCALE_TO_MM = {
    0: (1.0, "未声明（按毫米）"),
    1: (25.4, "英寸"),
    2: (304.8, "英尺"),
    3: (1609344.0, "英里"),
    4: (1.0, "毫米"),
    5: (10.0, "厘米"),
    6: (1000.0, "米"),
    7: (1_000_000.0, "千米"),
    8: (0.0000254, "微英寸"),
    9: (0.0254, "密耳"),
    10: (914.4, "码"),
    11: (1.0e-7, "埃"),
    12: (1.0e-6, "纳米"),
    13: (0.001, "微米"),
    14: (100.0, "分米"),
    15: (10_000.0, "十米"),
    16: (100_000.0, "百米"),
    17: (1.0e12, "吉米"),
    18: (1.495978707e14, "天文单位"),
    19: (9.4607304725808e18, "光年"),
    20: (3.085677581491367e19, "秒差距"),
}


@dataclass
class _Segment:
    value: dict[str, Any]
    start: tuple[float, float]
    end: tuple[float, float]
    samples: list[tuple[float, float]]


@dataclass
class _Loop:
    segments: list[_Segment]
    samples: list[tuple[float, float]]


def _number(value: str, name: str) -> float:
    try:
        result = float(value.strip())
    except ValueError as error:
        raise ValueError(f"DXF {name} 不是有效数值") from error
    if not math.isfinite(result):
        raise ValueError(f"DXF {name} 必须是有限数值")
    return result


def _integer(value: str, name: str) -> int:
    try:
        return int(value.strip())
    except ValueError as error:
        raise ValueError(f"DXF {name} 不是有效整数") from error


def _read_pairs(path: Path) -> list[tuple[int, str]]:
    data = path.read_bytes()
    if len(data) > 32 * 1024 * 1024:
        raise ValueError("DXF 文件超过 32 MB")
    if data.startswith(b"AutoCAD Binary DXF"):
        raise ValueError("当前只支持 ASCII DXF；请在制图软件中另存为 ASCII DXF")
    text = None
    for encoding in ("utf-8-sig", "gb18030", "latin-1"):
        try:
            text = data.decode(encoding)
            break
        except UnicodeDecodeError:
            continue
    if text is None:
        raise ValueError("无法识别 DXF 文本编码")
    lines = text.splitlines()
    if len(lines) % 2:
        raise ValueError("DXF 组码和值没有成对出现")
    pairs: list[tuple[int, str]] = []
    for index in range(0, len(lines), 2):
        try:
            code = int(lines[index].strip())
        except ValueError as error:
            raise ValueError(f"DXF 第 {index + 1} 行不是有效组码") from error
        pairs.append((code, lines[index + 1].rstrip("\r")))
    return pairs


def _section_pairs(pairs: list[tuple[int, str]], requested: str) -> list[tuple[int, str]]:
    result: list[tuple[int, str]] = []
    section = ""
    index = 0
    while index < len(pairs):
        code, value = pairs[index]
        upper = value.strip().upper()
        if code == 0 and upper == "SECTION" and index + 1 < len(pairs) and pairs[index + 1][0] == 2:
            section = pairs[index + 1][1].strip().upper()
            index += 2
            continue
        if code == 0 and upper == "ENDSEC":
            section = ""
        elif section == requested:
            result.append((code, value))
        index += 1
    return result


def _records(pairs: list[tuple[int, str]]) -> list[tuple[str, list[tuple[int, str]]]]:
    records: list[tuple[str, list[tuple[int, str]]]] = []
    kind = ""
    values: list[tuple[int, str]] = []
    for code, value in pairs:
        if code == 0:
            if kind:
                records.append((kind, values))
            kind = value.strip().upper()
            values = []
        elif kind:
            values.append((code, value))
    if kind:
        records.append((kind, values))
    return records


def _first(values: list[tuple[int, str]], code: int, default: str | None = None) -> str:
    for candidate_code, value in values:
        if candidate_code == code:
            return value
    if default is not None:
        return default
    raise ValueError(f"DXF 实体缺少组码 {code}")


def _all(values: list[tuple[int, str]], code: int) -> list[str]:
    return [value for candidate_code, value in values if candidate_code == code]


def _point(values: list[tuple[int, str]], x_code: int = 10, y_code: int = 20) -> tuple[float, float]:
    return (_number(_first(values, x_code), str(x_code)), _number(_first(values, y_code), str(y_code)))


def _entity_is_model_space(values: list[tuple[int, str]]) -> bool:
    return _integer(_first(values, 67, "0"), "67") == 0


def _line(start: tuple[float, float], end: tuple[float, float]) -> _Segment:
    if math.dist(start, end) <= 1.0e-12:
        raise ValueError("DXF 中存在零长度直线")
    return _Segment(
        {"kind": "line", "start": list(start), "end": list(end)},
        start, end, [start, end],
    )


def _arc_from_center(
    center: tuple[float, float], radius: float, start_angle: float, sweep: float,
) -> _Segment:
    if radius <= 0.0 or abs(sweep) <= 1.0e-12 or abs(sweep) >= _TAU - 1.0e-10:
        raise ValueError("DXF 圆弧半径或角度无效")
    point = lambda angle: (
        center[0] + radius * math.cos(angle),
        center[1] + radius * math.sin(angle),
    )
    start = point(start_angle)
    middle = point(start_angle + sweep / 2.0)
    end = point(start_angle + sweep)
    count = max(4, int(math.ceil(abs(sweep) / (math.pi / 18.0))))
    samples = [point(start_angle + sweep * index / count) for index in range(count + 1)]
    return _Segment(
        {"kind": "arc", "start": list(start), "middle": list(middle), "end": list(end)},
        start, end, samples,
    )


def _bulge_segment(start: tuple[float, float], end: tuple[float, float], bulge: float) -> _Segment:
    if abs(bulge) <= 1.0e-12:
        return _line(start, end)
    chord = math.dist(start, end)
    if chord <= 1.0e-12:
        raise ValueError("DXF 多段线圆弧的端点重合")
    sweep = 4.0 * math.atan(bulge)
    midpoint = ((start[0] + end[0]) / 2.0, (start[1] + end[1]) / 2.0)
    dx, dy = end[0] - start[0], end[1] - start[1]
    offset = chord / (2.0 * math.tan(sweep / 2.0))
    center = (midpoint[0] - dy / chord * offset, midpoint[1] + dx / chord * offset)
    start_angle = math.atan2(start[1] - center[1], start[0] - center[0])
    return _arc_from_center(center, math.dist(center, start), start_angle, sweep)


def _rational_ellipse_segments(
    center: tuple[float, float], major: tuple[float, float], ratio: float,
    start_parameter: float, sweep: float,
) -> list[_Segment]:
    major_radius = math.hypot(*major)
    minor_radius = major_radius * ratio
    if major_radius <= 0.0 or not (0.0 < ratio <= 1.0):
        raise ValueError("DXF 椭圆的主轴或轴比无效")
    ux, uy = major[0] / major_radius, major[1] / major_radius
    vx, vy = -uy, ux
    chunk_count = max(1, int(math.ceil(abs(sweep) / (math.pi / 2.0))))
    result: list[_Segment] = []
    for index in range(chunk_count):
        a = start_parameter + sweep * index / chunk_count
        b = start_parameter + sweep * (index + 1) / chunk_count
        middle = (a + b) / 2.0
        weight = math.cos((b - a) / 2.0)
        def curve_point(parameter: float, divisor: float = 1.0) -> tuple[float, float]:
            return (
                center[0] + (ux * major_radius * math.cos(parameter)
                    + vx * minor_radius * math.sin(parameter)) / divisor,
                center[1] + (uy * major_radius * math.cos(parameter)
                    + vy * minor_radius * math.sin(parameter)) / divisor,
            )
        p0, p1, p2 = curve_point(a), curve_point(middle, weight), curve_point(b)
        sample_count = 10
        samples = [curve_point(a + (b - a) * sample / sample_count)
                   for sample in range(sample_count + 1)]
        result.append(_Segment({
            "kind": "nurbs", "degree": 2,
            "controlPoints": [list(p0), list(p1), list(p2)],
            "weights": [1.0, weight, 1.0],
            "knots": [0.0, 0.0, 0.0, 1.0, 1.0, 1.0],
        }, p0, p2, samples))
    return result


def _polyline_vertices(values: list[tuple[int, str]]) -> list[tuple[float, float, float]]:
    vertices: list[list[float]] = []
    current: list[float] | None = None
    for code, value in values:
        if code == 10:
            if current is not None:
                vertices.append(current)
            current = [_number(value, "10"), 0.0, 0.0]
        elif code == 20 and current is not None:
            current[1] = _number(value, "20")
        elif code == 42 and current is not None:
            current[2] = _number(value, "42")
    if current is not None:
        vertices.append(current)
    if len(vertices) < 2:
        raise ValueError("DXF 多段线至少需要两个顶点")
    return [(item[0], item[1], item[2]) for item in vertices]


def _segments_from_vertices(
    vertices: list[tuple[float, float, float]], closed: bool,
) -> list[_Segment]:
    result: list[_Segment] = []
    count = len(vertices) if closed else len(vertices) - 1
    for index in range(count):
        start = vertices[index]
        end = vertices[(index + 1) % len(vertices)]
        result.append(_bulge_segment((start[0], start[1]), (end[0], end[1]), start[2]))
    return result


def _spline(values: list[tuple[int, str]]) -> tuple[_Segment, bool]:
    degree = _integer(_first(values, 71), "71")
    flags = _integer(_first(values, 70, "0"), "70")
    knots = [_number(value, "40") for value in _all(values, 40)]
    weights = [_number(value, "41") for value in _all(values, 41)]
    points: list[list[float]] = []
    current: list[float] | None = None
    for code, value in values:
        if code == 10:
            if current is not None:
                points.append(current)
            current = [_number(value, "10"), 0.0]
        elif code == 20 and current is not None:
            current[1] = _number(value, "20")
    if current is not None:
        points.append(current)
    if degree < 1 or len(points) < degree + 1 or len(knots) < 2:
        raise ValueError("DXF SPLINE 的次数、控制点或节点无效")
    rational = bool(flags & 4) or bool(weights)
    if rational and not weights:
        weights = [1.0] * len(points)
    if weights and len(weights) != len(points):
        raise ValueError("DXF SPLINE 的权重数与控制点数不一致")
    periodic = bool(flags & 2)
    value: dict[str, Any] = {
        "kind": "nurbs" if rational else "bspline",
        "degree": degree,
        "controlPoints": points,
        "knots": knots,
    }
    if rational:
        value["weights"] = weights
    if periodic:
        value["periodic"] = True
    start, end = tuple(points[0]), tuple(points[-1])
    samples = [tuple(point) for point in points]
    if bool(flags & 1) or periodic:
        samples.append(samples[0])
    return _Segment(value, start, end, samples), bool(flags & 1) or periodic


def _reverse_segment(segment: _Segment) -> _Segment:
    value = dict(segment.value)
    kind = value["kind"]
    if kind == "line":
        value["start"], value["end"] = value["end"], value["start"]
    elif kind == "arc":
        value["start"], value["end"] = value["end"], value["start"]
    elif kind in ("bspline", "nurbs"):
        value["controlPoints"] = list(reversed(value["controlPoints"]))
        if "weights" in value:
            value["weights"] = list(reversed(value["weights"]))
        knots = list(value["knots"])
        if knots:
            total = knots[0] + knots[-1]
            value["knots"] = [total - knot for knot in reversed(knots)]
    else:
        raise ValueError(f"DXF 曲线 {kind} 无法反向连接")
    return _Segment(value, segment.end, segment.start, list(reversed(segment.samples)))


def _loop(segments: list[_Segment]) -> _Loop:
    samples: list[tuple[float, float]] = []
    for segment in segments:
        samples.extend(segment.samples if not samples else segment.samples[1:])
    if samples and math.dist(samples[0], samples[-1]) > 1.0e-12:
        samples.append(samples[0])
    return _Loop(segments, samples)


def _join_segments(segments: list[_Segment], tolerance: float) -> list[_Loop]:
    remaining = list(segments)
    loops: list[_Loop] = []
    while remaining:
        chain = [remaining.pop(0)]
        while math.dist(chain[-1].end, chain[0].start) > tolerance:
            current = chain[-1].end
            match_index = -1
            reverse = False
            for index, candidate in enumerate(remaining):
                if math.dist(current, candidate.start) <= tolerance:
                    match_index = index
                    break
                if math.dist(current, candidate.end) <= tolerance:
                    match_index, reverse = index, True
                    break
            if match_index < 0:
                raise ValueError("DXF 截面包含开口或断开的轮廓")
            candidate = remaining.pop(match_index)
            chain.append(_reverse_segment(candidate) if reverse else candidate)
            if len(chain) > 10000:
                raise ValueError("DXF 单条轮廓的曲线段过多")
        loops.append(_loop(chain))
    return loops


def _signed_area(points: list[tuple[float, float]]) -> float:
    return sum(left[0] * right[1] - right[0] * left[1]
               for left, right in zip(points, points[1:])) / 2.0


def _contains(points: list[tuple[float, float]], point: tuple[float, float]) -> bool:
    inside = False
    x, y = point
    for left, right in zip(points, points[1:]):
        if (left[1] > y) != (right[1] > y):
            crossing = (right[0] - left[0]) * (y - left[1]) / (right[1] - left[1]) + left[0]
            if x < crossing:
                inside = not inside
    return inside


def _point_segment_distance(
    point: tuple[float, float], start: tuple[float, float], end: tuple[float, float],
) -> float:
    dx, dy = end[0] - start[0], end[1] - start[1]
    squared = dx * dx + dy * dy
    if squared <= 1.0e-24:
        return math.dist(point, start)
    ratio = max(0.0, min(1.0, ((point[0] - start[0]) * dx + (point[1] - start[1]) * dy) / squared))
    return math.dist(point, (start[0] + dx * ratio, start[1] + dy * ratio))


def _approximate_wall(outer: _Loop, holes: list[_Loop]) -> float:
    if not holes:
        return 0.0
    best = math.inf
    for hole in holes:
        for point in hole.samples[:-1]:
            for start, end in zip(outer.samples, outer.samples[1:]):
                best = min(best, _point_segment_distance(point, start, end))
        for point in outer.samples[:-1]:
            for start, end in zip(hole.samples, hole.samples[1:]):
                best = min(best, _point_segment_distance(point, start, end))
    return 0.0 if not math.isfinite(best) else best


def _transform_point(point: list[float], center: tuple[float, float], scale: float) -> list[float]:
    return [(float(point[0]) - center[0]) * scale, (float(point[1]) - center[1]) * scale]


def _transform_segment(value: dict[str, Any], center: tuple[float, float], scale: float) -> dict[str, Any]:
    result = dict(value)
    for key in ("start", "middle", "end", "center"):
        if key in result:
            result[key] = _transform_point(result[key], center, scale)
    if "controlPoints" in result:
        result["controlPoints"] = [_transform_point(point, center, scale)
                                   for point in result["controlPoints"]]
    for key in ("radius", "majorRadius", "minorRadius"):
        if key in result:
            result[key] = float(result[key]) * scale
    return result


def _units(header: list[tuple[int, str]]) -> tuple[int, float, str]:
    for index, (code, value) in enumerate(header):
        if code == 9 and value.strip().upper() == "$INSUNITS":
            for next_code, next_value in header[index + 1:index + 8]:
                if next_code in (70, 280):
                    unit_code = _integer(next_value, "$INSUNITS")
                    scale, label = _UNIT_SCALE_TO_MM.get(unit_code, (1.0, f"未知单位 {unit_code}（按毫米）"))
                    return unit_code, scale, label
    return 0, 1.0, _UNIT_SCALE_TO_MM[0][1]


def _parse(path: Path) -> dict[str, Any]:
    pairs = _read_pairs(path)
    header = _section_pairs(pairs, "HEADER")
    records = _records(_section_pairs(pairs, "ENTITIES"))
    if not records:
        raise ValueError("DXF 模型空间没有二维实体")

    closed_loops: list[_Loop] = []
    loose: list[_Segment] = []
    ignored: set[str] = set()
    index = 0
    while index < len(records):
        kind, values = records[index]
        if not _entity_is_model_space(values):
            index += 1
            continue
        if kind == "LINE":
            loose.append(_line(_point(values), _point(values, 11, 21)))
        elif kind == "ARC":
            center = _point(values)
            radius = _number(_first(values, 40), "40")
            start = math.radians(_number(_first(values, 50), "50"))
            end = math.radians(_number(_first(values, 51), "51"))
            sweep = (end - start) % _TAU
            loose.append(_arc_from_center(center, radius, start, sweep))
        elif kind == "CIRCLE":
            center = _point(values)
            radius = _number(_first(values, 40), "40")
            closed_loops.append(_loop(_rational_ellipse_segments(
                center, (radius, 0.0), 1.0, 0.0, _TAU)))
        elif kind == "ELLIPSE":
            center = _point(values)
            major = _point(values, 11, 21)
            ratio = _number(_first(values, 40), "40")
            start = _number(_first(values, 41, "0"), "41")
            end = _number(_first(values, 42, str(_TAU)), "42")
            sweep = (end - start) % _TAU
            if sweep <= 1.0e-10:
                sweep = _TAU
            segments = _rational_ellipse_segments(center, major, ratio, start, sweep)
            if abs(sweep - _TAU) <= 1.0e-9:
                closed_loops.append(_loop(segments))
            else:
                loose.extend(segments)
        elif kind == "LWPOLYLINE":
            flags = _integer(_first(values, 70, "0"), "70")
            closed = bool(flags & 1)
            segments = _segments_from_vertices(_polyline_vertices(values), closed)
            (closed_loops.append(_loop(segments)) if closed else loose.extend(segments))
        elif kind == "POLYLINE":
            flags = _integer(_first(values, 70, "0"), "70")
            if flags & (8 | 16 | 32 | 64):
                raise ValueError("DXF 截面不支持 3D、多边形网格或多面体 POLYLINE")
            vertices: list[tuple[float, float, float]] = []
            cursor = index + 1
            while cursor < len(records) and records[cursor][0] == "VERTEX":
                vertex_values = records[cursor][1]
                vertex = _point(vertex_values)
                vertices.append((vertex[0], vertex[1], _number(_first(vertex_values, 42, "0"), "42")))
                cursor += 1
            if cursor >= len(records) or records[cursor][0] != "SEQEND":
                raise ValueError("DXF POLYLINE 缺少 SEQEND")
            closed = bool(flags & 1)
            segments = _segments_from_vertices(vertices, closed)
            (closed_loops.append(_loop(segments)) if closed else loose.extend(segments))
            index = cursor
        elif kind == "SPLINE":
            segment, closed = _spline(values)
            (closed_loops.append(_loop([segment])) if closed else loose.append(segment))
        elif kind not in ("VERTEX", "SEQEND"):
            ignored.add(kind)
        index += 1

    all_samples = [point for loop in closed_loops for point in loop.samples]
    all_samples.extend(point for segment in loose for point in segment.samples)
    if not all_samples:
        supported = "LINE、LWPOLYLINE/POLYLINE、ARC、CIRCLE、ELLIPSE、SPLINE"
        raise ValueError(f"DXF 没有可用截面实体；当前支持 {supported}")
    min_x = min(point[0] for point in all_samples)
    max_x = max(point[0] for point in all_samples)
    min_y = min(point[1] for point in all_samples)
    max_y = max(point[1] for point in all_samples)
    diagonal = math.hypot(max_x - min_x, max_y - min_y)
    tolerance = max(1.0e-7, diagonal * 1.0e-8)
    closed_loops.extend(_join_segments(loose, tolerance))
    if not closed_loops:
        raise ValueError("DXF 没有闭合截面")

    ranked = sorted(closed_loops, key=lambda loop: abs(_signed_area(loop.samples)), reverse=True)
    outer, holes = ranked[0], ranked[1:]
    if abs(_signed_area(outer.samples)) <= tolerance * tolerance:
        raise ValueError("DXF 外轮廓面积为零")
    for hole in holes:
        probe = hole.samples[0]
        if not _contains(outer.samples, probe):
            raise ValueError("DXF 包含多个互不相连的外轮廓；一个管型只能有一个外轮廓")

    unit_code, scale, unit_label = _units(header)
    center = ((min_x + max_x) / 2.0, (min_y + max_y) / 2.0)
    ordered = [outer, *holes]
    contours = [{
        "kind": "path",
        "segments": [_transform_segment(segment.value, center, scale) for segment in loop.segments],
    } for loop in ordered]
    width = (max_x - min_x) * scale
    depth = (max_y - min_y) * scale
    if width <= 1.0e-6 or depth <= 1.0e-6:
        raise ValueError("DXF 截面的外包尺寸无效")
    wall = _approximate_wall(outer, holes) * scale
    canonical = json.dumps(contours, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
    digest = hashlib.sha256(canonical.encode("utf-8")).hexdigest()
    number = lambda value: f"{value:.6f}".rstrip("0").rstrip(".")
    result = {
        "schema": "icax.imported-tube-profile",
        "schemaVersion": 1,
        "kind": "imported-dxf",
        "name": path.stem,
        "sourceFileName": path.name,
        "sourceFormat": "cad.dxf",
        "sourceUnitCode": unit_code,
        "sourceUnit": unit_label,
        "unitScaleToMillimeter": scale,
        "width": width,
        "depth": depth,
        "wallThickness": wall,
        "cornerRadius": 0.0,
        "wallThicknessApproximate": bool(holes),
        "specification": f"DXF {number(width)} × {number(depth)} mm",
        "hollow": bool(holes),
        "contourCount": len(contours),
        "contours": contours,
        "contentDigest": f"sha256:{digest}",
    }
    if ignored:
        result["ignoredEntityTypes"] = sorted(ignored)
    return result


def generate(parameters: dict[str, Any], context: dict[str, Any]) -> dict[str, Any]:
    del context
    source_path = parameters.get("sourcePath")
    if not isinstance(source_path, str) or not source_path.strip():
        raise ValueError("缺少 DXF 文件路径")
    path = Path(source_path).expanduser().resolve()
    if path.suffix.lower() != ".dxf":
        raise ValueError("请选择 .dxf 文件")
    if not path.is_file():
        raise FileNotFoundError(f"DXF 文件不存在：{path}")
    return {"profile": _parse(path)}
