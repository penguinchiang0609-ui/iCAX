from __future__ import annotations

from dataclasses import dataclass
import math
from typing import Any

from icax_template_sdk import NeutralModel


TEMPLATE_ID = "single-face-security-window"
TEMPLATE_VERSION = "2.5.0"


@dataclass(frozen=True)
class Profile:
    kind: str
    width: float
    depth: float
    wall: float
    radius: float = 0.0


@dataclass(frozen=True)
class CornerProcess:
    join_type: str
    groove_style: str = ""
    butt_wrap: str = "side_wraps_horizontal"


@dataclass(frozen=True)
class Part:
    key: str
    name: str
    start: tuple[float, float, float]
    end: tuple[float, float, float]
    profile: Profile
    group: str
    category_key: str = ""
    category_name: str = ""
    start_cut: str = "square"
    end_cut: str = "square"

    @property
    def length(self) -> float:
        return math.dist(self.start, self.end)

    @property
    def vertical(self) -> bool:
        return abs(self.end[2] - self.start[2]) > abs(self.end[0] - self.start[0])


@dataclass(frozen=True)
class ContinuousFrame:
    key: str
    name: str
    left: float
    bottom: float
    right: float
    top: float
    profile: Profile
    group: str
    process: CornerProcess
    parameters: dict[str, Any]

    @property
    def bend_allowance(self) -> float:
        return math.pi * 0.5 * float(self.parameters["vGrooveKFactor"]) * self.profile.wall

    @property
    def horizontal_run(self) -> float:
        return self.right - self.left - self.profile.wall * 2

    @property
    def vertical_run(self) -> float:
        return self.top - self.bottom - self.profile.wall * 2

    @property
    def length(self) -> float:
        return 2 * self.horizontal_run + 2 * self.vertical_run + 4 * self.bend_allowance


ModelItem = Part | ContinuousFrame


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


def _process(parameters: dict[str, Any], join_key: str, butt_key: str) -> CornerProcess:
    encoded = str(parameters[join_key])
    if encoded.startswith("v_groove_90:"):
        join_type, style = encoded.split(":", 1)
        if style not in {"sharp_v", "rounded_v", "left_arc", "right_arc"}:
            raise ValueError(f"{join_key} 的 V 槽样式不支持：{style}")
        return CornerProcess(join_type, style, str(parameters[butt_key]))
    if encoded not in {"miter_45", "butt_90"}:
        raise ValueError(f"{join_key} 的连接工艺不支持：{encoded}")
    butt_wrap = str(parameters[butt_key])
    if butt_wrap not in {"side_wraps_horizontal", "horizontal_wraps_side"}:
        raise ValueError(f"{butt_key} 的包边方向不支持：{butt_wrap}")
    return CornerProcess(encoded, "", butt_wrap)


def _even_positions(count: int, minimum: float, maximum: float) -> list[float]:
    if count == 0:
        return []
    if maximum <= minimum:
        raise ValueError("杆件布置没有可用空间")
    step = (maximum - minimum) / (count + 1)
    return [minimum + step * (index + 1) for index in range(count)]


def _vertical_count(
    parameters: dict[str, Any], minimum: float, maximum: float, bar_width: float,
) -> int:
    mode = str(parameters.get("verticalLayoutMode", "manual_count"))
    if mode == "manual_count":
        return _integer(parameters, "middleVerticalCount")
    if mode != "maximum_clear_gap":
        raise ValueError(f"不支持的竖杆布置方式：{mode}")
    maximum_gap = _number(parameters, "maximumVerticalClearGap")
    span = maximum - minimum
    if span <= 0:
        raise ValueError("竿件布置没有可用空间")
    count = max(0, math.ceil(span / (maximum_gap + bar_width / 2) - 1 - 1.0e-9))
    if count > 100:
        raise ValueError("按最大净间距计算的竖杆数量超过100")
    return count


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


def _add_part(
    items: list[ModelItem], counters: dict[str, int], role: str, name: str,
    start: tuple[float, float, float], end: tuple[float, float, float],
    profile: Profile, group: str = "main", start_cut: str = "square",
    end_cut: str = "square", category_key: str = "", category_name: str = "",
) -> Part | None:
    if math.dist(start, end) <= 1.0e-7:
        return None
    part = Part(
        key=_next_key(counters, role), name=name, start=start, end=end,
        profile=profile, group=group, category_key=category_key or role,
        category_name=category_name or name, start_cut=start_cut, end_cut=end_cut,
    )
    items.append(part)
    return part


