from __future__ import annotations

from dataclasses import dataclass
import hashlib
import importlib.util
import math
from pathlib import Path
import sys
from typing import Any, Iterable

from icax_template_sdk import NeutralModel


Point = tuple[float, float, float]
Vector = tuple[float, float, float]

PROFILE_CATALOG_SCRIPT = Path(__file__).resolve().parent / "tube_profile_catalog.py"
PROFILE_CATALOG_MODULE = "icax_tube_profile_catalog_" + hashlib.sha256(
    PROFILE_CATALOG_SCRIPT.read_bytes()
).hexdigest()[:16]
if PROFILE_CATALOG_MODULE in sys.modules:
    _profile_catalog = sys.modules[PROFILE_CATALOG_MODULE]
else:
    _profile_catalog_spec = importlib.util.spec_from_file_location(
        PROFILE_CATALOG_MODULE, PROFILE_CATALOG_SCRIPT,
    )
    if _profile_catalog_spec is None or _profile_catalog_spec.loader is None:
        raise RuntimeError(f"无法加载管型目录：{PROFILE_CATALOG_SCRIPT}")
    _profile_catalog = importlib.util.module_from_spec(_profile_catalog_spec)
    sys.modules[PROFILE_CATALOG_MODULE] = _profile_catalog
    _profile_catalog_spec.loader.exec_module(_profile_catalog)
Profile = _profile_catalog.Profile

SHARED_GEOMETRY_SCRIPT = Path(__file__).resolve().parent / "shared_tube_geometry.py"
SHARED_GEOMETRY_MODULE = "icax_shared_tube_geometry_" + hashlib.sha256(
    SHARED_GEOMETRY_SCRIPT.read_bytes()
).hexdigest()[:16]
if SHARED_GEOMETRY_MODULE not in sys.modules:
    _shared_geometry_spec = importlib.util.spec_from_file_location(
        SHARED_GEOMETRY_MODULE, SHARED_GEOMETRY_SCRIPT,
    )
    if _shared_geometry_spec is None or _shared_geometry_spec.loader is None:
        raise RuntimeError("无法加载共享管体声明规则")
    _shared_geometry_module = importlib.util.module_from_spec(_shared_geometry_spec)
    sys.modules[SHARED_GEOMETRY_MODULE] = _shared_geometry_module
    _shared_geometry_spec.loader.exec_module(_shared_geometry_module)
SharedTubeGeometry = sys.modules[SHARED_GEOMETRY_MODULE].SharedTubeGeometry
request_geometry_purpose = sys.modules[SHARED_GEOMETRY_MODULE].request_geometry_purpose
finish_geometry_request = sys.modules[SHARED_GEOMETRY_MODULE].finish_geometry_request

REVIEW_RULES_SCRIPT = Path(__file__).resolve().parent / "security_window_rules.py"
REVIEW_RULES_MODULE = "tube_designer_security_window_rules_" + hashlib.sha256(
    REVIEW_RULES_SCRIPT.read_bytes()
).hexdigest()[:16]
if REVIEW_RULES_MODULE not in sys.modules:
    _review_spec = importlib.util.spec_from_file_location(REVIEW_RULES_MODULE, REVIEW_RULES_SCRIPT)
    if _review_spec is None or _review_spec.loader is None:
        raise RuntimeError("无法加载防盗窗设计规则")
    _review_module = importlib.util.module_from_spec(_review_spec)
    sys.modules[REVIEW_RULES_MODULE] = _review_module
    _review_spec.loader.exec_module(_review_module)
generate_reviewed = sys.modules[REVIEW_RULES_MODULE].generate_reviewed

FRAME_SCRIPT = Path(__file__).resolve().parent / "security_window_frame_geometry.py"
FRAME_MODULE = "icax_security_window_frames_" + hashlib.sha256(FRAME_SCRIPT.read_bytes()).hexdigest()[:16]
if FRAME_MODULE not in sys.modules:
    _frame_spec = importlib.util.spec_from_file_location(FRAME_MODULE, FRAME_SCRIPT)
    if _frame_spec is None or _frame_spec.loader is None:
        raise RuntimeError("无法加载共用窗框加工规则")
    _frame_module = importlib.util.module_from_spec(_frame_spec)
    sys.modules[FRAME_MODULE] = _frame_module
    _frame_spec.loader.exec_module(_frame_module)
_frame_geometry = sys.modules[FRAME_MODULE]


@dataclass(frozen=True)
class Part:
    key: str
    name: str
    start: Point
    end: Point
    profile: Profile
    profile_x_axis: Vector
    profile_y_axis: Vector
    category_key: str
    category_name: str
    face_index: int = 0
    face_name: str = ""
    # Slopes of the end planes in (stock length, horizontal transverse) space.
    # The stock starts/ends at the outside long points, not the corner centers.
    start_miter: float | None = None
    end_miter: float | None = None

    @property
    def length(self) -> float:
        return math.dist(self.start, self.end)


@dataclass(frozen=True)
class Face:
    index: int
    name: str
    start: Point
    end: Point

    @property
    def vector(self) -> Vector:
        return _subtract(self.end, self.start)

    @property
    def length(self) -> float:
        return math.dist(self.start, self.end)

    @property
    def direction(self) -> Vector:
        return _normalize(self.vector)

    @property
    def normal(self) -> Vector:
        direction = self.direction
        return (-direction[1], direction[0], 0.0)


@dataclass(frozen=True)
class DoorSurface:
    face_index: int
    face_name: str
    origin: Point
    u_axis: Vector
    v_axis: Vector
    normal: Vector
    u_length: float
    v_length: float


def _number(parameters: dict[str, Any], key: str) -> float:
    value = parameters[key]
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{key} 必须是数值")
    result = float(value)
    if not math.isfinite(result):
        raise ValueError(f"{key} 必须是有限数值")
    return result


def _integer(parameters: dict[str, Any], key: str) -> int:
    value = parameters[key]
    if isinstance(value, bool) or not isinstance(value, int):
        raise ValueError(f"{key} 必须是整数")
    return value


def _profile(parameters: dict[str, Any], prefix: str) -> Profile:
    return _profile_catalog.load_profile(parameters, prefix)


def _add(left: Point, right: Vector) -> Point:
    return tuple(left[index] + right[index] for index in range(3))  # type: ignore[return-value]


def _subtract(left: Point, right: Point) -> Vector:
    return tuple(left[index] - right[index] for index in range(3))  # type: ignore[return-value]


def _scale(vector: Vector, factor: float) -> Vector:
    return tuple(value * factor for value in vector)  # type: ignore[return-value]


def _normalize(vector: Vector) -> Vector:
    length = math.sqrt(sum(value * value for value in vector))
    if length <= 1.0e-9:
        raise ValueError("无法归一化零向量")
    return tuple(value / length for value in vector)  # type: ignore[return-value]


def _dot(left: Vector, right: Vector) -> float:
    return sum(left[index] * right[index] for index in range(3))


def _point2(x: float, y: float) -> Point:
    return (float(x), float(y), 0.0)


def _centerline_footprint(layout: str, parameters: dict[str, Any]) -> tuple[list[Point], list[str]]:
    front = _number(parameters, "frontWidth")
    if layout == "two-face":
        side = _number(parameters, "sideWidth")
        if parameters["sidePosition"] == "left":
            return [
                _point2(-front / 2, -side), _point2(-front / 2, 0), _point2(front / 2, 0),
            ], ["左侧面", "正面"]
        if parameters["sidePosition"] == "right":
            return [
                _point2(-front / 2, 0), _point2(front / 2, 0), _point2(front / 2, -side),
            ], ["正面", "右侧面"]
        raise ValueError("sidePosition 仅支持 left 或 right")
    if layout == "three-face":
        left = _number(parameters, "leftWidth")
        right = _number(parameters, "rightWidth")
        return [
            _point2(-front / 2, -left), _point2(-front / 2, 0),
            _point2(front / 2, 0), _point2(front / 2, -right),
        ], ["左侧面", "正面", "右侧面"]
    if layout == "five-face":
        depth = _number(parameters, "depth")
        return [
            _point2(-front / 2, -depth), _point2(-front / 2, 0),
            _point2(front / 2, 0), _point2(front / 2, -depth),
        ], ["左侧面", "正面", "右侧面"]
    raise ValueError(f"不支持的多面防盗窗布局：{layout}")


