from __future__ import annotations

from dataclasses import dataclass
import math
from typing import Any, Iterable

from icax_template_sdk import NeutralModel


Point = tuple[float, float, float]
Vector = tuple[float, float, float]


@dataclass(frozen=True)
class Profile:
    kind: str
    width: float
    depth: float
    wall: float
    radius: float = 0.0


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
    kind = str(parameters[f"{prefix}ProfileType"])
    width = _number(parameters, f"{prefix}Width")
    depth = width if kind == "round" else _number(parameters, f"{prefix}Depth")
    wall = _number(parameters, f"{prefix}WallThickness")
    radius_key = f"{prefix}CornerRadius"
    radius = _number(parameters, radius_key) if radius_key in parameters else 0.0
    if kind not in {"rect", "round"}:
        raise ValueError(f"{prefix}ProfileType 不支持：{kind}")
    if width <= 0 or depth <= 0 or wall <= 0 or wall * 2 >= min(width, depth):
        raise ValueError(f"{prefix} 截面尺寸或壁厚无效")
    if radius < 0 or (kind == "rect" and radius >= min(width, depth) / 2):
        raise ValueError(f"{prefix}CornerRadius 无效")
    return Profile(kind, width, depth, wall, radius)


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


def _footprint(layout: str, parameters: dict[str, Any]) -> tuple[list[Point], list[str]]:
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
        raise ValueError("所选检修口安装面不属于当前产品")

    names = {
        "left": "左侧面", "front": "正面", "right": "右侧面",
        "top": "上面", "bottom": "下面",
    }
    face_name = names[face_code]
    if face_code in {"left", "front", "right"}:
        try:
            offset = face_names.index(face_name)
        except ValueError as error:
            raise ValueError("所选检修口安装面不属于当前产品") from error
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


def _even_positions(count: int, minimum: float, maximum: float) -> list[float]:
    if count == 0:
        return []
    if maximum <= minimum:
        raise ValueError("杆件布置没有可用空间")
    step = (maximum - minimum) / (count + 1)
    return [minimum + step * (index + 1) for index in range(count)]


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
    bar_width: float, manual_key: str,
) -> int:
    mode = str(parameters.get("verticalLayoutMode", "manual_count"))
    if mode == "maximum_clear_gap":
        return _count_for_maximum_clear_gap(
            minimum, maximum, bar_width,
            _number(parameters, "maximumVerticalClearGap"),
        )
    if mode == "manual_count":
        return _integer(parameters, manual_key)
    raise ValueError(f"不支持的竖杆布置方式：{mode}")


def _horizontal_positions(parameters: dict[str, Any], height: float) -> list[float]:
    count = _integer(parameters, "horizontalCount")
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
) -> Part:
    if math.dist(start, end) <= 1.0e-7:
        raise ValueError(f"{name} 的长度为零")
    part = Part(
        _next_key(counters, role), name, start, end, profile,
        _normalize(profile_x_axis), _normalize(profile_y_axis),
        category_key, category_name, face_index, face_name,
    )
    parts.append(part)
    return part