def _add_processed_rectangle(
    items: list[ModelItem], counters: dict[str, int], prefix: str, name: str,
    left: float, bottom: float, right: float, top: float, profile: Profile,
    group: str, process: CornerProcess, parameters: dict[str, Any],
) -> None:
    if right - left <= profile.width * 2 or top - bottom <= profile.width * 2:
        raise ValueError(f"{name}尺寸不足以形成封闭框")
    if process.join_type == "v_groove_90":
        role = f"{prefix}.continuous"
        items.append(ContinuousFrame(
            _next_key(counters, role), name, left, bottom, right, top,
            profile, group, process, parameters,
        ))
        return

    vertical_bottom, vertical_top = bottom, top
    horizontal_left, horizontal_right = left, right
    if process.join_type == "butt_90" and process.butt_wrap == "side_wraps_horizontal":
        horizontal_left += profile.width
        horizontal_right -= profile.width
    elif process.join_type == "butt_90":
        vertical_bottom += profile.width
        vertical_top -= profile.width
    cut = "miter-45" if process.join_type == "miter_45" else "square"
    half = profile.width / 2
    _add_part(items, counters, f"{prefix}.left", f"{name}左边",
              (left + half, 0, vertical_bottom), (left + half, 0, vertical_top),
              profile, group, cut, cut, f"{prefix}.vertical", f"{name}竖边")
    _add_part(items, counters, f"{prefix}.right", f"{name}右边",
              (right - half, 0, vertical_bottom), (right - half, 0, vertical_top),
              profile, group, cut, cut, f"{prefix}.vertical", f"{name}竖边")
    _add_part(items, counters, f"{prefix}.bottom", f"{name}下边",
              (horizontal_left, 0, bottom + half), (horizontal_right, 0, bottom + half),
              profile, group, cut, cut, f"{prefix}.horizontal", f"{name}横边")
    _add_part(items, counters, f"{prefix}.top", f"{name}上边",
              (horizontal_left, 0, top - half), (horizontal_right, 0, top - half),
              profile, group, cut, cut, f"{prefix}.horizontal", f"{name}横边")


def _profile_arguments(
    start: tuple[float, float, float], end: tuple[float, float, float],
    profile: Profile, *, inner: bool, clearance: float = 0.0,
) -> dict[str, Any]:
    horizontal = abs(end[0] - start[0]) >= abs(end[2] - start[2])
    if horizontal:
        x_axis, y_axis = [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]
        contour_width, contour_height = profile.depth, profile.width
    else:
        x_axis, y_axis = [1.0, 0.0, 0.0], [0.0, 1.0, 0.0]
        contour_width, contour_height = profile.width, profile.depth
    if inner:
        contour_width -= profile.wall * 2
        contour_height -= profile.wall * 2
    else:
        contour_width += clearance * 2
        contour_height += clearance * 2
    if profile.kind == "round":
        contour: dict[str, Any] = {"kind": "circle", "radius": contour_width / 2}
    else:
        radius = max(0.0, profile.radius - (profile.wall if inner else 0.0) + clearance)
        contour = {
            "kind": "roundedRectangle", "width": contour_width,
            "height": contour_height, "radius": radius,
        }
    return {
        "placement": {"origin": list(start), "xAxis": x_axis, "yAxis": y_axis},
        "contour": contour,
    }


def _emit_tube_geometry(model: NeutralModel, part: Part) -> tuple[str, str]:
    outer_profile = model.geometry(
        f"{part.key}.profile.outer", "profile2d",
        arguments=_profile_arguments(part.start, part.end, part.profile, inner=False),
    )
    vector = [part.end[index] - part.start[index] for index in range(3)]
    outer = model.geometry(
        f"{part.key}.solid.outer", "extrude", inputs=[outer_profile],
        arguments={"vector": vector},
    )
    inner_profile = model.geometry(
        f"{part.key}.profile.inner", "profile2d",
        arguments=_profile_arguments(part.start, part.end, part.profile, inner=True),
    )
    inner = model.geometry(
        f"{part.key}.solid.inner", "extrude", inputs=[inner_profile],
        arguments={"vector": vector, "extendStart": 1.0, "extendEnd": 1.0},
    )
    hollow = model.geometry(
        f"{part.key}.solid.hollow", "boolean", inputs=[outer, inner],
        arguments={"operation": "subtract", "target": outer, "tools": [inner]},
    )
    return outer, hollow