def _footprint(layout: str, parameters: dict[str, Any]) -> tuple[list[Point], list[str]]:
    points, names = _centerline_footprint(layout, parameters)
    # Product dimensions always describe the installed frame's outside envelope.
    frame = _profile(parameters, "frame")
    faces = [Face(index + 1, names[index], points[index], points[index + 1])
             for index in range(len(names))]
    extents = []
    for index in range(len(points)):
        axis = _vertex_profile_axis(faces, index)
        normal = (-axis[1], axis[0], 0.0)
        extents.append(tuple(abs(axis[i]) * frame.depth / 2
                             + abs(normal[i]) * frame.width / 2 for i in (0, 1)))
    left_pad = max(frame.depth / 2, *(extent[0] for point, extent in zip(points, extents) if point[0] < 0))
    right_pad = max(frame.depth / 2, *(extent[0] for point, extent in zip(points, extents) if point[0] > 0))
    front_pad = max(frame.depth / 2, *(extent[1] for point, extent in zip(points, extents) if point[1] == 0))
    adjusted = []
    for point, extent in zip(points, extents):
        x = point[0] + left_pad if point[0] < 0 else point[0] - right_pad
        back_pad = max(extent[1], frame.width / 2) if layout == "five-face" else extent[1]
        y = -front_pad if point[1] == 0 else point[1] + back_pad
        adjusted.append(_point2(x, y))
    if adjusted[-1][0] <= adjusted[0][0]:
        raise ValueError("正面外包宽度不足以容纳外框")
    for index, name in enumerate(names):
        original = _subtract(points[index + 1], points[index])
        current = _subtract(adjusted[index + 1], adjusted[index])
        if _dot(original, current) <= 0:
            raise ValueError(f"{name}外包尺寸不足以容纳外框")
    return adjusted, names


def _surface_point(surface: DoorSurface, u: float, v: float) -> Point:
    return _add(
        _add(surface.origin, _scale(surface.u_axis, u)),
        _scale(surface.v_axis, v),
    )


def _resolve_door_surface(
    layout: str, parameters: dict[str, Any], points: list[Point], face_names: list[str],
    frame: Profile,
) -> DoorSurface | None:
    if not bool(parameters.get("accessDoorEnabled", False)):
        return None

    requested = str(parameters.get("accessDoorFace", "front"))
    face_code = requested
    if layout == "two-face" and requested == "side":
        face_code = "left" if parameters.get("sidePosition") == "left" else "right"
    allowed = {
        "two-face": {"front", "side"},
        "three-face": {"left", "front", "right"},
        "five-face": {"left", "front", "right", "top", "bottom"},
    }[layout]
    if requested not in allowed:
        raise ValueError("所选逃生窗安装面不属于当前产品")

    names = {
        "left": "左侧面", "front": "正面", "right": "右侧面",
        "top": "上面", "bottom": "下面",
    }
    face_name = names[face_code]
    if face_code in {"left", "front", "right"}:
        try:
            offset = face_names.index(face_name)
        except ValueError as error:
            raise ValueError("所选逃生窗安装面不属于当前产品") from error
        face = Face(offset + 1, face_name, points[offset], points[offset + 1])
        return DoorSurface(
            face.index, face.name, face.start, face.direction, (0.0, 0.0, 1.0),
            face.normal, face.length, _number(parameters, "height"),
        )

    front_left, front_right, back_left = points[1], points[2], points[0]
    width_axis = _normalize(_subtract(front_right, front_left))
    depth_axis = _normalize(_subtract(back_left, front_left))
    z = (_number(parameters, "height") - frame.width / 2
         if face_code == "top" else frame.width / 2)
    return DoorSurface(
        4 if face_code == "top" else 5,
        face_name,
        (front_left[0], front_left[1], z),
        width_axis,
        depth_axis,
        (0.0, 0.0, 1.0 if face_code == "top" else -1.0),
        math.dist(front_left, front_right),
        math.dist(front_left, back_left),
    )


def _door_bounds(parameters: dict[str, Any]) -> tuple[float, float, float, float]:
    u_min = _number(parameters, "doorUOffset")
    v_min = _number(parameters, "doorVOffset")
    return (
        u_min, v_min,
        u_min + _number(parameters, "doorWidth"),
        v_min + _number(parameters, "doorHeight"),
    )


def _door_construction_parameters(
    parameters: dict[str, Any], surface: DoorSurface | None,
    points: list[Point], names: list[str], frame: Profile,
) -> dict[str, Any]:
    """Convert outside-edge UI offsets to the local frame-center coordinates.

    Only this private copy uses centerline coordinates. The input parameters and
    saved model retain the dimensions and offsets that the user actually entered.
    """
    result = dict(parameters)
    result.setdefault("sideHorizontalCount", 4)
    result.setdefault("sideVerticalCount", 4)
    result.setdefault("sideMaximumVerticalClearGap", 110.0)
    if surface is None:
        return result
    faces = [Face(index + 1, names[index], points[index], points[index + 1])
             for index in range(len(names))]
    fixed_half = _profile(parameters, "doorFrame").width / 2
    if surface.face_index <= len(faces):
        face = faces[surface.face_index - 1]
        axis = _vertex_profile_axis(faces, surface.face_index - 1)
        normal = (-axis[1], axis[0], 0.0)
        start_pad = abs(_dot(axis, face.direction)) * frame.depth / 2 + abs(_dot(normal, face.direction)) * frame.width / 2
        result["doorUOffset"] = _number(parameters, "doorUOffset") - start_pad + fixed_half
        result["doorVOffset"] = _number(parameters, "doorVOffset") + fixed_half
    else:
        # Cap coordinates begin at the front-left post center, whereas the UI
        # measures from the finished frame's left/front outside edges.
        left_pad = _number(parameters, "frontWidth") / 2 + points[1][0]
        front_pad = -points[1][1]
        result["doorUOffset"] = _number(parameters, "doorUOffset") - left_pad + fixed_half
        result["doorVOffset"] = _number(parameters, "doorVOffset") - front_pad + fixed_half
    return result


def _door_surface_clear_bounds(
    surface: DoorSurface, points: list[Point], frame: Profile,
) -> tuple[float, float, float, float]:
    if surface.face_index > len(points) - 1:
        return frame.depth / 2, frame.depth / 2, surface.u_length - frame.depth / 2, surface.v_length - frame.width / 2
    faces = [Face(index + 1, "", points[index], points[index + 1]) for index in range(len(points) - 1)]
    extents = []
    for index in (surface.face_index - 1, surface.face_index):
        axis = _vertex_profile_axis(faces, index)
        normal = (-axis[1], axis[0], 0.0)
        extents.append(abs(_dot(axis, surface.u_axis)) * frame.depth / 2
                       + abs(_dot(normal, surface.u_axis)) * frame.width / 2)
    return extents[0], frame.width, surface.u_length - extents[1], surface.v_length - frame.width


def _even_positions(count: int, minimum: float, maximum: float) -> list[float]:
    if count == 0:
        return []
    if maximum <= minimum:
        raise ValueError("杆件布置没有可用空间")
    step = (maximum - minimum) / (count + 1)
    return [minimum + step * (index + 1) for index in range(count)]


def _validate_positions(
    positions: list[float], minimum: float, maximum: float, width: float, label: str,
) -> None:
    ordered = sorted(positions)
    if any(position - width / 2 < minimum - 1.0e-7
           or position + width / 2 > maximum + 1.0e-7 for position in ordered):
        raise ValueError(f"{label}超出外框内侧的可用范围，请调整数量或边距")
    if any(right - left <= width + 1.0e-7 for left, right in zip(ordered, ordered[1:])):
        raise ValueError(f"{label}布置过密，杆件之间必须保留净间隙")


def _count_for_maximum_clear_gap(
    minimum: float, maximum: float, bar_width: float, maximum_gap: float,
) -> int:
    span = maximum - minimum
    if span <= 0:
        raise ValueError("竿件布置没有可用空间")
    count = max(0, math.ceil(span / (maximum_gap + bar_width / 2) - 1 - 1.0e-9))
    if count > 100:
        raise ValueError("按最大净间距计算的竖杆数量超过100")
    return count