def _append_surface_rectangle(
    parts: list[Part], counters: dict[str, int], surface: DoorSurface,
    role_prefix: str, label: str, category_key: str, category_name: str,
    profile: Profile, u_min: float, v_min: float, u_max: float, v_max: float,
) -> dict[str, Part]:
    result = {
        "bottom": _append_part(
            parts, counters, f"{role_prefix}.bottom", f"{surface.face_name}{label}下边",
            _surface_point(surface, u_min, v_min), _surface_point(surface, u_max, v_min),
            profile, surface.normal, surface.v_axis, category_key, category_name,
            surface.face_index, surface.face_name,
        ),
        "top": _append_part(
            parts, counters, f"{role_prefix}.top", f"{surface.face_name}{label}上边",
            _surface_point(surface, u_min, v_max), _surface_point(surface, u_max, v_max),
            profile, surface.normal, surface.v_axis, category_key, category_name,
            surface.face_index, surface.face_name,
        ),
        "left": _append_part(
            parts, counters, f"{role_prefix}.left", f"{surface.face_name}{label}左边",
            _surface_point(surface, u_min, v_min), _surface_point(surface, u_min, v_max),
            profile, surface.u_axis, surface.normal, category_key, category_name,
            surface.face_index, surface.face_name,
        ),
        "right": _append_part(
            parts, counters, f"{role_prefix}.right", f"{surface.face_name}{label}右边",
            _surface_point(surface, u_max, v_min), _surface_point(surface, u_max, v_max),
            profile, surface.u_axis, surface.normal, category_key, category_name,
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
        parts, counters, surface, "access_door.fixed_frame", "固定门框",
        "access_door.fixed_frame", "固定门框", fixed_profile,
        u_min, v_min, u_max, v_max,
    )
    inset = fixed_profile.width / 2 + _number(parameters, "doorGap") + leaf_profile.width / 2
    leaf_u_min, leaf_v_min = u_min + inset, v_min + inset
    leaf_u_max, leaf_v_max = u_max - inset, v_max - inset
    leaf = _append_surface_rectangle(
        parts, counters, surface, "access_door.leaf.frame", "活动门扇框",
        "access_door.leaf.frame", "活动门扇框", leaf_profile,
        leaf_u_min, leaf_v_min, leaf_u_max, leaf_v_max,
    )

    inner_u_min, inner_u_max = leaf_u_min + leaf_profile.width / 2, leaf_u_max - leaf_profile.width / 2
    inner_v_min, inner_v_max = leaf_v_min + leaf_profile.width / 2, leaf_v_max - leaf_profile.width / 2
    door_horizontal = _profile(parameters, "doorHorizontal")
    door_vertical = _profile(parameters, "doorVertical")
    horizontals: list[Part] = []
    for index, v in enumerate(_even_positions(
        _integer(parameters, "doorHorizontalCount"), inner_v_min, inner_v_max,
    ), start=1):
        bar = _append_part(
            parts, counters, "access_door.leaf.horizontal",
            f"{surface.face_name}门内横杆 {index}",
            _surface_point(surface, inner_u_min, v), _surface_point(surface, inner_u_max, v),
            door_horizontal, surface.normal, surface.v_axis,
            "access_door.leaf.horizontal", "门内横杆", surface.face_index, surface.face_name,
        )
        horizontals.append(bar)
        crossing_map.setdefault(leaf["left"].key, []).append(bar)
        crossing_map.setdefault(leaf["right"].key, []).append(bar)

    for index, u in enumerate(_even_positions(
        _integer(parameters, "doorVerticalCount"), inner_u_min, inner_u_max,
    ), start=1):
        bar = _append_part(
            parts, counters, "access_door.leaf.vertical",
            f"{surface.face_name}门内竖杆 {index}",
            _surface_point(surface, u, inner_v_min), _surface_point(surface, u, inner_v_max),
            door_vertical, surface.u_axis, surface.normal,
            "access_door.leaf.vertical", "门内竖杆", surface.face_index, surface.face_name,
        )
        crossing_map.setdefault(leaf["bottom"].key, []).append(bar)
        crossing_map.setdefault(leaf["top"].key, []).append(bar)
        for horizontal in horizontals:
            crossing_map.setdefault(horizontal.key, []).append(bar)
    return fixed, leaf


def _profile_arguments(part: Part, *, inner: bool, clearance: float = 0.0) -> dict[str, Any]:
    width = part.profile.width
    depth = part.profile.depth
    if inner:
        width -= part.profile.wall * 2
        depth -= part.profile.wall * 2
    else:
        width += clearance * 2
        depth += clearance * 2
    if part.profile.kind == "round":
        contour: dict[str, Any] = {"kind": "circle", "radius": width / 2}
    else:
        radius = max(0.0, part.profile.radius - (part.profile.wall if inner else 0.0) + clearance)
        contour = {"kind": "roundedRectangle", "width": depth, "height": width, "radius": radius}
    return {
        "placement": {
            "origin": list(part.start),
            "xAxis": list(part.profile_x_axis),
            "yAxis": list(part.profile_y_axis),
        },
        "contour": contour,
    }


def _emit_tube(model: NeutralModel, part: Part) -> str:
    outer_profile = model.geometry(
        f"{part.key}.profile.outer", "profile2d",
        arguments=_profile_arguments(part, inner=False),
    )
    vector = list(_subtract(part.end, part.start))
    outer = model.geometry(
        f"{part.key}.solid.outer", "extrude", inputs=[outer_profile],
        arguments={"vector": vector},
    )
    inner_profile = model.geometry(
        f"{part.key}.profile.inner", "profile2d",
        arguments=_profile_arguments(part, inner=True),
    )
    inner = model.geometry(
        f"{part.key}.solid.inner", "extrude", inputs=[inner_profile],
        arguments={"vector": vector, "extendStart": 0.1, "extendEnd": 0.1},
    )
    return model.geometry(
        f"{part.key}.solid.tube", "boolean", inputs=[outer, inner],
        arguments={"operation": "subtract", "target": outer, "tools": [inner]},
    )


def _emit_crossing_cutter(
    model: NeutralModel, target: Part, inserted: Part, index: int, clearance: float,
) -> str:
    profile = model.geometry(
        f"{target.key}.through.{index:04d}.profile", "profile2d",
        arguments=_profile_arguments(inserted, inner=False, clearance=clearance),
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
    return {
        "kind": profile.kind,
        "width": profile.width,
        "depth": profile.depth,
        "wallThickness": profile.wall,
        "cornerRadius": profile.radius,
    }


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


def _build_parts(
    parameters: dict[str, Any], points: list[Point], face_names: list[str],
    frame: Profile, horizontal: Profile, vertical: Profile,
    door_surface: DoorSurface | None,
) -> tuple[list[Part], dict[str, list[Part]]]:
    height = _number(parameters, "height")
    faces = [Face(index + 1, face_names[index], points[index], points[index + 1])
             for index in range(len(face_names))]
    parts: list[Part] = []
    counters: dict[str, int] = {}
    crossing_map: dict[str, list[Part]] = {}

    frame_posts: list[Part] = []
    for vertex_index, point in enumerate(points):
        x_axis = _vertex_profile_axis(faces, vertex_index)
        y_axis = (-x_axis[1], x_axis[0], 0.0)
        frame_posts.append(_append_part(
            parts, counters, "outer_frame.vertical", f"外框立柱 {vertex_index + 1}",
            point, (point[0], point[1], height), frame, x_axis, y_axis,
            "outer_frame.vertical", "外框立柱",
        ))

    horizontal_positions = _horizontal_positions(parameters, height)
    horizontal_reserve = _number(parameters, "horizontalBranchReserve")
    vertical_reserve = _number(parameters, "verticalBranchReserve")
    horizontal_reserve = 10.0 if horizontal_reserve < 0 else horizontal_reserve
    vertical_reserve = 10.0 if vertical_reserve < 0 else vertical_reserve
    door_u_min, door_v_min, door_u_max, door_v_max = (
        _door_bounds(parameters) if door_surface is not None else (0.0, 0.0, 0.0, 0.0)
    )
    for face in faces:
        direction, normal = face.direction, face.normal
        has_door = door_surface is not None and door_surface.face_index == face.index
        start_post = frame_posts[face.index - 1]
        end_post = frame_posts[face.index]
        start_extent = _profile_half_extent(start_post, direction)
        end_extent = _profile_half_extent(end_post, direction)
        if face.length <= start_extent + end_extent:
            raise ValueError(f"{face.name}宽度不足以容纳两端外框")
        rail_start = _add(face.start, _scale(direction, start_extent - horizontal_reserve))
        rail_end = _add(face.end, _scale(direction, -(end_extent - horizontal_reserve)))
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
            ))

        face_horizontals: list[tuple[Part, float, float, float]] = []
        for index, z in enumerate(horizontal_positions, start=1):
            if has_door and door_v_min < z < door_v_max:
                segments = (
                    ("start", "起始段", start_extent - horizontal_reserve, door_u_min),
                    ("end", "末端段", door_u_max, face.length - end_extent + horizontal_reserve),
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
                    crossing_map.setdefault(boundary_post.key, []).append(horizontal_part)
            else:
                u_start = start_extent - horizontal_reserve
                u_end = face.length - end_extent + horizontal_reserve
                horizontal_part = _append_part(
                    parts, counters, "main_grid.horizontal", f"{face.name}主横杆 {index}",
                    (rail_start[0], rail_start[1], z), (rail_end[0], rail_end[1], z),
                    horizontal, normal, (0.0, 0.0, 1.0),
                    "main_grid.horizontal", "主横杆", face.index, face.name,
                )
                face_horizontals.append((horizontal_part, u_start, u_end, z))
                crossing_map.setdefault(start_post.key, []).append(horizontal_part)
                crossing_map.setdefault(end_post.key, []).append(horizontal_part)

        vertical_minimum = start_extent
        vertical_maximum = face.length - end_extent
        vertical_count = _vertical_count(
            parameters, vertical_minimum, vertical_maximum,
            vertical.width, "verticalCountPerFace",
        )
        for index, distance in enumerate(_even_positions(
            vertical_count, vertical_minimum, vertical_maximum,
        ), start=1):
            base = _add(face.start, _scale(direction, distance))
            if has_door and door_u_min < distance < door_u_max:
                vertical_specs = (
                    ("bottom", "下段", frame.width - vertical_reserve, door_v_min, (frame_rails[0],)),
                    ("top", "上段", door_v_max, height - frame.width + vertical_reserve, (frame_rails[1],)),
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
                    vertical, direction, normal, "main_grid.vertical", "主竖杆",
                    face.index, face.name,
                )
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
    horizontal_reserve = _number(parameters, "horizontalBranchReserve")
    vertical_reserve = _number(parameters, "verticalBranchReserve")
    horizontal_reserve = 10.0 if horizontal_reserve < 0 else horizontal_reserve
    vertical_reserve = 10.0 if vertical_reserve < 0 else vertical_reserve

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
    door_u_min, door_v_min, door_u_max, door_v_max = (
        _door_bounds(parameters) if door_surface is not None else (0.0, 0.0, 0.0, 0.0)
    )

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
        back_frame = _append_part(
            parts, counters, "outer_frame.back", f"{face_name}后框",
            (*_add(back_origin, _scale(width_direction, rail_start_distance))[:2], z),
            (*_add(back_origin, _scale(width_direction, rail_end_distance))[:2], z),
            frame, (0.0, 0.0, 1.0), depth_direction,
            "outer_frame.back", "外框后梁", face_index, face_name,
        )

        face_crossbars: list[tuple[Part, float, float, float]] = []
        for index, distance in enumerate(crossbar_positions, start=1):
            origin = _add(front_left, _scale(depth_direction, distance))
            if has_door and door_v_min < distance < door_v_max:
                specs = (
                    ("start", "起始段", rail_start_distance, door_u_min, perimeter[1]),
                    ("end", "末端段", door_u_max, rail_end_distance, perimeter[3]),
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
                if boundary is None:
                    crossing_map.setdefault(perimeter[1].key, []).append(crossbar)
                    crossing_map.setdefault(perimeter[3].key, []).append(crossbar)
                else:
                    crossing_map.setdefault(boundary.key, []).append(crossbar)

        for index, distance in enumerate(rod_positions, start=1):
            origin = _add(front_left, _scale(width_direction, distance))
            if has_door and door_u_min < distance < door_u_max:
                rod_specs = (
                    ("front", "前段", rod_start_distance, door_v_min, (perimeter[2],)),
                    ("back", "后段", door_v_max, rod_end_distance, (back_frame,)),
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
                    vertical, width_direction, (0.0, 0.0, 1.0),
                    "cap_grid.vertical", "顶底面纵杆", face_index, face_name,
                )
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
    if height <= frame.width * 2:
        raise ValueError("产品高度必须大于外框宽度的两倍")
    if not (frame.width >= horizontal.width > vertical.width):
        raise ValueError("杆件可见宽度必须满足：外框 >= 横杆 > 竖杆")
    if not 0 <= _integer(parameters, "horizontalCount") <= 100:
        raise ValueError("horizontalCount 超出支持范围")
    if str(parameters.get("verticalLayoutMode", "manual_count")) not in {
        "manual_count", "maximum_clear_gap",
    }:
        raise ValueError("verticalLayoutMode 不支持")
    if _number(parameters, "maximumVerticalClearGap") <= 0:
        raise ValueError("最大竖杆净间距必须大于0")
    if not 0 <= _integer(parameters, "verticalCountPerFace") <= 100:
        raise ValueError("verticalCountPerFace 超出支持范围")
    if layout == "five-face":
        if not 0 <= _integer(parameters, "topBottomCrossbarCount") <= 100:
            raise ValueError("topBottomCrossbarCount 超出支持范围")
        if not 0 <= _integer(parameters, "topBottomRodCount") <= 100:
            raise ValueError("topBottomRodCount 超出支持范围")
    clearance = _number(parameters, "assemblyClearance")
    if clearance < 0:
        raise ValueError("装配间隙不能为负数")
    _validate_through_fit(horizontal, vertical, clearance, "主横杆与主竖杆")
    for key in ("horizontalBranchReserve", "verticalBranchReserve"):
        insertion = _number(parameters, key)
        if insertion > 20 or (insertion < 0 and insertion != -1):
            raise ValueError(f"{key} 必须在0到20 mm之间")
    for index in range(len(points) - 1):
        if math.dist(points[index], points[index + 1]) <= frame.width * 2:
            raise ValueError(f"第 {index + 1} 面宽度必须大于外框宽度的两倍")
    if door_surface is None:
        return

    door_u_min, door_v_min, door_u_max, door_v_max = _door_bounds(parameters)
    if door_u_min <= frame.width or door_u_max >= door_surface.u_length - frame.width:
        raise ValueError("检修口横向范围必须保持在所选面的外框以内")
    if door_v_min <= frame.width or door_v_max >= door_surface.v_length - frame.width:
        raise ValueError("检修口纵向范围必须保持在所选面的外框以内")
    fixed_profile = _profile(parameters, "doorFrame")
    leaf_profile = _profile(parameters, "doorLeafFrame")
    door_horizontal = _profile(parameters, "doorHorizontal")
    door_vertical = _profile(parameters, "doorVertical")
    _validate_through_fit(door_horizontal, door_vertical, clearance, "门内横杆与门内竖杆")
    gap = _number(parameters, "doorGap")
    required = fixed_profile.width + leaf_profile.width + gap * 2
    if gap < 0 or _number(parameters, "doorWidth") <= required or _number(parameters, "doorHeight") <= required:
        raise ValueError("检修口尺寸不足以容纳固定门框、活动门扇和装配间隙")
    if str(parameters.get("doorHingeSide")) not in {"left", "right"}:
        raise ValueError("铰链侧仅支持左侧或右侧")
    if not 1 <= _integer(parameters, "doorHingeCount") <= 10:
        raise ValueError("铰链数量超出支持范围")
    for key in ("doorHorizontalCount", "doorVerticalCount"):
        if not 0 <= _integer(parameters, key) <= 100:
            raise ValueError(f"{key} 超出支持范围")


def generate_multi_face(
    parameters: dict[str, Any], context: dict[str, Any], *,
    template_id: str, template_version: str, layout: str,
) -> dict[str, Any]:
    points, face_names = _footprint(layout, parameters)
    frame = _profile(parameters, "frame")
    horizontal = _profile(parameters, "horizontal")
    vertical = _profile(parameters, "vertical")
    door_surface = _resolve_door_surface(layout, parameters, points, face_names, frame)
    _validate(layout, parameters, points, frame, horizontal, vertical, door_surface)

    template = context["template"]
    model = NeutralModel(
        template_id=template_id,
        template_version=template_version,
        package_digest=str(template.get("packageDigest", "")),
        parameters=parameters,
    )
    parts, crossing_map = _build_parts(
        parameters, points, face_names, frame, horizontal, vertical, door_surface,
    )
    counters: dict[str, int] = {}
    for part in parts:
        role, _, suffix = part.key.rpartition(".")
        counters[role] = max(counters.get(role, 0), int(suffix))
    if layout == "five-face":
        _append_five_face_caps(
            parameters, points, frame, horizontal, vertical,
            parts, counters, crossing_map, door_surface,
        )
    door_frames: tuple[dict[str, Part], dict[str, Part]] | None = None
    if door_surface is not None:
        door_frames = _append_access_door(
            parameters, door_surface, parts, counters, crossing_map,
        )
    raw_geometry = {part.key: _emit_tube(model, part) for part in parts}
    clearance = _number(parameters, "assemblyClearance")
    representations: dict[str, str] = {}
    for part in parts:
        display = raw_geometry[part.key]
        cutters = [
            _emit_crossing_cutter(model, part, inserted, index, clearance)
            for index, inserted in enumerate(crossing_map.get(part.key, []), start=1)
        ]
        if cutters:
            display = model.geometry(
                f"{part.key}.solid.final", "boolean", inputs=[display, *cutters],
                arguments={"operation": "subtract", "target": display, "tools": cutters},
            )
        representations[part.key] = display

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
            "tubeDesigner.endProcess": {"startCut": "square", "endCut": "square"},
        }
        item_key = model.item(
            part.key, part.name,
            representations={"display": representations[part.key], "export": representations[part.key]},
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
        hinge_u = _door_bounds(parameters)[0 if hinge_side == "left" else 2]
        _, door_v_min, _, door_v_max = _door_bounds(parameters)
        hinge_count = _integer(parameters, "doorHingeCount")
        for index in range(hinge_count):
            hinge_v = door_v_min + (door_v_max - door_v_min) * (index + 1) / (hinge_count + 1)
            model.relationship(
                f"access_door.hinge.{index + 1:04d}", "hinge",
                [fixed[hinge_side].key, leaf[hinge_side].key],
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
    return model.build()