def _quadratic_points(
    start: tuple[float, float], control: tuple[float, float],
    end: tuple[float, float], segments: int = 8,
) -> list[list[float]]:
    result: list[list[float]] = []
    for index in range(segments + 1):
        t = index / segments
        u = 1.0 - t
        result.append([
            u * u * start[0] + 2 * u * t * control[0] + t * t * end[0],
            u * u * start[1] + 2 * u * t * control[1] + t * t * end[1],
        ])
    return result


def _groove_polygon(frame: ContinuousFrame, center: float) -> list[list[float]]:
    profile = frame.profile
    wall = max(0.1, min(profile.wall, min(profile.width, profile.depth) / 2 - 0.1))
    requested = _number(frame.parameters, "vGrooveBottomDistance")
    distance = max(0.1, min(requested if requested > 0 else wall, profile.width - wall))
    root = profile.width / 2 - distance
    base = -profile.width / 2 - wall
    depth = max(root - base, 0.1)
    half_slot = max(depth, wall * 2)
    top_y, root_y = -base, -root
    style = frame.process.groove_style

    if bool(frame.parameters["vGrooveMaleFemale"]):
        feature = min(max(wall, 0.1), half_slot * 0.95)
        ledge = top_y - min(wall * 2, depth * 0.95)
        return [
            [center - half_slot + feature, top_y], [center + half_slot + feature, top_y],
            [center + half_slot + feature, ledge], [center + half_slot - feature, ledge],
            [center, root_y], [center - half_slot + feature, ledge],
        ]
    if style == "rounded_v":
        radius = _number(frame.parameters, "vGrooveRadius")
        radius = min(max(radius if radius > 0 else wall * 0.5, 0.1), depth * 0.45)
        arc = []
        arc_center_y = root_y + radius
        for index in range(9):
            angle = math.pi + math.pi * index / 8
            arc.append([center + radius * math.cos(angle), arc_center_y + radius * math.sin(angle)])
        return [[center - half_slot, top_y], *arc, [center + half_slot, top_y]]
    if style == "left_arc":
        curved = _quadratic_points(
            (center - half_slot, top_y), (center - half_slot, root_y), (center, root_y),
        )
        return [*curved, [center + half_slot, top_y]]
    if style == "right_arc":
        curved = _quadratic_points(
            (center, root_y), (center + half_slot, root_y), (center + half_slot, top_y),
        )
        return [[center - half_slot, top_y], *curved]
    return [[center - half_slot, top_y], [center, root_y], [center + half_slot, top_y]]


def _emit_polygon_cutter(
    model: NeutralModel, key: str, points: list[list[float]], half_depth: float,
) -> str:
    profile = model.geometry(
        f"{key}.profile", "profile2d",
        arguments={
            "placement": {
                "origin": [0.0, -half_depth, 0.0],
                "xAxis": [1.0, 0.0, 0.0], "yAxis": [0.0, 0.0, 1.0],
            },
            "contour": {"kind": "polygon", "points": points},
        },
    )
    return model.geometry(
        f"{key}.solid", "extrude", inputs=[profile],
        arguments={"vector": [0.0, half_depth * 2, 0.0]},
    )