def _vertical_count(
    parameters: dict[str, Any], minimum: float, maximum: float,
    bar_width: float, manual_key: str, maximum_gap_key: str = "maximumVerticalClearGap",
) -> int:
    mode = str(parameters.get("verticalLayoutMode", "manual_count"))
    if mode == "maximum_clear_gap":
        return _count_for_maximum_clear_gap(
            minimum, maximum, bar_width,
            _number(parameters, maximum_gap_key),
        )
    if mode == "manual_count":
        return _integer(parameters, manual_key)
    raise ValueError(f"不支持的竖杆布置方式：{mode}")


def _horizontal_positions(
    parameters: dict[str, Any], height: float, count_key: str = "horizontalCount",
) -> list[float]:
    count = _integer(parameters, count_key)
    if count == 0:
        return []
    top = height - _number(parameters, "firstHorizontalTopOffset")
    bottom = _number(parameters, "lastHorizontalBottomOffset")
    if top < bottom:
        raise ValueError("首末横杆偏移没有留下有效布置空间")
    if count == 1:
        return [(top + bottom) / 2]
    step = (top - bottom) / (count - 1)
    return [top - step * index for index in range(count)]


def _next_key(counters: dict[str, int], role: str) -> str:
    counters[role] = counters.get(role, 0) + 1
    return f"{role}.{counters[role]:04d}"


def _append_part(
    parts: list[Part], counters: dict[str, int], role: str, name: str,
    start: Point, end: Point, profile: Profile, profile_x_axis: Vector,
    profile_y_axis: Vector, category_key: str, category_name: str,
    face_index: int = 0, face_name: str = "",
    *, start_miter: float | None = None, end_miter: float | None = None,
) -> Part:
    if math.dist(start, end) <= 1.0e-7:
        raise ValueError(f"{name} 的长度为零")
    part = Part(
        _next_key(counters, role), name, start, end, profile,
        _normalize(profile_x_axis), _normalize(profile_y_axis),
        category_key, category_name, face_index, face_name, start_miter, end_miter,
    )
    parts.append(part)
    return part


def _append_surface_rectangle(
    parts: list[Part], counters: dict[str, int], surface: DoorSurface,
    role_prefix: str, label: str, category_key: str, category_name: str,
    profile: Profile, u_min: float, v_min: float, u_max: float, v_max: float,
) -> dict[str, Part]:
    # Bounds are the outside envelope. The full-length uprights wrap the rails;
    # four square-ended tubes meet at surfaces without overlapping corners.
    if u_max - u_min <= 2 * profile.width or v_max - v_min <= 2 * profile.width:
        raise ValueError(f"{label}尺寸不足以容纳直拼框件")
    left, right = u_min + profile.width / 2, u_max - profile.width / 2
    bottom, top = v_min + profile.width / 2, v_max - profile.width / 2
    result = {
        "bottom": _append_part(
            parts, counters, f"{role_prefix}.bottom", f"{surface.face_name}{label}下边",
            _surface_point(surface, u_min + profile.width, bottom), _surface_point(surface, u_max - profile.width, bottom),
            profile, surface.normal, surface.v_axis, category_key, category_name,
            surface.face_index, surface.face_name,
        ),
        "top": _append_part(
            parts, counters, f"{role_prefix}.top", f"{surface.face_name}{label}上边",
            _surface_point(surface, u_min + profile.width, top), _surface_point(surface, u_max - profile.width, top),
            profile, surface.normal, surface.v_axis, category_key, category_name,
            surface.face_index, surface.face_name,
        ),
        "left": _append_part(
            parts, counters, f"{role_prefix}.left", f"{surface.face_name}{label}左边",
            _surface_point(surface, left, v_min), _surface_point(surface, left, v_max),
            profile, surface.normal, surface.u_axis, category_key, category_name,
            surface.face_index, surface.face_name,
        ),
        "right": _append_part(
            parts, counters, f"{role_prefix}.right", f"{surface.face_name}{label}右边",
            _surface_point(surface, right, v_min), _surface_point(surface, right, v_max),
            profile, surface.normal, surface.u_axis, category_key, category_name,
            surface.face_index, surface.face_name,
        ),
    }
    return result


def _append_access_door(
    parameters: dict[str, Any], surface: DoorSurface, parts: list[Part],
    counters: dict[str, int], crossing_map: dict[str, list[Part]],
) -> tuple[dict[str, Part], dict[str, Part]]:
    u_min, v_min, u_max, v_max = _door_bounds(parameters)
    fixed_profile = _profile(parameters, "doorFrame")
    leaf_profile = _profile(parameters, "doorLeafFrame")
    fixed = _append_surface_rectangle(
        parts, counters, surface, "access_door.fixed_frame", "固定窗框",
        "access_door.fixed_frame", "固定窗框", fixed_profile,
        u_min - fixed_profile.width / 2, v_min - fixed_profile.width / 2,
        u_max + fixed_profile.width / 2, v_max + fixed_profile.width / 2,
    )
    inset = fixed_profile.width / 2 + _number(parameters, "doorGap") + leaf_profile.width / 2
    leaf_u_min, leaf_v_min = u_min + inset, v_min + inset
    leaf_u_max, leaf_v_max = u_max - inset, v_max - inset
    leaf = _append_surface_rectangle(
        parts, counters, surface, "access_door.leaf.frame", "活动窗扇框",
        "access_door.leaf.frame", "活动窗扇框", leaf_profile,
        leaf_u_min - leaf_profile.width / 2, leaf_v_min - leaf_profile.width / 2,
        leaf_u_max + leaf_profile.width / 2, leaf_v_max + leaf_profile.width / 2,
    )

    inner_u_min, inner_u_max = leaf_u_min + leaf_profile.width / 2, leaf_u_max - leaf_profile.width / 2
    inner_v_min, inner_v_max = leaf_v_min + leaf_profile.width / 2, leaf_v_max - leaf_profile.width / 2
    door_horizontal = _profile(parameters, "doorHorizontal")
    door_vertical = _profile(parameters, "doorVertical")
    horizontal_positions = _even_positions(
        _integer(parameters, "doorHorizontalCount"), inner_v_min, inner_v_max,
    )
    vertical_positions = _even_positions(
        _integer(parameters, "doorVerticalCount"), inner_u_min, inner_u_max,
    )
    _validate_positions(horizontal_positions, inner_v_min, inner_v_max,
                        door_horizontal.width, "窗内横杆")
    _validate_positions(vertical_positions, inner_u_min, inner_u_max,
                        door_vertical.width, "窗内竖杆")
    vertical_reserve = _number(parameters, "verticalBranchReserve")
    if vertical_positions and vertical_reserve >= leaf_profile.width - leaf_profile.wall:
        raise ValueError("窗内竖杆入榫深度必须小于窗扇框至对侧内壁的距离")
    horizontals: list[Part] = []
    for index, v in enumerate(horizontal_positions, start=1):
        bar = _append_part(
            parts, counters, "access_door.leaf.horizontal",
            f"{surface.face_name}窗内横杆 {index}",
            _surface_point(surface, inner_u_min, v), _surface_point(surface, inner_u_max, v),
            door_horizontal, surface.normal, surface.v_axis,
            "access_door.leaf.horizontal", "窗内横杆", surface.face_index, surface.face_name,
        )
        horizontals.append(bar)

    for index, u in enumerate(vertical_positions, start=1):
        bar = _append_part(
            parts, counters, "access_door.leaf.vertical",
            f"{surface.face_name}窗内竖杆 {index}",
            _surface_point(surface, u, inner_v_min - vertical_reserve),
            _surface_point(surface, u, inner_v_max + vertical_reserve),
            door_vertical, surface.normal, surface.u_axis,
            "access_door.leaf.vertical", "窗内竖杆", surface.face_index, surface.face_name,
        )
        if vertical_reserve > 0:
            crossing_map.setdefault(leaf["bottom"].key, []).append(bar)
            crossing_map.setdefault(leaf["top"].key, []).append(bar)
        for horizontal in horizontals:
            crossing_map.setdefault(horizontal.key, []).append(bar)
    return fixed, leaf


