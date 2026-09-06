from __future__ import annotations

import math
from pathlib import Path
from typing import Any, Iterable


def _number(value: Any, name: str) -> float:
    result = float(value)
    if not math.isfinite(result):
        raise ValueError(f"{name} 不是有限数值")
    return result


def _point(value: Any, name: str) -> tuple[float, float]:
    if not isinstance(value, (list, tuple)) or len(value) < 2:
        raise ValueError(f"{name} 不是二维点")
    return _number(value[0], name), _number(value[1], name)


def _fmt(value: float) -> str:
    text = f"{value:.12g}"
    return "0" if text in ("-0", "-0.0") else text


class _Writer:
    def __init__(self) -> None:
        self.lines: list[str] = [
            "0", "SECTION", "2", "HEADER",
            "9", "$ACADVER", "1", "AC1027",
            "9", "$INSUNITS", "70", "4",
            "0", "ENDSEC", "0", "SECTION", "2", "ENTITIES",
        ]
        self.count = 0

    def add(self, kind: str, pairs: Iterable[tuple[int, Any]]) -> None:
        self.lines.extend(("0", kind, "8", "0"))
        for code, value in pairs:
            self.lines.extend((str(code), _fmt(value) if isinstance(value, float) else str(value)))
        self.count += 1

    def polyline(self, vertices: list[tuple[float, float, float]]) -> None:
        pairs: list[tuple[int, Any]] = [(90, len(vertices)), (70, 1)]
        for x, y, bulge in vertices:
            pairs.extend(((10, x), (20, y)))
            if abs(bulge) > 1.0e-14:
                pairs.append((42, bulge))
        self.add("LWPOLYLINE", pairs)

    def finish(self, path: Path) -> None:
        self.lines.extend(("0", "ENDSEC", "0", "EOF"))
        path.write_text("\n".join(self.lines) + "\n", encoding="ascii")


def _circle(writer: _Writer, radius: float) -> None:
    writer.add("CIRCLE", ((10, 0.0), (20, 0.0), (30, 0.0), (40, radius)))


def _ellipse(writer: _Writer, width: float, height: float) -> None:
    rx, ry = width / 2.0, height / 2.0
    if rx >= ry:
        major_x, major_y, ratio = rx, 0.0, ry / rx
    else:
        major_x, major_y, ratio = 0.0, ry, rx / ry
    writer.add("ELLIPSE", (
        (10, 0.0), (20, 0.0), (30, 0.0),
        (11, major_x), (21, major_y), (31, 0.0),
        (40, ratio), (41, 0.0), (42, math.tau),
    ))


def _rounded_rectangle(writer: _Writer, width: float, height: float, radius: float) -> None:
    hw, hh = width / 2.0, height / 2.0
    radius = min(max(0.0, radius), hw, hh)
    if radius <= 1.0e-12:
        writer.polyline([(-hw, -hh, 0.0), (hw, -hh, 0.0), (hw, hh, 0.0), (-hw, hh, 0.0)])
        return
    bulge = math.tan(math.pi / 8.0)
    writer.polyline([
        (-hw + radius, -hh, 0.0), (hw - radius, -hh, bulge),
        (hw, -hh + radius, 0.0), (hw, hh - radius, bulge),
        (hw - radius, hh, 0.0), (-hw + radius, hh, bulge),
        (-hw, hh - radius, 0.0), (-hw, -hh + radius, bulge),
    ])


def _capsule(writer: _Writer, width: float, height: float) -> None:
    if abs(width - height) <= 1.0e-12:
        _circle(writer, width / 2.0)
    elif width > height:
        radius, straight = height / 2.0, (width - height) / 2.0
        writer.polyline([
            (-straight, -radius, 0.0), (straight, -radius, 1.0),
            (straight, radius, 0.0), (-straight, radius, 1.0),
        ])
    else:
        radius, straight = width / 2.0, (height - width) / 2.0
        writer.polyline([
            (-radius, -straight, 1.0), (radius, -straight, 0.0),
            (radius, straight, 1.0), (-radius, straight, 0.0),
        ])


def _arc(writer: _Writer, segment: dict[str, Any]) -> None:
    start = _point(segment.get("start"), "arc.start")
    middle = _point(segment.get("middle"), "arc.middle")
    end = _point(segment.get("end"), "arc.end")
    x1, y1 = start
    x2, y2 = middle
    x3, y3 = end
    determinant = 2.0 * (x1 * (y2 - y3) + x2 * (y3 - y1) + x3 * (y1 - y2))
    if abs(determinant) <= 1.0e-12:
        raise ValueError("圆弧三点共线")
    ux = ((x1 * x1 + y1 * y1) * (y2 - y3)
          + (x2 * x2 + y2 * y2) * (y3 - y1)
          + (x3 * x3 + y3 * y3) * (y1 - y2)) / determinant
    uy = ((x1 * x1 + y1 * y1) * (x3 - x2)
          + (x2 * x2 + y2 * y2) * (x1 - x3)
          + (x3 * x3 + y3 * y3) * (x2 - x1)) / determinant
    radius = math.hypot(x1 - ux, y1 - uy)
    cross = (x2 - x1) * (y3 - y2) - (y2 - y1) * (x3 - x2)
    first, last = (start, end) if cross > 0.0 else (end, start)
    angle = lambda point: math.degrees(math.atan2(point[1] - uy, point[0] - ux)) % 360.0
    writer.add("ARC", (
        (10, ux), (20, uy), (30, 0.0), (40, radius),
        (50, angle(first)), (51, angle(last)),
    ))