def _emit_continuous_frame_geometry(
    model: NeutralModel, frame: ContinuousFrame,
) -> tuple[str, str, list[float]]:
    half = frame.profile.width / 2
    preview_parts = [
        Part(f"{frame.key}.display.left", "", (frame.left + half, 0, frame.bottom), (frame.left + half, 0, frame.top), frame.profile, frame.group),
        Part(f"{frame.key}.display.right", "", (frame.right - half, 0, frame.bottom), (frame.right - half, 0, frame.top), frame.profile, frame.group),
        Part(f"{frame.key}.display.bottom", "", (frame.left, 0, frame.bottom + half), (frame.right, 0, frame.bottom + half), frame.profile, frame.group),
        Part(f"{frame.key}.display.top", "", (frame.left, 0, frame.top - half), (frame.right, 0, frame.top - half), frame.profile, frame.group),
    ]
    display_shapes = [_emit_tube_geometry(model, part)[1] for part in preview_parts]
    display = model.geometry(
        f"{frame.key}.display.compound", "compound", inputs=display_shapes,
    )

    base = Part(
        f"{frame.key}.export.base", "", (0.0, 0.0, 0.0), (frame.length, 0.0, 0.0),
        frame.profile, frame.group,
    )
    _, export_shape = _emit_tube_geometry(model, base)
    centers: list[float] = []
    cursor = 0.0
    for segment in (frame.horizontal_run / 2, frame.vertical_run, frame.horizontal_run, frame.vertical_run):
        center = cursor + segment + frame.bend_allowance / 2
        centers.append(center)
        cursor += segment + frame.bend_allowance

    half_tool_depth = (frame.profile.depth + max(frame.profile.wall * 4, 10.0)) / 2
    cutters: list[str] = []
    for index, center in enumerate(centers, start=1):
        prefix = f"{frame.key}.export.groove.{index:04d}"
        cutters.append(_emit_polygon_cutter(
            model, prefix, _groove_polygon(frame, center), half_tool_depth,
        ))
        if frame.process.groove_style == "sharp_v" and bool(frame.parameters["vGrooveBottomCut"]):
            root = -(frame.profile.width / 2 - max(0.1, _number(frame.parameters, "vGrooveBottomDistance")))
            size = max(frame.profile.wall * 4, _number(frame.parameters, "vGrooveRadius") * 1.5, 1.0)
            cutters.append(_emit_polygon_cutter(model, f"{prefix}.bottom_cut", [
                [center - size / 2, root - frame.profile.wall],
                [center + size / 2, root - frame.profile.wall],
                [center + size / 2, root + frame.profile.wall],
                [center - size / 2, root + frame.profile.wall],
            ], half_tool_depth))
        if frame.process.groove_style == "sharp_v" and bool(frame.parameters["vGrooveReliefHole"]):
            radius = _number(frame.parameters, "vGrooveReliefDiameter") / 2
            if radius <= 0:
                radius = max(_number(frame.parameters, "vGrooveRadius"), frame.profile.wall * 2)
            root_y = -(frame.profile.width / 2 - max(0.1, _number(frame.parameters, "vGrooveBottomDistance")))
            hole_profile = model.geometry(
                f"{prefix}.relief.profile", "profile2d",
                arguments={
                    "placement": {
                        "origin": [center, -half_tool_depth, root_y],
                        "xAxis": [1.0, 0.0, 0.0], "yAxis": [0.0, 0.0, 1.0],
                    },
                    "contour": {"kind": "circle", "radius": radius},
                },
            )
            length = frame.profile.depth / 2 + radius * 2 if bool(frame.parameters["vGrooveReliefNoThrough"]) else half_tool_depth * 2
            cutters.append(model.geometry(
                f"{prefix}.relief.solid", "extrude", inputs=[hole_profile],
                arguments={"vector": [0.0, length, 0.0]},
            ))
        if frame.process.groove_style == "sharp_v" and bool(frame.parameters["vGrooveWallOvercut"]):
            root_y = -(frame.profile.width / 2 - max(0.1, _number(frame.parameters, "vGrooveBottomDistance")))
            width = max(frame.profile.wall, 0.2)
            cutters.append(_emit_polygon_cutter(model, f"{prefix}.wall_overcut", [
                [center - width, root_y - width], [center + width, root_y - width],
                [center + width, root_y + frame.profile.wall],
                [center - width, root_y + frame.profile.wall],
            ], half_tool_depth))
    if cutters:
        export_shape = model.geometry(
            f"{frame.key}.export.final", "boolean", inputs=[export_shape, *cutters],
            arguments={"operation": "subtract", "target": export_shape, "tools": cutters},
        )
    return display, export_shape, centers


def _crossings(parts: list[Part]) -> dict[str, list[Part]]:
    result: dict[str, list[Part]] = {}
    verticals = [part for part in parts if part.vertical]
    horizontals = [part for part in parts if not part.vertical]
    for vertical in verticals:
        x = (vertical.start[0] + vertical.end[0]) / 2
        for horizontal in horizontals:
            z = (horizontal.start[2] + horizontal.end[2]) / 2
            if not min(horizontal.start[0], horizontal.end[0]) - 1.0e-7 <= x <= max(horizontal.start[0], horizontal.end[0]) + 1.0e-7:
                continue
            if not min(vertical.start[2], vertical.end[2]) - 1.0e-7 <= z <= max(vertical.start[2], vertical.end[2]) + 1.0e-7:
                continue
            vertical_leaf = vertical.group == "access_door.leaf"
            horizontal_leaf = horizontal.group == "access_door.leaf"
            if vertical_leaf != horizontal_leaf:
                continue
            vertical_size = max(vertical.profile.width, vertical.profile.depth)
            horizontal_size = max(horizontal.profile.width, horizontal.profile.depth)
            target, inserted = (horizontal, vertical) if horizontal_size >= vertical_size else (vertical, horizontal)
            result.setdefault(target.key, []).append(inserted)
    return result