def _profile_arguments(
    part: Part, *, clearance: float = 0.0, outer_only: bool = False,
) -> dict[str, Any]:
    contours = part.profile.contours(clearance=clearance, swap_axes=True)
    return {
        "placement": {
            "origin": list(part.start),
            "xAxis": list(part.profile_x_axis),
            "yAxis": list(part.profile_y_axis),
        },
        "contours": contours[:1] if outer_only else contours,
    }


def _emit_tube(model: NeutralModel, part: Part, shared_geometry: SharedTubeGeometry) -> str:
    vector = list(_subtract(part.end, part.start))
    return shared_geometry.emit_tube(
        part.key, profile_arguments=_profile_arguments(part),
        extrude_arguments={"vector": vector},
    )


def _horizontal_transverse(part: Part) -> Vector:
    return (part.profile_x_axis if abs(part.profile_x_axis[2]) < 1.0e-7
            else part.profile_y_axis)


def _miter_planes(part: Part) -> list[tuple[str, Point, Vector]]:
    direction = _normalize(_subtract(part.end, part.start))
    transverse = _horizontal_transverse(part)
    half = part.profile.width / 2
    result = []
    if part.start_miter is not None:
        result.append(("start", _add(part.start, _scale(direction, half)),
                       _subtract(direction, _scale(transverse, part.start_miter))))
    if part.end_miter is not None:
        result.append(("end", _add(part.end, _scale(direction, -half)),
                       _add(_scale(direction, -1), _scale(transverse, part.end_miter))))
    return result


def _emit_miter_shape(model: NeutralModel, part: Part, raw: str) -> str:
    """Manufacturing-only trimming of the real hollow, rounded-corner tube."""
    if part.start_miter is None and part.end_miter is None:
        return raw
    direction = _normalize(_subtract(part.end, part.start))
    transverse = _horizontal_transverse(part)
    half, margin = part.profile.width / 2, 1.0
    low, high = -half - margin, half + margin
    tools = []
    for end, slope in (("start", part.start_miter), ("end", part.end_miter)):
        if slope is None:
            continue
        center = half if end == "start" else part.length - half
        outside = -margin * 2 if end == "start" else part.length + margin * 2
        profile = model.geometry(
            f"{part.key}.miter.{end}.profile", "profile2d",
            arguments={
                "placement": {"origin": [part.start[0], part.start[1], part.start[2] - half - margin],
                              "xAxis": list(direction), "yAxis": list(transverse)},
                "contours": [{"kind": "polygon", "points": [
                    [outside, low], [center + slope * low, low],
                    [center + slope * high, high], [outside, high],
                ]}],
            },
        )
        tools.append(model.geometry(
            f"{part.key}.miter.{end}.solid", "extrude", inputs=[profile],
            arguments={"vector": [0.0, 0.0, part.profile.width + margin * 2]},
        ))
    return model.geometry(
        f"{part.key}.solid.miter", "boolean", inputs=[raw, *tools],
        arguments={"operation": "subtract", "target": raw, "tools": tools},
    )


def _emit_crossing_cutter(
    model: NeutralModel, target: Part, inserted: Part, index: int, clearance: float,
) -> str:
    profile = model.geometry(
        f"{target.key}.through.{index:04d}.profile", "profile2d",
        arguments=_profile_arguments(inserted, clearance=clearance, outer_only=True),
    )
    return model.geometry(
        f"{target.key}.through.{index:04d}.solid", "extrude", inputs=[profile],
        arguments={
            "vector": list(_subtract(inserted.end, inserted.start)),
            "extendStart": clearance + 1.0,
            "extendEnd": clearance + 1.0,
        },
    )


def _profile_properties(profile: Profile) -> dict[str, Any]:
    return profile.properties()


def _profile_half_extent(part: Part, direction: Vector) -> float:
    return (
        abs(_dot(part.profile_x_axis, direction)) * part.profile.depth / 2
        + abs(_dot(part.profile_y_axis, direction)) * part.profile.width / 2
    )


def _vertex_profile_axis(faces: list[Face], vertex_index: int) -> Vector:
    if vertex_index == 0:
        return faces[0].direction
    if vertex_index == len(faces):
        return faces[-1].direction
    # A corner post belongs to the incoming face.  Rotating its rectangular
    # section onto the angle bisector makes an ordinary L corner appear as a
    # diamond and leaves neither rail meeting a flat side of the post.
    return faces[vertex_index - 1].direction


def _horizontal_insertion(parameters: dict[str, Any]) -> float:
    # An unused insertion value is not a geometric input in welded mode.
    if parameters.get("mainHorizontalConnection", "insert") == "weld":
        return 0.0
    return _number(parameters, "horizontalBranchReserve")


def _miter_slope(direction: Vector, transverse: Vector, adjacent: Vector) -> float:
    bisector = _add(direction, adjacent)
    denominator = _dot(bisector, direction)
    if abs(_dot(direction, adjacent)) > 1.0e-7 or abs(denominator) < 1.0e-7:
        raise ValueError("外框斜拼目前只支持相邻面为90°的方管轮廓")
    return -_dot(bisector, transverse) / denominator