def _ellipse_arc(writer: _Writer, segment: dict[str, Any]) -> None:
    center = _point(segment.get("center"), "ellipseArc.center")
    major = _number(segment.get("majorRadius"), "ellipseArc.majorRadius")
    minor = _number(segment.get("minorRadius"), "ellipseArc.minorRadius")
    rotation = _number(segment.get("rotation", 0.0), "ellipseArc.rotation")
    writer.add("ELLIPSE", (
        (10, center[0]), (20, center[1]), (30, 0.0),
        (11, major * math.cos(rotation)), (21, major * math.sin(rotation)), (31, 0.0),
        (40, minor / major),
        (41, _number(segment.get("startAngle"), "ellipseArc.startAngle")),
        (42, _number(segment.get("endAngle"), "ellipseArc.endAngle")),
    ))


def _spline(writer: _Writer, segment: dict[str, Any], bezier: bool = False) -> None:
    points = [_point(value, "spline.controlPoints") for value in segment.get("controlPoints", [])]
    if bezier:
        degree = len(points) - 1
        knots = [0.0] * (degree + 1) + [1.0] * (degree + 1)
        weights: list[float] = []
        periodic = False
    else:
        degree = int(segment.get("degree", 0))
        raw_knots = [_number(value, "spline.knots") for value in segment.get("knots", [])]
        multiplicities = segment.get("multiplicities")
        knots = ([knot for knot, count in zip(raw_knots, multiplicities)
                  for _ in range(int(count))] if isinstance(multiplicities, list) else raw_knots)
        weights = [_number(value, "spline.weights") for value in segment.get("weights", [])]
        periodic = bool(segment.get("periodic", False))
    if degree < 1 or len(points) < degree + 1 or len(knots) < 2:
        raise ValueError("样条曲线数据不完整")
    flags = 8 | (3 if periodic else 0) | (4 if weights else 0)
    pairs: list[tuple[int, Any]] = [
        (70, flags), (71, degree), (72, len(knots)), (73, len(points)), (74, 0),
    ]
    pairs.extend((40, value) for value in knots)
    pairs.extend((41, value) for value in weights)
    for x, y in points:
        pairs.extend(((10, x), (20, y), (30, 0.0)))
    writer.add("SPLINE", pairs)


def _path(writer: _Writer, contour: dict[str, Any]) -> None:
    for segment in contour.get("segments", []):
        kind = str(segment.get("kind", ""))
        if kind == "line":
            start, end = _point(segment.get("start"), "line.start"), _point(segment.get("end"), "line.end")
            writer.add("LINE", ((10, start[0]), (20, start[1]), (30, 0.0),
                                (11, end[0]), (21, end[1]), (31, 0.0)))
        elif kind == "arc":
            _arc(writer, segment)
        elif kind == "ellipseArc":
            _ellipse_arc(writer, segment)
        elif kind == "bezier":
            _spline(writer, segment, True)
        elif kind in ("bspline", "nurbs"):
            _spline(writer, segment)
        else:
            raise ValueError(f"不支持导出的路径段：{kind}")


def _contour(writer: _Writer, contour: dict[str, Any]) -> None:
    kind = str(contour.get("kind", ""))
    if kind == "circle":
        _circle(writer, _number(contour.get("radius"), "circle.radius"))
    elif kind == "ellipse":
        _ellipse(writer, _number(contour.get("width"), "ellipse.width"),
                 _number(contour.get("height"), "ellipse.height"))
    elif kind == "roundedRectangle":
        _rounded_rectangle(writer, _number(contour.get("width"), "rectangle.width"),
                           _number(contour.get("height"), "rectangle.height"),
                           _number(contour.get("radius", 0.0), "rectangle.radius"))
    elif kind == "capsule":
        _capsule(writer, _number(contour.get("width"), "capsule.width"),
                 _number(contour.get("height"), "capsule.height"))
    elif kind == "polygon":
        points = [_point(value, "polygon.points") for value in contour.get("points", [])]
        if len(points) < 3:
            raise ValueError("多边形至少需要三个点")
        writer.polyline([(x, y, 0.0) for x, y in points])
    elif kind == "path":
        _path(writer, contour)
    else:
        raise ValueError(f"不支持导出的轮廓：{kind}")


def generate(parameters: dict[str, Any], context: dict[str, Any]) -> dict[str, Any]:
    del context
    profile = parameters.get("profile")
    target_path = parameters.get("targetPath")
    if not isinstance(profile, dict):
        raise ValueError("缺少管型截面")
    if not isinstance(target_path, str) or not target_path.strip():
        raise ValueError("缺少 DXF 导出路径")
    path = Path(target_path).expanduser().resolve()
    if path.suffix.lower() != ".dxf":
        raise ValueError("DXF 导出路径必须以 .dxf 结尾")
    path.parent.mkdir(parents=True, exist_ok=True)
    contours = profile.get("contours")
    if not isinstance(contours, list) or not contours:
        raise ValueError("管型没有可导出的轮廓")
    writer = _Writer()
    for contour in contours:
        if not isinstance(contour, dict):
            raise ValueError("管型轮廓格式无效")
        _contour(writer, contour)
    writer.finish(path)
    return {"path": str(path), "entityCount": writer.count}