def _emit_crossing_cutter(
    model: NeutralModel, target: Part, inserted: Part, index: int, clearance: float,
) -> str:
    key = f"{target.key}.through.{index:04d}"
    profile_key = model.geometry(
        f"{key}.profile", "profile2d",
        arguments=_profile_arguments(
            inserted.start, inserted.end, inserted.profile, inner=False, clearance=clearance,
        ),
    )
    vector = [inserted.end[i] - inserted.start[i] for i in range(3)]
    return model.geometry(
        f"{key}.solid", "extrude", inputs=[profile_key],
        arguments={"vector": vector, "extendStart": clearance + 1.0, "extendEnd": clearance + 1.0},
    )


def _miter_triangles(part: Part) -> list[list[list[float]]]:
    width = part.profile.width
    half = width / 2
    min_x = min(part.start[0], part.end[0]) - (0 if not part.vertical else half)
    max_x = max(part.start[0], part.end[0]) + (0 if not part.vertical else half)
    min_z = min(part.start[2], part.end[2]) - (half if not part.vertical else 0)
    max_z = max(part.start[2], part.end[2]) + (half if not part.vertical else 0)
    if ".left." in part.key:
        return [
            [[min_x, min_z], [max_x, min_z], [max_x, min_z + width]],
            [[min_x, max_z], [max_x, max_z], [max_x, max_z - width]],
        ]
    if ".right." in part.key:
        return [
            [[max_x, min_z], [min_x, min_z], [min_x, min_z + width]],
            [[max_x, max_z], [min_x, max_z], [min_x, max_z - width]],
        ]
    if ".bottom." in part.key:
        return [
            [[min_x, min_z], [min_x, max_z], [min_x + width, max_z]],
            [[max_x, min_z], [max_x, max_z], [max_x - width, max_z]],
        ]
    if ".top." in part.key:
        return [
            [[min_x, max_z], [min_x, min_z], [min_x + width, min_z]],
            [[max_x, max_z], [max_x, min_z], [max_x - width, min_z]],
        ]
    raise ValueError(f"无法为非框件生成 45° 拼角：{part.key}")


def _emit_miter_cutters(model: NeutralModel, part: Part) -> list[str]:
    half_depth = part.profile.depth / 2 + max(part.profile.wall * 2, 2.0)
    return [
        _emit_polygon_cutter(
            model, f"{part.key}.miter.{index:04d}", triangle, half_depth,
        )
        for index, triangle in enumerate(_miter_triangles(part), start=1)
    ]


def _profile_properties(profile: Profile) -> dict[str, Any]:
    return {
        "kind": profile.kind, "width": profile.width, "depth": profile.depth,
        "wallThickness": profile.wall, "cornerRadius": profile.radius,
    }


def _frame_relationship_item(
    items: list[ModelItem], group: str, prefix: str, side: str,
) -> str:
    candidates = [item for item in items if item.group == group and item.key.startswith(prefix)]
    for item in candidates:
        if isinstance(item, ContinuousFrame):
            return item.key
        if f".{side}." in item.key:
            return item.key
    raise ValueError(f"{prefix} 缺少铰链侧零件")


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