def _build_parts(
    parameters: dict[str, Any], points: list[Point], face_names: list[str],
    frame: Profile, horizontal: Profile, vertical: Profile,
    door_surface: DoorSurface | None,
    *, closed_perimeter: bool = False,
) -> tuple[list[Part], dict[str, list[Part]]]:
    height = _number(parameters, "height")
    faces = [Face(index + 1, face_names[index], points[index], points[index + 1])
             for index in range(len(face_names))]
    parts: list[Part] = []
    counters: dict[str, int] = {}
    crossing_map: dict[str, list[Part]] = {}
    miter = parameters.get("frameCornerJoin", "post_butt") == "rail_miter"

    frame_posts: list[Part] = []
    for vertex_index, point in enumerate(points):
        x_axis = _vertex_profile_axis(faces, vertex_index)
        y_axis = (-x_axis[1], x_axis[0], 0.0)
        frame_posts.append(_append_part(
            parts, counters, "outer_frame.vertical", f"外框立柱 {vertex_index + 1}",
            (point[0], point[1], frame.width if miter else 0.0),
            (point[0], point[1], height - frame.width if miter else height), frame, x_axis, y_axis,
            "outer_frame.vertical", "外框立柱",
        ))

    horizontal_reserve = _horizontal_insertion(parameters)
    vertical_reserve = _number(parameters, "verticalBranchReserve")
    door_u_min, door_v_min, door_u_max, door_v_max = (
        _door_bounds(parameters) if door_surface is not None else (0.0, 0.0, 0.0, 0.0)
    )
    fixed_half = _profile(parameters, "doorFrame").width / 2 if door_surface else 0.0
    for face in faces:
        direction, normal = face.direction, face.normal
        is_front = face.name == "正面"
        horizontal_positions = _horizontal_positions(
            parameters, height, "horizontalCount" if is_front else "sideHorizontalCount")
        _validate_positions(horizontal_positions, frame.width, height - frame.width,
                            horizontal.width, f"{face.name}主横杆")
        has_door = door_surface is not None and door_surface.face_index == face.index
        start_post = frame_posts[face.index - 1]
        end_post = frame_posts[face.index]
        start_extent = _profile_half_extent(start_post, direction)
        end_extent = _profile_half_extent(end_post, direction)
        if face.length <= start_extent + end_extent:
            raise ValueError(f"{face.name}宽度不足以容纳两端外框")
        rail_start = _add(face.start, _scale(direction, start_extent))
        rail_end = _add(face.end, _scale(direction, -end_extent))
        start_miter = end_miter = None
        if miter:
            rail_start = _add(face.start, _scale(direction, -frame.width / 2))
            rail_end = _add(face.end, _scale(direction, frame.width / 2))
            if face.index > 1 or closed_perimeter:
                previous = (faces[face.index - 2].direction if face.index > 1
                            else _normalize(_subtract(points[0], points[-1])))
                start_miter = _miter_slope(direction, normal, previous)
            if face.index < len(faces) or closed_perimeter:
                following = (faces[face.index].direction if face.index < len(faces)
                             else _normalize(_subtract(points[0], points[-1])))
                end_miter = _miter_slope(direction, normal, following)
        frame_rails: list[Part] = []
        for role, label, z in (
            ("outer_frame.bottom", "下框", frame.width / 2),
            ("outer_frame.top", "上框", height - frame.width / 2),
        ):
            frame_rails.append(_append_part(
                parts, counters, role, f"{face.name}{label}",
                (rail_start[0], rail_start[1], z), (rail_end[0], rail_end[1], z),
                frame, normal, (0.0, 0.0, 1.0), role, "外框横梁",
                face.index, face.name,
                start_miter=start_miter, end_miter=end_miter,
            ))

        face_horizontals: list[tuple[Part, float, float, float]] = []
        for index, z in enumerate(horizontal_positions, start=1):
            if has_door and door_v_min - fixed_half - horizontal.width / 2 < z < door_v_max + fixed_half + horizontal.width / 2:
                segments = (
                    ("start", "起始段", start_extent - horizontal_reserve, door_u_min - fixed_half),
                    ("end", "末端段", door_u_max + fixed_half, face.length - end_extent + horizontal_reserve),
                )
                for side, label, u_start, u_end in segments:
                    horizontal_part = _append_part(
                        parts, counters, f"main_grid.horizontal.{side}",
                        f"{face.name}主横杆 {index} {label}",
                        (*_add(face.start, _scale(direction, u_start))[:2], z),
                        (*_add(face.start, _scale(direction, u_end))[:2], z),
                        horizontal, normal, (0.0, 0.0, 1.0),
                        "main_grid.horizontal", "主横杆", face.index, face.name,
                    )
                    face_horizontals.append((horizontal_part, u_start, u_end, z))
                    boundary_post = start_post if side == "start" else end_post
                    if horizontal_reserve > 0:
                        crossing_map.setdefault(boundary_post.key, []).append(horizontal_part)
            else:
                u_start = start_extent - horizontal_reserve
                u_end = face.length - end_extent + horizontal_reserve
                horizontal_part = _append_part(
                    parts, counters, "main_grid.horizontal", f"{face.name}主横杆 {index}",
                    (*_add(face.start, _scale(direction, u_start))[:2], z),
                    (*_add(face.start, _scale(direction, u_end))[:2], z),
                    horizontal, normal, (0.0, 0.0, 1.0),
                    "main_grid.horizontal", "主横杆", face.index, face.name,
                )
                face_horizontals.append((horizontal_part, u_start, u_end, z))
                if horizontal_reserve > 0:
                    crossing_map.setdefault(start_post.key, []).append(horizontal_part)
                    crossing_map.setdefault(end_post.key, []).append(horizontal_part)

        vertical_minimum = start_extent
        vertical_maximum = face.length - end_extent
        vertical_count = _vertical_count(
            parameters, vertical_minimum, vertical_maximum,
            vertical.width, "verticalCountPerFace" if is_front else "sideVerticalCount",
            "maximumVerticalClearGap" if is_front else "sideMaximumVerticalClearGap",
        )
        vertical_positions = _even_positions(vertical_count, vertical_minimum, vertical_maximum)
        _validate_positions(vertical_positions, vertical_minimum, vertical_maximum,
                            vertical.width, f"{face.name}主竖杆")
        for index, distance in enumerate(vertical_positions, start=1):
            base = _add(face.start, _scale(direction, distance))
            if has_door and door_u_min - fixed_half - vertical.width / 2 < distance < door_u_max + fixed_half + vertical.width / 2:
                vertical_specs = (
                    ("bottom", "下段", frame.width - vertical_reserve, door_v_min - fixed_half, (frame_rails[0],)),
                    ("top", "上段", door_v_max + fixed_half, height - frame.width + vertical_reserve, (frame_rails[1],)),
                )
            else:
                vertical_specs = ((
                    "full", "", frame.width - vertical_reserve,
                    height - frame.width + vertical_reserve, tuple(frame_rails),
                ),)
            for segment, label, z_start, z_end, boundary_rails in vertical_specs:
                role = "main_grid.vertical" if segment == "full" else f"main_grid.vertical.{segment}"
                name = f"{face.name}主竖杆 {index}" + (f" {label}" if label else "")
                vertical_part = _append_part(
                    parts, counters, role, name,
                    (base[0], base[1], z_start), (base[0], base[1], z_end),
                    vertical, normal, direction, "main_grid.vertical", "主竖杆",
                    face.index, face.name,
                )
                if vertical_reserve > 0:
                    for frame_rail in boundary_rails:
                        crossing_map.setdefault(frame_rail.key, []).append(vertical_part)
                for target, u_start, u_end, z in face_horizontals:
                    if not (u_start < distance < u_end and z_start < z < z_end):
                        continue
                    target_size = max(target.profile.width, target.profile.depth)
                    inserted_size = max(vertical_part.profile.width, vertical_part.profile.depth)
                    if target_size >= inserted_size:
                        crossing_map.setdefault(target.key, []).append(vertical_part)
                    else:
                        crossing_map.setdefault(vertical_part.key, []).append(target)
    return parts, crossing_map


def _append_five_face_caps(
    parameters: dict[str, Any], points: list[Point], frame: Profile,
    horizontal: Profile, vertical: Profile, parts: list[Part],
    counters: dict[str, int], crossing_map: dict[str, list[Part]],
    door_surface: DoorSurface | None,
) -> None:
    if len(points) != 4:
        raise ValueError("五面防盗窗的立面轮廓必须包含四个角点")

    height = _number(parameters, "height")
    front_left, front_right = points[1], points[2]
    back_left, back_right = points[0], points[3]
    width_direction = _normalize(_subtract(front_right, front_left))
    depth_direction = _normalize(_subtract(back_left, front_left))
    width = math.dist(front_left, front_right)
    depth = math.dist(front_left, back_left)
    horizontal_reserve = _horizontal_insertion(parameters)
    vertical_reserve = _number(parameters, "verticalBranchReserve")
    miter = parameters.get("frameCornerJoin", "post_butt") == "rail_miter"

    width_start_extent = _profile_half_extent(next(
        part for part in parts if part.face_index == 1 and part.key.startswith("outer_frame.top.")
    ), width_direction)
    width_end_extent = _profile_half_extent(next(
        part for part in parts if part.face_index == 3 and part.key.startswith("outer_frame.top.")
    ), width_direction)
    depth_start_extent = _profile_half_extent(next(
        part for part in parts if part.face_index == 2 and part.key.startswith("outer_frame.top.")
    ), depth_direction)
    rail_start_distance = width_start_extent - horizontal_reserve
    rail_end_distance = width - width_end_extent + horizontal_reserve
    rod_start_distance = depth_start_extent - vertical_reserve
    rod_end_distance = depth - frame.width / 2 + vertical_reserve
    crossbar_positions = _even_positions(
        _integer(parameters, "topBottomCrossbarCount"), frame.width, depth - frame.width,
    )
    rod_minimum = width_start_extent
    rod_maximum = width - width_end_extent
    rod_positions = _even_positions(
        _vertical_count(
            parameters, rod_minimum, rod_maximum,
            vertical.width, "topBottomRodCount",
        ),
        rod_minimum, rod_maximum,
    )
    _validate_positions(crossbar_positions, depth_start_extent, depth - frame.width / 2,
                        horizontal.width, "顶底面横杆")
    _validate_positions(rod_positions, rod_minimum, rod_maximum,
                        vertical.width, "顶底面纵杆")
    door_u_min, door_v_min, door_u_max, door_v_max = (
        _door_bounds(parameters) if door_surface is not None else (0.0, 0.0, 0.0, 0.0)
    )
    fixed_half = _profile(parameters, "doorFrame").width / 2 if door_surface else 0.0
    back_posts = [part for part in parts if part.key.startswith("outer_frame.vertical.")]
    back_start_extent = _profile_half_extent(back_posts[0], width_direction)
    back_end_extent = _profile_half_extent(back_posts[-1], width_direction)

    for face_index, face_name, z in (
        (4, "上面", height - frame.width / 2),
        (5, "下面", frame.width / 2),
    ):
        has_door = door_surface is not None and door_surface.face_index == face_index
        perimeter_role = "outer_frame.top" if face_index == 4 else "outer_frame.bottom"
        perimeter = {
            part.face_index: part for part in parts
            if part.key.startswith(f"{perimeter_role}.") and part.face_index in {1, 2, 3}
        }
        if set(perimeter) != {1, 2, 3}:
            raise ValueError(f"{face_name}缺少立面共用的外框横梁")
        back_origin = back_left
        back_start = -frame.width / 2 if miter else back_start_extent
        back_end = width + frame.width / 2 if miter else width - back_end_extent
        back_start_miter = back_end_miter = None
        if miter:
            back_start_miter = _miter_slope(width_direction, depth_direction, depth_direction)
            back_end_miter = _miter_slope(width_direction, depth_direction, _scale(depth_direction, -1))
        back_frame = _append_part(
            parts, counters, "outer_frame.back", f"{face_name}后框",
            (*_add(back_origin, _scale(width_direction, back_start))[:2], z),
            (*_add(back_origin, _scale(width_direction, back_end))[:2], z),
            frame, (0.0, 0.0, 1.0), depth_direction,
            "outer_frame.back", "外框后梁", face_index, face_name,
            start_miter=back_start_miter, end_miter=back_end_miter,
        )

        face_crossbars: list[tuple[Part, float, float, float]] = []
        for index, distance in enumerate(crossbar_positions, start=1):
            origin = _add(front_left, _scale(depth_direction, distance))
            if has_door and door_v_min - fixed_half - horizontal.width / 2 < distance < door_v_max + fixed_half + horizontal.width / 2:
                specs = (
                    ("start", "起始段", rail_start_distance, door_u_min - fixed_half, perimeter[1]),
                    ("end", "末端段", door_u_max + fixed_half, rail_end_distance, perimeter[3]),
                )
            else:
                specs = (("full", "", rail_start_distance, rail_end_distance, None),)
            for segment, label, u_start, u_end, boundary in specs:
                role = "cap_grid.horizontal" if segment == "full" else f"cap_grid.horizontal.{segment}"
                name = f"{face_name}横杆 {index}" + (f" {label}" if label else "")
                crossbar = _append_part(
                    parts, counters, role, name,
                    (*_add(origin, _scale(width_direction, u_start))[:2], z),
                    (*_add(origin, _scale(width_direction, u_end))[:2], z),
                    horizontal, (0.0, 0.0, 1.0), depth_direction,
                    "cap_grid.horizontal", "顶底面横杆", face_index, face_name,
                )
                face_crossbars.append((crossbar, u_start, u_end, distance))
                if horizontal_reserve <= 0:
                    continue
                if boundary is None:
                    crossing_map.setdefault(perimeter[1].key, []).append(crossbar)
                    crossing_map.setdefault(perimeter[3].key, []).append(crossbar)
                else:
                    crossing_map.setdefault(boundary.key, []).append(crossbar)

        for index, distance in enumerate(rod_positions, start=1):
            origin = _add(front_left, _scale(width_direction, distance))
            if has_door and door_u_min - fixed_half - vertical.width / 2 < distance < door_u_max + fixed_half + vertical.width / 2:
                rod_specs = (
                    ("front", "前段", rod_start_distance, door_v_min - fixed_half, (perimeter[2],)),
                    ("back", "后段", door_v_max + fixed_half, rod_end_distance, (back_frame,)),
                )
            else:
                rod_specs = (("full", "", rod_start_distance, rod_end_distance, (perimeter[2], back_frame)),)
            for segment, label, v_start, v_end, boundary_frames in rod_specs:
                role = "cap_grid.vertical" if segment == "full" else f"cap_grid.vertical.{segment}"
                name = f"{face_name}纵杆 {index}" + (f" {label}" if label else "")
                rod = _append_part(
                    parts, counters, role, name,
                    (*_add(origin, _scale(depth_direction, v_start))[:2], z),
                    (*_add(origin, _scale(depth_direction, v_end))[:2], z),
                    vertical, (0.0, 0.0, 1.0), width_direction,
                    "cap_grid.vertical", "顶底面纵杆", face_index, face_name,
                )
                if vertical_reserve > 0:
                    for boundary_frame in boundary_frames:
                        crossing_map.setdefault(boundary_frame.key, []).append(rod)
                for target, u_start, u_end, v in face_crossbars:
                    if not (u_start < distance < u_end and v_start < v < v_end):
                        continue
                    target_size = max(target.profile.width, target.profile.depth)
                    inserted_size = max(rod.profile.width, rod.profile.depth)
                    if target_size >= inserted_size:
                        crossing_map.setdefault(target.key, []).append(rod)
                    else:
                        crossing_map.setdefault(rod.key, []).append(target)


def _validate_through_fit(
    receiver: Profile, inserted: Profile, clearance: float, label: str,
) -> None:
    available = min(receiver.width, receiver.depth) - receiver.wall * 2
    required = max(inserted.width, inserted.depth) + clearance * 2
    if available <= required:
        raise ValueError(
            f"{label}无法安全穿管：接收管内腔较小边 {available:g} mm，"
            f"必须大于穿杆及开孔间隙 {required:g} mm"
        )


def _validate(
    layout: str, parameters: dict[str, Any], points: list[Point], frame: Profile,
    horizontal: Profile, vertical: Profile, door_surface: DoorSurface | None,
) -> None:
    height = _number(parameters, "height")
    if parameters.get("frameCornerJoin", "post_butt") not in {"post_butt", "rail_miter"}:
        raise ValueError("frameCornerJoin 仅支持 post_butt 或 rail_miter")
    if parameters.get("frameCornerJoin", "post_butt") == "rail_miter" and (
        frame.kind != "rect" or abs(frame.width - frame.depth) > 1.0e-7
    ):
        raise ValueError("上下框45°斜拼目前仅支持宽深相等的矩形方管外框")
    if parameters.get("mainHorizontalConnection", "insert") not in {"insert", "weld"}:
        raise ValueError("mainHorizontalConnection 仅支持 insert 或 weld")
    if parameters.get("mainHorizontalConnection", "insert") == "weld":
        if frame.kind != "rect":
            raise ValueError("横杆贴合焊接目前仅支持有平直接触面的标准矩形管外框")
        # A corner post can receive one face across its width and the adjacent
        # face across its depth; cap crossbars also meet the horizontal rails.
        # The unchanged square-cut end needs a full flat seat in every case.
        # Conservatively require both receiving sides rather than silently
        # claiming a corner-radius/curved-surface contact is a fitted weld.
        flat_width = min(frame.width, frame.depth) - frame.radius * 2
        if horizontal.depth > flat_width + 1.0e-7:
            raise ValueError(
                f"横杆贴合焊接端面厚度 {horizontal.depth:g} mm 超过外框扣除圆角后的"
                f"最小平直面宽度 {flat_width:g} mm，请减小外框圆角或调整管材规格"
            )
    if height <= frame.width * 2:
        raise ValueError("产品高度必须大于外框宽度的两倍")
    if not (
        frame.width > horizontal.width > vertical.width
        and frame.depth > horizontal.depth > vertical.depth
    ):
        raise ValueError("杆件宽深必须满足：外框 > 横杆 > 竖杆")
    for key in ("horizontalCount", "sideHorizontalCount", "verticalCountPerFace", "sideVerticalCount"):
        if not 0 <= _integer(parameters, key) <= 100:
            raise ValueError(f"{key} 超出支持范围")
    if str(parameters.get("verticalLayoutMode", "manual_count")) not in {
        "manual_count", "maximum_clear_gap",
    }:
        raise ValueError("verticalLayoutMode 不支持")
    for key in ("maximumVerticalClearGap", "sideMaximumVerticalClearGap"):
        if _number(parameters, key) <= 0:
            raise ValueError(f"{key} 最大竖杆净间距必须大于0")
    if layout == "five-face":
        if not 0 <= _integer(parameters, "topBottomCrossbarCount") <= 100:
            raise ValueError("topBottomCrossbarCount 超出支持范围")
        if not 0 <= _integer(parameters, "topBottomRodCount") <= 100:
            raise ValueError("topBottomRodCount 超出支持范围")
    clearance = _number(parameters, "assemblyClearance")
    if clearance < 0:
        raise ValueError("装配间隙不能为负数")
    for key in ("horizontalBranchReserve", "verticalBranchReserve"):
        if key == "horizontalBranchReserve" and parameters.get("mainHorizontalConnection", "insert") == "weld":
            continue
        insertion = _number(parameters, key)
        if not 0 <= insertion <= 20:
            raise ValueError(f"{key} 必须在0到20 mm之间")
    for index in range(len(points) - 1):
        if math.dist(points[index], points[index + 1]) <= frame.width * 2:
            raise ValueError(f"第 {index + 1} 面宽度必须大于外框宽度的两倍")
    if door_surface is None:
        return

    door_u_min, door_v_min, door_u_max, door_v_max = _door_bounds(parameters)
    fixed_profile = _profile(parameters, "doorFrame")
    fixed_half = fixed_profile.width / 2
    clear_u_min, clear_v_min, clear_u_max, clear_v_max = _door_surface_clear_bounds(door_surface, points, frame)
    if door_u_min - fixed_half <= clear_u_min or door_u_max + fixed_half >= clear_u_max:
        raise ValueError("逃生窗横向范围必须保持在所选面的外框以内")
    if door_v_min - fixed_half <= clear_v_min or door_v_max + fixed_half >= clear_v_max:
        raise ValueError("逃生窗纵向范围必须保持在所选面的外框以内")
    leaf_profile = _profile(parameters, "doorLeafFrame")
    gap = _number(parameters, "doorGap")
    required = fixed_profile.width + leaf_profile.width * 2 + gap * 2
    if gap < 0 or _number(parameters, "doorWidth") <= required or _number(parameters, "doorHeight") <= required:
        raise ValueError("逃生窗尺寸不足以容纳固定窗框、活动窗扇和装配间隙")
    if str(parameters.get("doorHingeSide")) not in {"left", "right"}:
        raise ValueError("铰链侧仅支持左侧或右侧")
    if not 1 <= _integer(parameters, "doorHingeCount") <= 10:
        raise ValueError("铰链数量超出支持范围")
    for key in ("doorHorizontalCount", "doorVerticalCount"):
        if not 0 <= _integer(parameters, key) <= 100:
            raise ValueError(f"{key} 超出支持范围")