def _validate(parameters: dict[str, Any]) -> None:
    width, height = _number(parameters, "width"), _number(parameters, "height")
    frame = _profile(parameters, "frame")
    horizontal = _profile(parameters, "horizontal")
    vertical = _profile(parameters, "vertical")
    if width <= frame.width * 2 or height <= frame.width * 2:
        raise ValueError("产品宽高必须大于外框宽度的两倍")
    if not (frame.width >= horizontal.width > vertical.width):
        raise ValueError("杆件可见宽度必须满足：外框 >= 横杆 > 竖杆")
    clearance = _number(parameters, "assemblyClearance")
    if clearance < 0:
        raise ValueError("装配间隙不能为负数")
    _validate_through_fit(horizontal, vertical, clearance, "主横杆与主竖杆")
    if str(parameters.get("verticalLayoutMode", "manual_count")) not in {
        "manual_count", "maximum_clear_gap",
    }:
        raise ValueError("verticalLayoutMode 不支持")
    if _number(parameters, "maximumVerticalClearGap") <= 0:
        raise ValueError("最大竖杆净间距必须大于0")
    for key in ("horizontalBranchReserve", "verticalBranchReserve"):
        insertion = _number(parameters, key)
        if insertion > 20 or (insertion < 0 and insertion != -1):
            raise ValueError(f"{key} 必须在0到20 mm之间")
    for key in ("horizontalCount", "middleVerticalCount", "doorHorizontalCount", "doorVerticalCount"):
        if not 0 <= _integer(parameters, key) <= 100:
            raise ValueError(f"{key} 超出支持范围")
    if not 1 <= _integer(parameters, "doorHingeCount") <= 10:
        raise ValueError("doorHingeCount 超出支持范围")
    if parameters["doorHingeSide"] not in {"left", "right"}:
        raise ValueError("doorHingeSide 不支持")
    processes = [
        _process(parameters, "frameJoinType", "frameButtWrapMode"),
        _process(parameters, "doorFrameJoinType", "doorFrameButtWrapMode"),
        _process(parameters, "doorLeafFrameJoinType", "doorLeafFrameButtWrapMode"),
    ]
    if any(process.join_type == "v_groove_90" and process.groove_style != "sharp_v" for process in processes):
        if any(bool(parameters[key]) for key in ("vGrooveBottomCut", "vGrooveReliefHole", "vGrooveWallOvercut")):
            raise ValueError("底部切除、释放孔和壁厚过切仅适用于尖角 V 槽")
    if not 0 <= _number(parameters, "vGrooveKFactor") <= 1:
        raise ValueError("展开 K 因子必须在 0 到 1 之间")
    if _number(parameters, "vGrooveBottomDistance") >= frame.width - frame.wall:
        raise ValueError("V 槽底距离无效")
    if not parameters["accessDoorEnabled"]:
        return
    left, bottom = _number(parameters, "doorLeft"), _number(parameters, "doorBottom")
    door_width, door_height = _number(parameters, "doorWidth"), _number(parameters, "doorHeight")
    if left <= frame.width or left + door_width >= width - frame.width:
        raise ValueError("检修门必须保持在产品宽度范围内")
    if bottom <= frame.width or bottom + door_height >= height - frame.width:
        raise ValueError("检修门必须保持在产品高度范围内")
    door_frame = _profile(parameters, "doorFrame")
    leaf_frame = _profile(parameters, "doorLeafFrame")
    door_horizontal = _profile(parameters, "doorHorizontal")
    door_vertical = _profile(parameters, "doorVertical")
    _validate_through_fit(door_horizontal, door_vertical, clearance, "门内横杆与门内竖杆")
    gap = _number(parameters, "doorGap")
    required = door_frame.width + leaf_frame.width + gap * 2
    if gap < 0 or door_width <= required or door_height <= required:
        raise ValueError("检修门尺寸不足以容纳门框、门扇框和间隙")