def _validate_connections(parts: list[Part], crossing_map: dict[str, list[Part]], clearance: float) -> None:
    by_key = {part.key: part for part in parts}
    for key, inserted_parts in crossing_map.items():
        receiver = by_key[key]
        for inserted in inserted_parts:
            _validate_through_fit(receiver.profile, inserted.profile, clearance,
                                  f"{receiver.name}与{inserted.name}")
            _validate_miter_hole_margin(receiver, inserted, clearance)
        if receiver.key.startswith("outer_frame.vertical."):
            for index, first in enumerate(inserted_parts):
                for second in inserted_parts[index + 1:]:
                    if first.face_index == second.face_index:
                        continue
                    if _part_envelopes_overlap(first, second):
                        raise ValueError(
                            f"{receiver.name}内的{first.name}与{second.name}端部相交，"
                            "请减小横杆入榫深度，或调整横杆高度以错开连接位置"
                        )


def _validate_miter_hole_margin(receiver: Part, inserted: Part, clearance: float) -> None:
    planes = _miter_planes(receiver)
    if not planes:
        return
    # Clip the actual crossing cutter's conservative envelope to the receiver's
    # stock first; remote portions of a long crossbar do not belong to this joint.
    direction = _normalize(_subtract(inserted.end, inserted.start))
    limits = []
    for index in range(3):
        axis = tuple(1.0 if i == index else 0.0 for i in range(3))
        stock_half = _profile_half_extent(receiver, axis)
        cutter_half = (_profile_half_extent(inserted, axis)
                       + clearance * (abs(inserted.profile_x_axis[index]) + abs(inserted.profile_y_axis[index]))
                       + (clearance + 1.0) * abs(direction[index]))
        low = max(min(receiver.start[index], receiver.end[index]) - stock_half,
                  min(inserted.start[index], inserted.end[index]) - cutter_half)
        high = min(max(receiver.start[index], receiver.end[index]) + stock_half,
                   max(inserted.start[index], inserted.end[index]) + cutter_half)
        if high <= low:
            return
        limits.append((low, high))
    for _, origin, inward in planes:
        nearest = tuple(limits[index][0 if inward[index] >= 0 else 1] for index in range(3))
        if _dot(_subtract(nearest, origin), inward) <= 1.0e-7:
            raise ValueError(
                f"{receiver.name}与{inserted.name}的插接孔进入45°斜切端面，"
                "请减少杆数或调整布置，使孔与拼角保持分离"
            )


def _part_envelopes_overlap(first: Part, second: Part) -> bool:
    for index in range(3):
        axis = tuple(1.0 if i == index else 0.0 for i in range(3))
        first_half = _profile_half_extent(first, axis)
        second_half = _profile_half_extent(second, axis)
        minimum = max(min(first.start[index], first.end[index]) - first_half,
                      min(second.start[index], second.end[index]) - second_half)
        maximum = min(max(first.start[index], first.end[index]) + first_half,
                      max(second.start[index], second.end[index]) + second_half)
        if maximum - minimum <= 1.0e-7:
            return False
    return True


def _end_process(part: Part) -> dict[str, Any]:
    return {
        "startCut": "miter_45" if part.start_miter is not None else "square",
        "endCut": "miter_45" if part.end_miter is not None else "square",
        "lengthReference": "outside_long_points",
        "cutPlanes": [{"end": end, "origin": list(origin), "inwardNormal": list(_normalize(inward))}
                      for end, origin, inward in _miter_planes(part)],
    }


def _connection_process(
    part: Part, crossing_map: dict[str, list[Part]], parameters: dict[str, Any],
) -> dict[str, Any]:
    process: dict[str, Any] = {
        "endCut": "square",
        "receives": [inserted.key for inserted in crossing_map.get(part.key, [])],
        "passesInto": [key for key, inserted in crossing_map.items()
                       if any(item.key == part.key for item in inserted)],
    }
    if part.key.startswith(("outer_frame.", "access_door.fixed_frame.", "access_door.leaf.frame.")):
        process.update({"cornerJoin": "butt_90", "buttWrapMode": "side_wraps_horizontal"})
        if part.key.startswith("outer_frame.") and parameters.get("frameCornerJoin", "post_butt") == "rail_miter":
            process.update({"cornerJoin": "miter_45" if _miter_planes(part) else "butt_90",
                            "buttWrapMode": "horizontal_wraps_posts"})
            process["endCut"] = "miter_45" if _miter_planes(part) else "square"
    elif part.key.startswith("access_door.leaf.horizontal."):
        process["endJoin"] = "butt_to_leaf_inner_face"
    elif part.key.startswith(("main_grid.", "cap_grid.")) and any(
        segment in part.key.split(".") for segment in ("start", "end", "bottom", "top", "front", "back")
    ):
        process["openingEndJoin"] = "butt_to_fixed_frame_outer_face"
    if part.key.startswith(("main_grid.horizontal.", "cap_grid.horizontal.")):
        process["outerFrameConnection"] = parameters.get("mainHorizontalConnection", "insert")
        process["outerFrameInsertionDepth"] = _horizontal_insertion(parameters)
    return process


def _generate_multi_face_geometry(
    parameters: dict[str, Any], context: dict[str, Any], *,
    template_id: str, template_version: str, layout: str,
) -> dict[str, Any]:
    purpose = request_geometry_purpose(context)
    points, face_names = _footprint(layout, parameters)
    frame = _profile(parameters, "frame")
    horizontal = _profile(parameters, "horizontal")
    vertical = _profile(parameters, "vertical")
    door_surface = _resolve_door_surface(layout, parameters, points, face_names, frame)
    construction = _door_construction_parameters(parameters, door_surface, points, face_names, frame)
    _validate(layout, construction, points, frame, horizontal, vertical, door_surface)

    template = context["template"]
    model = NeutralModel(
        template_id=template_id,
        template_version=template_version,
        package_digest=str(template.get("packageDigest", "")),
        parameters=parameters,
    )
    shared_geometry = SharedTubeGeometry(model)
    parts, crossing_map = _build_parts(
        construction, points, face_names, frame, horizontal, vertical, door_surface,
        closed_perimeter=layout == "five-face",
    )
    counters: dict[str, int] = {}
    for part in parts:
        role, _, suffix = part.key.rpartition(".")
        counters[role] = max(counters.get(role, 0), int(suffix))
    if layout == "five-face":
        _append_five_face_caps(
            construction, points, frame, horizontal, vertical,
            parts, counters, crossing_map, door_surface,
        )
    door_frames: tuple[dict[str, Part], dict[str, Part]] | None = None
    if door_surface is not None:
        door_frames = _append_access_door(
            construction, door_surface, parts, counters, crossing_map,
        )
    clearance = _number(parameters, "assemblyClearance")
    _validate_connections(parts, crossing_map, clearance)

    processed_frames = []
    frame_receivers = {}
    if door_frames is not None:
        def local_point(point):
            delta = _subtract(point, door_surface.origin)
            return (_dot(delta, door_surface.u_axis), _dot(delta, door_surface.normal), _dot(delta, door_surface.v_axis))
        # Existing reference frames establish clear bounds and validate insertion.
        # Only the selected processed frame items below are emitted to the model.
        local_bars = [
            _frame_geometry.Part(part.key, part.name, local_point(part.start), local_point(part.end),
                                 part.profile, "access_door.leaf")
            for part in parts if part.key.startswith(("access_door.leaf.horizontal.", "access_door.leaf.vertical."))
        ]
        placement = {"origin": list(door_surface.origin), "xAxis": list(door_surface.u_axis),
                     "yAxis": list(door_surface.normal), "zAxis": list(door_surface.v_axis)}
        processes = [
            (_frame_geometry._process(construction, join_key, wrap_key), _profile(construction, profile_key))
            for join_key, wrap_key, profile_key in (
                ("doorFrameJoinType", "doorFrameButtWrapMode", "doorFrame"),
                ("doorLeafFrameJoinType", "doorLeafFrameButtWrapMode", "doorLeafFrame"))
        ]
        _frame_geometry.validate_frame_processes(construction, processes)
        for reference, (process, profile), prefix, label, group in zip(
                door_frames, processes,
                ("access_door.fixed_frame", "access_door.leaf.frame"),
                ("固定窗框", "活动窗扇框"), ("access_door.fixed_frame", "access_door.leaf")):
            left = local_point(reference["left"].start)[0] - profile.width / 2
            right = local_point(reference["right"].start)[0] + profile.width / 2
            bottom = local_point(reference["left"].start)[2]
            top = local_point(reference["left"].end)[2]
            records, sides = _frame_geometry.emit_surface_frame(
                model, shared_geometry, construction, prefix=prefix, name=label, profile=profile, group=group,
                bounds=(left, bottom, right, top), process=process, placement=placement,
                inserted_parts=local_bars if group == "access_door.leaf" else [], purpose=purpose)
            processed_frames.extend(records)
            frame_receivers.update({reference[side].key: key for side, key in sides.items()})
        parts = [part for part in parts if part.key not in frame_receivers]
    raw_geometry = {part.key: _emit_tube(model, part, shared_geometry) for part in parts}
    representations: dict[str, tuple[str, str]] = {}
    for part in parts:
        display = raw_geometry[part.key]
        export = display
        if purpose == "display":
            representations[part.key] = (display, display)
            continue
        export = _emit_miter_shape(model, part, export)
        cutters = [
            _emit_crossing_cutter(model, part, inserted, index, clearance)
            for index, inserted in enumerate(crossing_map.get(part.key, []), start=1)
        ]
        if cutters:
            export = model.geometry(
                f"{part.key}.solid.final", "boolean", inputs=[export, *cutters],
                arguments={"operation": "subtract", "target": export, "tools": cutters},
            )
        representations[part.key] = (display, export)

    item_keys: list[str] = []
    rows: list[dict[str, Any]] = []
    for index, part in enumerate(parts, start=1):
        part_number = f"{parameters['productCode']}-{index:03d}"
        properties = {
            "partNumber": part_number,
            "quantity": 1,
            "group": "access_door" if part.key.startswith("access_door.") else "main",
            "length": round(part.length, 3),
            "manufacturing.categoryKey": part.category_key,
            "manufacturing.categoryName": part.category_name,
            "tubeDesigner.profile": _profile_properties(part.profile),
            "tubeDesigner.faceIndex": part.face_index,
            "tubeDesigner.faceName": part.face_name,
            "tubeDesigner.endProcess": _end_process(part),
            "tubeDesigner.connectionProcess": _connection_process(part, crossing_map, construction),
        }
        connection = properties["tubeDesigner.connectionProcess"]
        connection["passesInto"] = list(dict.fromkeys(frame_receivers.get(key, key) for key in connection["passesInto"]))
        if part.start_miter is not None or part.end_miter is not None:
            properties["tubeDesigner.displayApproximation"] = "uncut_miter_stock"
        item_key = model.item(
            part.key, part.name,
            representations={"display": representations[part.key][0], "export": representations[part.key][1]},
            properties=properties,
        )
        item_keys.append(item_key)
        rows.append({
            "key": f"row.{part.key}",
            "parentKey": str(parameters["productCode"]),
            "itemKey": item_key,
            "values": {
                "partNumber": part_number,
                "name": part.name,
                "quantity": 1,
                "length": round(part.length, 3),
            },
        })

    for record in processed_frames:
        record["properties"]["tubeDesigner.connectionProcess"]["receives"] = list(dict.fromkeys(
            part.key for reference_key, actual_key in frame_receivers.items() if actual_key == record["key"]
            for part in crossing_map.get(reference_key, [])))
        record["properties"]["tubeDesigner.connectionProcess"]["passesInto"] = []
        number = f"{parameters['productCode']}-{len(item_keys) + 1:03d}"
        record["properties"].update({"partNumber": number, "tubeDesigner.faceIndex": door_surface.face_index,
                                     "tubeDesigner.faceName": door_surface.face_name})
        key = model.item(record["key"], record["name"], representations=record["representations"], properties=record["properties"])
        item_keys.append(key)
        rows.append({"key": f"row.{key}", "parentKey": str(parameters["productCode"]), "itemKey": key,
                     "values": {"partNumber": number, "name": record["name"], "quantity": 1,
                                "length": record["properties"]["length"]}})

    for index in range(1, len(points) - 1):
        model.relationship(
            f"footprint.corner.{index:04d}", "corner",
            [
                f"outer_frame.vertical.{index + 1:04d}",
                f"outer_frame.top.{index:04d}",
                f"outer_frame.top.{index + 1:04d}",
            ],
            properties={
                "origin": list(points[index]),
                "incomingFace": face_names[index - 1],
                "outgoingFace": face_names[index],
            },
        )
    if door_surface is not None and door_frames is not None:
        fixed, leaf = door_frames
        hinge_side = str(parameters["doorHingeSide"])
        hinge_u = _door_bounds(construction)[0 if hinge_side == "left" else 2]
        _, door_v_min, _, door_v_max = _door_bounds(construction)
        hinge_count = _integer(parameters, "doorHingeCount")
        for index in range(hinge_count):
            hinge_v = door_v_min + (door_v_max - door_v_min) * (index + 1) / (hinge_count + 1)
            model.relationship(
                f"access_door.hinge.{index + 1:04d}", "hinge",
                [frame_receivers[fixed[hinge_side].key], frame_receivers[leaf[hinge_side].key]],
                properties={
                    "participantRoles": ["fixed", "moving"],
                    "origin": list(_surface_point(door_surface, hinge_u, hinge_v)),
                    "axis": list(door_surface.v_axis),
                    "faceIndex": door_surface.face_index,
                    "faceName": door_surface.face_name,
                },
            )
    model.output("display.default", "display", item_keys)
    model.output("export.manufacturing", "export", item_keys)
    model.table(
        "parts", "零件清单",
        columns=[
            {"key": "partNumber", "displayName": "零件编号", "valueType": "string"},
            {"key": "name", "displayName": "名称", "valueType": "string"},
            {"key": "quantity", "displayName": "数量", "valueType": "integer"},
            {"key": "length", "displayName": "长度", "valueType": "number", "unit": "mm"},
        ],
        rows=rows,
    )
    return finish_geometry_request(model, context)


def generate_multi_face(
    parameters: dict[str, Any], context: dict[str, Any], *,
    template_id: str, template_version: str, layout: str,
) -> dict[str, Any]:
    def kernel(effective: dict[str, Any], request: dict[str, Any]) -> dict[str, Any]:
        return _generate_multi_face_geometry(effective, request, template_id=template_id,
                                             template_version=template_version, layout=layout)
    return generate_reviewed(parameters, context, layout=layout, load_profile=_profile,
                             kernel=kernel)