def generate(parameters: dict[str, Any], context: dict[str, Any]) -> dict[str, Any]:
    _validate(parameters)
    template = context["template"]
    model = NeutralModel(
        template_id=TEMPLATE_ID, template_version=TEMPLATE_VERSION,
        package_digest=str(template.get("packageDigest", "")), parameters=parameters,
    )
    width, height = _number(parameters, "width"), _number(parameters, "height")
    frame = _profile(parameters, "frame")
    horizontal = _profile(parameters, "horizontal")
    vertical = _profile(parameters, "vertical")
    layout = str(parameters["frameLayout"])
    items: list[ModelItem] = []
    counters: dict[str, int] = {}
    half = frame.width / 2

    if layout == "four_sides":
        _add_processed_rectangle(
            items, counters, "outer_frame", "大外框", 0, 0, width, height,
            frame, "main", _process(parameters, "frameJoinType", "frameButtWrapMode"), parameters,
        )
    else:
        if layout == "left_right":
            _add_part(items, counters, "outer_frame.left", "大外框左边", (half, 0, 0), (half, 0, height), frame,
                      category_key="outer_frame.vertical", category_name="大外框竖边")
            _add_part(items, counters, "outer_frame.right", "大外框右边", (width - half, 0, 0), (width - half, 0, height), frame,
                      category_key="outer_frame.vertical", category_name="大外框竖边")
        elif layout == "top_bottom":
            _add_part(items, counters, "outer_frame.bottom", "大外框下边", (0, 0, half), (width, 0, half), frame,
                      category_key="outer_frame.horizontal", category_name="大外框横边")
            _add_part(items, counters, "outer_frame.top", "大外框上边", (0, 0, height - half), (width, 0, height - half), frame,
                      category_key="outer_frame.horizontal", category_name="大外框横边")
        else:
            raise ValueError(f"不支持的外框布置：{layout}")

    horizontal_reserve = _number(parameters, "horizontalBranchReserve")
    vertical_reserve = _number(parameters, "verticalBranchReserve")
    horizontal_reserve = 10.0 if horizontal_reserve < 0 else horizontal_reserve
    vertical_reserve = 10.0 if vertical_reserve < 0 else vertical_reserve
    has_side_frame = layout != "top_bottom"
    has_top_bottom_frame = layout != "left_right"
    horizontal_start = frame.width - horizontal_reserve if has_side_frame else 0.0
    horizontal_end = width - frame.width + horizontal_reserve if has_side_frame else width
    vertical_start = frame.width - vertical_reserve if has_top_bottom_frame else 0.0
    vertical_end = height - frame.width + vertical_reserve if has_top_bottom_frame else height
    door_enabled = bool(parameters["accessDoorEnabled"])
    door_left, door_bottom = _number(parameters, "doorLeft"), _number(parameters, "doorBottom")
    door_right = door_left + _number(parameters, "doorWidth")
    door_top = door_bottom + _number(parameters, "doorHeight")

    for index, z in enumerate(_horizontal_positions(parameters, height), start=1):
        if door_enabled and door_bottom < z < door_top:
            _add_part(items, counters, "main_grid.horizontal.left", f"主横杆 {index} 左段", (horizontal_start, 0, z), (door_left, 0, z), horizontal,
                      category_key="main_grid.horizontal", category_name="主横杆")
            _add_part(items, counters, "main_grid.horizontal.right", f"主横杆 {index} 右段", (door_right, 0, z), (horizontal_end, 0, z), horizontal,
                      category_key="main_grid.horizontal", category_name="主横杆")
        else:
            _add_part(items, counters, "main_grid.horizontal", f"主横杆 {index}", (horizontal_start, 0, z), (horizontal_end, 0, z), horizontal,
                      category_key="main_grid.horizontal", category_name="主横杆")

    vertical_left = frame.width if has_side_frame else 0.0
    vertical_right = width - frame.width if has_side_frame else width
    vertical_count = _vertical_count(
        parameters, vertical_left, vertical_right, vertical.width,
    )
    for index, x in enumerate(_even_positions(vertical_count, vertical_left, vertical_right), start=1):
        if door_enabled and door_left < x < door_right:
            _add_part(items, counters, "main_grid.vertical.bottom", f"主竖杆 {index} 下段", (x, 0, vertical_start), (x, 0, door_bottom), vertical,
                      category_key="main_grid.vertical", category_name="主竖杆")
            _add_part(items, counters, "main_grid.vertical.top", f"主竖杆 {index} 上段", (x, 0, door_top), (x, 0, vertical_end), vertical,
                      category_key="main_grid.vertical", category_name="主竖杆")
        else:
            _add_part(items, counters, "main_grid.vertical", f"主竖杆 {index}", (x, 0, vertical_start), (x, 0, vertical_end), vertical,
                      category_key="main_grid.vertical", category_name="主竖杆")

    if door_enabled:
        door_frame = _profile(parameters, "doorFrame")
        leaf_frame = _profile(parameters, "doorLeafFrame")
        _add_processed_rectangle(
            items, counters, "access_door.fixed_frame", "固定门框",
            door_left, door_bottom, door_right, door_top, door_frame,
            "access_door.fixed_frame",
            _process(parameters, "doorFrameJoinType", "doorFrameButtWrapMode"), parameters,
        )
        inset = door_frame.width / 2 + _number(parameters, "doorGap") + leaf_frame.width / 2
        leaf_left, leaf_right = door_left + inset, door_right - inset
        leaf_bottom, leaf_top = door_bottom + inset, door_top - inset
        _add_processed_rectangle(
            items, counters, "access_door.leaf.frame", "活动门扇框",
            leaf_left, leaf_bottom, leaf_right, leaf_top, leaf_frame,
            "access_door.leaf",
            _process(parameters, "doorLeafFrameJoinType", "doorLeafFrameButtWrapMode"), parameters,
        )
        inner_left, inner_right = leaf_left + leaf_frame.width / 2, leaf_right - leaf_frame.width / 2
        inner_bottom, inner_top = leaf_bottom + leaf_frame.width / 2, leaf_top - leaf_frame.width / 2
        door_horizontal = _profile(parameters, "doorHorizontal")
        door_vertical = _profile(parameters, "doorVertical")
        for index, z in enumerate(_even_positions(_integer(parameters, "doorHorizontalCount"), inner_bottom, inner_top), start=1):
            _add_part(items, counters, "access_door.leaf.horizontal", f"门内横杆 {index}", (inner_left, 0, z), (inner_right, 0, z), door_horizontal, "access_door.leaf",
                      category_name="门内横杆")
        for index, x in enumerate(_even_positions(_integer(parameters, "doorVerticalCount"), inner_left, inner_right), start=1):
            _add_part(items, counters, "access_door.leaf.vertical", f"门内竖杆 {index}", (x, 0, inner_bottom), (x, 0, inner_top), door_vertical, "access_door.leaf",
                      category_name="门内竖杆")

    straight_parts = [item for item in items if isinstance(item, Part)]
    crossing_map = _crossings(straight_parts)
    raw_geometry: dict[str, tuple[str, str]] = {
        part.key: _emit_tube_geometry(model, part) for part in straight_parts
    }
    representations: dict[str, tuple[str, str]] = {}
    clearance = _number(parameters, "assemblyClearance")
    for part in straight_parts:
        display = raw_geometry[part.key][1]
        cutters = [
            _emit_crossing_cutter(model, part, inserted, index, clearance)
            for index, inserted in enumerate(crossing_map.get(part.key, []), start=1)
        ]
        if part.start_cut == "miter-45" or part.end_cut == "miter-45":
            cutters.extend(_emit_miter_cutters(model, part))
        if cutters:
            display = model.geometry(
                f"{part.key}.solid.final", "boolean", inputs=[display, *cutters],
                arguments={"operation": "subtract", "target": display, "tools": cutters},
            )
        representations[part.key] = (display, display)

    bend_locations: dict[str, list[float]] = {}
    for item in items:
        if isinstance(item, ContinuousFrame):
            display, export, locations = _emit_continuous_frame_geometry(model, item)
            representations[item.key] = (display, export)
            bend_locations[item.key] = locations

    item_keys: list[str] = []
    rows: list[dict[str, Any]] = []
    for index, item in enumerate(items, start=1):
        display, export = representations[item.key]
        properties: dict[str, Any] = {
            "partNumber": f"{parameters['productCode']}-{index:03d}",
            "quantity": 1, "group": item.group, "length": round(item.length, 3),
            "tubeDesigner.profile": _profile_properties(item.profile),
        }
        if isinstance(item, ContinuousFrame):
            properties["manufacturing.categoryName"] = item.name
            properties["manufacturing.categoryKey"] = item.key.rsplit(".", 1)[0]
            properties["tubeDesigner.cornerProcess"] = {
                "joinType": item.process.join_type,
                "grooveStyle": item.process.groove_style,
                "bendAllowance": item.bend_allowance,
                "bendLocations": bend_locations[item.key],
            }
        else:
            properties["manufacturing.categoryName"] = item.category_name or item.name
            properties["manufacturing.categoryKey"] = item.category_key or item.key.rsplit(".", 1)[0]
            properties["tubeDesigner.endProcess"] = {
                "startCut": item.start_cut, "endCut": item.end_cut,
            }
        item_key = model.item(
            item.key, item.name, representations={"display": display, "export": export},
            properties=properties,
        )
        item_keys.append(item_key)
        rows.append({
            "key": f"row.{item.key}", "parentKey": str(parameters["productCode"]),
            "itemKey": item_key,
            "values": {
                "partNumber": f"{parameters['productCode']}-{index:03d}",
                "name": item.name, "quantity": 1, "length": round(item.length, 3),
            },
        })

    if door_enabled:
        hinge_side = str(parameters["doorHingeSide"])
        fixed_item = _frame_relationship_item(
            items, "access_door.fixed_frame", "access_door.fixed_frame", hinge_side,
        )
        moving_item = _frame_relationship_item(
            items, "access_door.leaf", "access_door.leaf.frame", hinge_side,
        )
        hinge_x = door_left if hinge_side == "left" else door_right
        for index in range(_integer(parameters, "doorHingeCount")):
            hinge_z = door_bottom + (door_top - door_bottom) * (index + 1) / (_integer(parameters, "doorHingeCount") + 1)
            model.relationship(
                f"access_door.hinge.{index + 1:04d}", "hinge",
                [fixed_item, moving_item],
                properties={
                    "participantRoles": ["fixed", "moving"],
                    "origin": [hinge_x, 0.0, hinge_z],
                    "axis": [0.0, 0.0, 1.0],
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
