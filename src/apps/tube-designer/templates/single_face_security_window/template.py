from __future__ import annotations

from dataclasses import dataclass, replace
import hashlib
import importlib.util
import math
from pathlib import Path
import sys
from typing import Any

from icax_template_sdk import NeutralModel


TEMPLATE_ID = "single-face-security-window"
TEMPLATE_VERSION = "3.0.0"

PROFILE_CATALOG_SCRIPT = Path(__file__).resolve().parent.parent / "_shared" / "tube_profile_catalog.py"
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

SHARED_GEOMETRY_SCRIPT = PROFILE_CATALOG_SCRIPT.parent / "shared_tube_geometry.py"
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

REVIEW_RULES_SCRIPT = PROFILE_CATALOG_SCRIPT.parent / "security_window_rules.py"
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

PLATE_SCRIPT = PROFILE_CATALOG_SCRIPT.parent / "plate_geometry.py"
PLATE_MODULE = "icax_plate_geometry_" + hashlib.sha256(PLATE_SCRIPT.read_bytes()).hexdigest()[:16]
if PLATE_MODULE not in sys.modules:
    _plate_spec = importlib.util.spec_from_file_location(PLATE_MODULE, PLATE_SCRIPT)
    if _plate_spec is None or _plate_spec.loader is None:
        raise RuntimeError("无法加载板件制造规则")
    _plate_module = importlib.util.module_from_spec(_plate_spec)
    sys.modules[PLATE_MODULE] = _plate_module
    _plate_spec.loader.exec_module(_plate_module)
emit_rectangular_plate = sys.modules[PLATE_MODULE].emit_rectangular_plate
plate_properties = sys.modules[PLATE_MODULE].plate_properties


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
    return _profile_catalog.load_profile(parameters, prefix)


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


def _validate_bar_positions(
    positions: list[float], minimum: float, maximum: float, width: float, label: str,
) -> None:
    ordered = sorted(positions)
    if any(center - width / 2 < minimum - 1.0e-7
           or center + width / 2 > maximum + 1.0e-7 for center in ordered):
        raise ValueError(f"{label}实体超出可用范围，请调整数量或边距")
    if any(right - left <= width + 1.0e-7 for left, right in zip(ordered, ordered[1:])):
        raise ValueError(f"{label}间距不足，杆件会相碰或重叠")


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
    profile: Profile, *, clearance: float = 0.0, outer_only: bool = False,
) -> dict[str, Any]:
    horizontal = abs(end[0] - start[0]) >= abs(end[2] - start[2])
    if horizontal:
        x_axis, y_axis = [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]
    else:
        x_axis, y_axis = [1.0, 0.0, 0.0], [0.0, 1.0, 0.0]
    contours = profile.contours(clearance=clearance, swap_axes=horizontal)
    return {
        "placement": {"origin": list(start), "xAxis": x_axis, "yAxis": y_axis},
        "contours": contours[:1] if outer_only else contours,
    }


def _emit_tube_geometry(
    model: NeutralModel, part: Part, shared_geometry: SharedTubeGeometry,
) -> tuple[str, str]:
    vector = [part.end[index] - part.start[index] for index in range(3)]
    solid = shared_geometry.emit_tube(
        part.key,
        profile_arguments=_profile_arguments(part.start, part.end, part.profile),
        extrude_arguments={"vector": vector},
    )
    return solid, solid


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
            "contours": [{"kind": "polygon", "points": points}],
        },
    )
    return model.geometry(
        f"{key}.solid", "extrude", inputs=[profile],
        arguments={"vector": [0.0, half_depth * 2, 0.0]},
    )


def _unfold_frame_point(
    frame: ContinuousFrame, side: str, point: tuple[float, float, float],
    seam_shift: float = 0.0,
) -> tuple[float, float, float]:
    """Unfold rigid straight spans, starting at the bottom-edge midpoint.

    The strip travels bottom-right, right-up, top-left, left-down, then
    bottom-right to the closing seam. Positive strip Z always faces the frame
    opening. The corner allowance follows the same runs used for the V slots;
    it is added between spans, never scaled into a piercing's shape.
    """
    x, y, z = point
    half, wall = frame.profile.width / 2, frame.profile.wall
    horizontal, vertical, bend = frame.horizontal_run, frame.vertical_run, frame.bend_allowance
    if side == "bottom":
        return (x - (frame.left + frame.right) / 2 + seam_shift, y,
                z - (frame.bottom + half))
    if side == "right":
        return (horizontal / 2 + bend + z - (frame.bottom + wall), y,
                frame.right - half - x)
    if side == "top":
        return (horizontal / 2 + vertical + 2 * bend + frame.right - wall - x, y,
                frame.top - half - z)
    if side == "left":
        return (horizontal * 1.5 + vertical + 3 * bend + frame.top - wall - z, y,
                x - (frame.left + half))
    raise ValueError(f"未知连续框边：{side}")


def _continuous_frame_cutters(
    model: NeutralModel, frame: ContinuousFrame, parts: list[Part], clearance: float,
) -> list[str]:
    cutters: list[str] = []
    for side in ("bottom", "right", "top", "left"):
        side_vertical = side in {"left", "right"}
        strip_min = {"left": frame.left, "right": frame.right - frame.profile.width,
                     "bottom": frame.bottom, "top": frame.top - frame.profile.width}[side]
        strip_max = strip_min + frame.profile.width
        for part in parts:
            if part.vertical == side_vertical:
                continue
            if (part.group == "access_door.leaf") != (frame.group == "access_door.leaf"):
                continue
            if not part.key.startswith(("main_grid.", "access_door.leaf.horizontal.",
                                        "access_door.leaf.vertical.")):
                continue
            axis = 0 if side_vertical else 2
            low, high = sorted((part.start[axis], part.end[axis]))
            # A butt joint touching the outside/inside face needs no piercing.
            if min(high, strip_max) - max(low, strip_min) <= 1.0e-7:
                continue
            center = part.start[2 if side_vertical else 0]
            span_min, span_max = ((frame.bottom, frame.top) if side_vertical
                                  else (frame.left, frame.right))
            if not span_min <= center <= span_max:
                continue
            seam_shifts = [0.0]
            if side == "bottom":
                midpoint = (frame.left + frame.right) / 2
                seam_shifts = [frame.length if center < midpoint else 0.0]
                if abs(center - midpoint) < part.profile.width / 2 + clearance:
                    # The bottom seam splits this aperture between both ends
                    # of the stock. Each full cutter is clipped by the stock.
                    seam_shifts = [0.0, frame.length]
            for seam_index, seam_shift in enumerate(seam_shifts):
                prefix = f"{frame.key}.export.through.{side}.{part.key}.{seam_index}"
                arguments = _profile_arguments(
                    part.start, part.end, part.profile, clearance=clearance, outer_only=True,
                )
                placement = arguments["placement"]
                origin = _unfold_frame_point(frame, side, part.start, seam_shift)
                for axis_name in ("xAxis", "yAxis"):
                    point = tuple(part.start[i] + placement[axis_name][i] for i in range(3))
                    mapped = _unfold_frame_point(frame, side, point, seam_shift)
                    placement[axis_name] = [mapped[i] - origin[i] for i in range(3)]
                placement["origin"] = list(origin)
                end = _unfold_frame_point(frame, side, part.end, seam_shift)
                profile_key = model.geometry(f"{prefix}.profile", "profile2d", arguments=arguments)
                cutters.append(model.geometry(
                    f"{prefix}.solid", "extrude", inputs=[profile_key],
                    arguments={"vector": [end[i] - origin[i] for i in range(3)],
                               "extendStart": clearance + 1.0, "extendEnd": clearance + 1.0},
                ))
    return cutters


def _emit_continuous_frame_geometry(
    model: NeutralModel, frame: ContinuousFrame, shared_geometry: SharedTubeGeometry,
    purpose: str | None = None, inserted_parts: list[Part] | None = None,
) -> tuple[str, str, list[float]]:
    half = frame.profile.width / 2
    display = ""
    if purpose != "manufacturing":
        preview_parts = [
            Part(f"{frame.key}.display.left", "", (frame.left + half, 0, frame.bottom), (frame.left + half, 0, frame.top), frame.profile, frame.group),
            Part(f"{frame.key}.display.right", "", (frame.right - half, 0, frame.bottom), (frame.right - half, 0, frame.top), frame.profile, frame.group),
            Part(f"{frame.key}.display.bottom", "", (frame.left, 0, frame.bottom + half), (frame.right, 0, frame.bottom + half), frame.profile, frame.group),
            Part(f"{frame.key}.display.top", "", (frame.left, 0, frame.top - half), (frame.right, 0, frame.top - half), frame.profile, frame.group),
        ]
        display_shapes = [_emit_tube_geometry(model, part, shared_geometry)[1] for part in preview_parts]
        display = model.geometry(
            f"{frame.key}.display.compound", "compound", inputs=display_shapes,
        )
    centers: list[float] = []
    cursor = 0.0
    for segment in (frame.horizontal_run / 2, frame.vertical_run, frame.horizontal_run, frame.vertical_run):
        center = cursor + segment + frame.bend_allowance / 2
        centers.append(center)
        cursor += segment + frame.bend_allowance

    if purpose == "display":
        return display, display, centers
    base = Part(
        f"{frame.key}.export.base", "", (0.0, 0.0, 0.0), (frame.length, 0.0, 0.0),
        frame.profile, frame.group,
    )
    _, export_shape = _emit_tube_geometry(model, base, shared_geometry)

    half_tool_depth = (frame.profile.depth + max(frame.profile.wall * 4, 10.0)) / 2
    cutters = _continuous_frame_cutters(
        model, frame, inserted_parts or [], _number(frame.parameters, "assemblyClearance"),
    )
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
                    "contours": [{"kind": "circle", "radius": radius}],
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
    return display or export_shape, export_shape, centers


def _crossings(parts: list[Part]) -> dict[str, list[Part]]:
    result: dict[str, list[Part]] = {}
    verticals = [part for part in parts if part.vertical]
    horizontals = [part for part in parts if not part.vertical]
    for vertical in verticals:
        x = (vertical.start[0] + vertical.end[0]) / 2
        for horizontal in horizontals:
            frame_families = (
                "outer_frame.",
                "access_door.fixed_frame.",
                "access_door.leaf.frame.",
                "center_plate.frame.",
            )
            if any(vertical.key.startswith(prefix) and horizontal.key.startswith(prefix)
                   for prefix in frame_families):
                # 同一矩形框的转角由它自己的拼角/V槽工艺处理，不能再当成交叉穿管。
                continue
            z = (horizontal.start[2] + horizontal.end[2]) / 2
            x_margin = vertical.profile.width / 2
            z_margin = horizontal.profile.width / 2
            if (min(max(horizontal.start[0], horizontal.end[0]), x + x_margin)
                    - max(min(horizontal.start[0], horizontal.end[0]), x - x_margin) <= 1.0e-7):
                continue
            if (min(max(vertical.start[2], vertical.end[2]), z + z_margin)
                    - max(min(vertical.start[2], vertical.end[2]), z - z_margin) <= 1.0e-7):
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
            inserted.start, inserted.end, inserted.profile,
            clearance=clearance, outer_only=True,
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
    return profile.properties()


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


def _validate_insertion(receiver: Profile, inserted: Profile, depth: float, clearance: float, label: str) -> None:
    if depth == 0:
        return
    if depth <= receiver.wall + clearance or depth >= receiver.width - receiver.wall - clearance:
        raise ValueError(f"{label}入榫深度必须穿过内侧管壁并保留外侧管壁")
    _validate_through_fit(receiver, inserted, clearance, label)


def _validate_flat_weld(receiver: Profile, branch: Profile, label: str) -> None:
    # A square-cut branch can only meet the straight portion of a rectangular
    # receiver face. Curved/imported sections need an actual coped end first.
    if receiver.profile_id != "rect" or receiver.kind != "rect":
        raise ValueError(f"{label}平切焊接仅支持标准矩形外框，其他截面需要端部仿形切割")
    flat_depth = receiver.depth - 2 * receiver.radius
    if branch.depth > flat_depth + 1.0e-7:
        raise ValueError(
            f"{label}平切焊接端面超出外框平直面 {flat_depth:g} mm，"
            "会在圆角处留缝；请减小横杆深度、增大外框平直面或改用插接"
        )


def _main_horizontal_joints(
    part: Part, items: list[ModelItem], *, connection: str, reserve: float,
    outer_start: float, outer_end: float, has_side_frame: bool,
) -> dict[str, Any]:
    result: dict[str, Any] = {"mainHorizontalConnection": connection}
    for end_name, point in (("start", part.start), ("end", part.end)):
        mode, receiver, depth = "free", "", 0.0
        if has_side_frame and (abs(point[0] - outer_start) < 1.0e-7
                               or abs(point[0] - outer_end) < 1.0e-7):
            side = "left" if abs(point[0] - outer_start) < 1.0e-7 else "right"
            receiver = _frame_relationship_item(items, "main", "outer_frame", side)
            mode = "insert" if connection == "insert" else "butt_weld"
            depth = reserve if connection == "insert" else 0.0
        else:
            # Opening/plate cuts remain butt joints, including split pieces.
            # Resolve their actual surviving endpoint, not their original key.
            for candidate in items:
                if not candidate.key.startswith(("access_door.fixed_frame.", "center_plate.frame.")):
                    continue
                if isinstance(candidate, ContinuousFrame):
                    on_face = (abs(point[0] - candidate.left) < 1.0e-7
                               or abs(point[0] - candidate.right) < 1.0e-7)
                    within = candidate.bottom < point[2] < candidate.top
                else:
                    if not candidate.vertical:
                        continue
                    on_face = abs(abs(point[0] - candidate.start[0]) - candidate.profile.width / 2) < 1.0e-7
                    within = min(candidate.start[2], candidate.end[2]) < point[2] < max(candidate.start[2], candidate.end[2])
                if on_face and within:
                    mode, receiver = "butt_weld", candidate.key
                    break
        result[end_name] = mode
        result[f"{end_name}Receiver"] = receiver
        result[f"{end_name}InsertionDepth"] = depth
    return result


def _validate(parameters: dict[str, Any]) -> None:
    width, height = _number(parameters, "width"), _number(parameters, "height")
    frame = _profile(parameters, "frame")
    horizontal = _profile(parameters, "horizontal")
    vertical = _profile(parameters, "vertical")
    if width <= frame.width * 2 or height <= frame.width * 2:
        raise ValueError("产品宽高必须大于外框宽度的两倍")
    if not (
        frame.width > horizontal.width > vertical.width
        and frame.depth > horizontal.depth > vertical.depth
    ):
        raise ValueError("杆件宽深必须满足：外框 > 横杆 > 竖杆")
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
    connection = str(parameters.get("mainHorizontalConnection", "insert"))
    if connection not in {"insert", "weld"}:
        raise ValueError("mainHorizontalConnection 仅支持 insert 或 weld")
    reserve_keys = ["verticalBranchReserve"]
    if connection == "insert":
        reserve_keys.append("horizontalBranchReserve")
    for key in reserve_keys:
        insertion = _number(parameters, key)
        if not 0 <= insertion <= 20:
            raise ValueError(f"{key} 必须在0到20 mm之间")
    for key in ("horizontalCount", "middleVerticalCount"):
        if not 0 <= _integer(parameters, key) <= 100:
            raise ValueError(f"{key} 超出支持范围")
    if _integer(parameters, "horizontalCount"):
        for key in ("firstHorizontalTopOffset", "lastHorizontalBottomOffset"):
            if _number(parameters, key) < 0:
                raise ValueError(f"{key} 不能为负数")
    layout = str(parameters["frameLayout"])
    if layout not in {"left_right", "top_bottom", "four_sides"}:
        raise ValueError(f"不支持的外框布置：{layout}")
    horizontal_margin = frame.width if layout != "left_right" else 0.0
    _validate_bar_positions(
        _horizontal_positions(parameters, height), horizontal_margin,
        height - horizontal_margin, horizontal.width, "主横杆",
    )
    vertical_margin = frame.width if layout != "top_bottom" else 0.0
    vertical_count = _vertical_count(parameters, vertical_margin, width - vertical_margin, vertical.width)
    _validate_bar_positions(
        _even_positions(vertical_count, vertical_margin, width - vertical_margin),
        vertical_margin, width - vertical_margin, vertical.width, "主竖杆",
    )
    if layout != "top_bottom" and _integer(parameters, "horizontalCount"):
        if connection == "weld":
            _validate_flat_weld(frame, horizontal, "外框与主横杆")
        else:
            reserve = _number(parameters, "horizontalBranchReserve")
            if reserve == 0:
                raise ValueError("主横杆插接入榫深度必须大于0；贴合焊接请选择焊接连接")
            _validate_insertion(frame, horizontal, reserve, clearance, "外框与主横杆")
    if layout != "left_right" and vertical_count:
        reserve = _number(parameters, "verticalBranchReserve")
        _validate_insertion(frame, vertical, reserve, clearance, "外框与主竖杆")
    processes: list[tuple[CornerProcess, Profile]] = []
    if layout == "four_sides":
        processes.append((_process(parameters, "frameJoinType", "frameButtWrapMode"), frame))
    if parameters["accessDoorEnabled"]:
        for key in ("doorHorizontalCount", "doorVerticalCount"):
            if not 0 <= _integer(parameters, key) <= 100:
                raise ValueError(f"{key} 超出支持范围")
        if not 1 <= _integer(parameters, "doorHingeCount") <= 10:
            raise ValueError("doorHingeCount 超出支持范围")
        if parameters["doorHingeSide"] not in {"left", "right"}:
            raise ValueError("doorHingeSide 不支持")
        processes.extend((
            (_process(parameters, "doorFrameJoinType", "doorFrameButtWrapMode"), _profile(parameters, "doorFrame")),
            (_process(parameters, "doorLeafFrameJoinType", "doorLeafFrameButtWrapMode"), _profile(parameters, "doorLeafFrame")),
        ))
    groove_processes = [(process, profile) for process, profile in processes if process.join_type == "v_groove_90"]
    if any(process.groove_style != "sharp_v" for process, _ in groove_processes):
        if any(bool(parameters[key]) for key in ("vGrooveBottomCut", "vGrooveReliefHole", "vGrooveWallOvercut")):
            raise ValueError("底部切除、释放孔和壁厚过切仅适用于尖角 V 槽")
    if groove_processes:
        if not 0 <= _number(parameters, "vGrooveKFactor") <= 1:
            raise ValueError("展开 K 因子必须在 0 到 1 之间")
        distance = _number(parameters, "vGrooveBottomDistance")
        if distance < 0 or any(distance >= profile.width - profile.wall for _, profile in groove_processes):
            raise ValueError("V 槽底距离必须非负且小于各加工框的截面宽度减壁厚")
        if (any(process.groove_style == "rounded_v" for process, _ in groove_processes)
                or parameters["vGrooveBottomCut"] or parameters["vGrooveReliefHole"]):
            if _number(parameters, "vGrooveRadius") < 0:
                raise ValueError("V 槽半径不能为负数")
        if parameters["vGrooveReliefHole"] and _number(parameters, "vGrooveReliefDiameter") < 0:
            raise ValueError("V 槽释放孔直径不能为负数")
    if not parameters["accessDoorEnabled"]:
        return
    left, bottom = _number(parameters, "doorLeft"), _number(parameters, "doorBottom")
    door_width, door_height = _number(parameters, "doorWidth"), _number(parameters, "doorHeight")
    if left <= frame.width or left + door_width >= width - frame.width:
        raise ValueError("逃生窗必须保持在产品宽度范围内")
    if bottom <= frame.width or bottom + door_height >= height - frame.width:
        raise ValueError("逃生窗必须保持在产品高度范围内")
    door_frame = _profile(parameters, "doorFrame")
    leaf_frame = _profile(parameters, "doorLeafFrame")
    door_horizontal = _profile(parameters, "doorHorizontal")
    door_vertical = _profile(parameters, "doorVertical")
    _validate_through_fit(door_horizontal, door_vertical, clearance, "窗内横杆与窗内竖杆")
    gap = _number(parameters, "doorGap")
    required = 2 * (door_frame.width + leaf_frame.width + gap)
    if gap < 0 or door_width <= required or door_height <= required:
        raise ValueError("逃生窗尺寸不足以容纳窗框、窗扇框和间隙")
    if _integer(parameters, "doorVerticalCount"):
        _validate_insertion(leaf_frame, door_vertical, leaf_frame.width / 2, clearance, "窗扇框与窗内竖杆")
    inset = door_frame.width + gap + leaf_frame.width
    for count_key, size, bar, label in (
        ("doorHorizontalCount", door_height, door_horizontal, "窗内横杆"),
        ("doorVerticalCount", door_width, door_vertical, "窗内竖杆"),
    ):
        _validate_bar_positions(
            _even_positions(_integer(parameters, count_key), inset, size - inset),
            inset, size - inset, bar.width, label,
        )


def _generate_geometry(parameters: dict[str, Any], context: dict[str, Any]) -> dict[str, Any]:
    purpose = request_geometry_purpose(context)
    _validate(parameters)
    template = context["template"]
    model = NeutralModel(
        template_id=TEMPLATE_ID, template_version=TEMPLATE_VERSION,
        package_digest=str(template.get("packageDigest", "")), parameters=parameters,
    )
    shared_geometry = SharedTubeGeometry(model)
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

    horizontal_connection = str(parameters.get("mainHorizontalConnection", "insert"))
    horizontal_reserve = (_number(parameters, "horizontalBranchReserve")
                          if horizontal_connection == "insert" else 0.0)
    vertical_reserve = _number(parameters, "verticalBranchReserve")
    has_side_frame = layout != "top_bottom"
    has_top_bottom_frame = layout != "left_right"
    horizontal_start = frame.width - horizontal_reserve if has_side_frame else 0.0
    horizontal_end = width - frame.width + horizontal_reserve if has_side_frame else width
    vertical_start = frame.width - vertical_reserve if has_top_bottom_frame else 0.0
    vertical_end = height - frame.width + vertical_reserve if has_top_bottom_frame else height
    door_enabled = bool(parameters["accessDoorEnabled"])
    door_left = door_bottom = door_right = door_top = 0.0
    if door_enabled:
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
            items, counters, "access_door.fixed_frame", "固定窗框",
            door_left, door_bottom, door_right, door_top, door_frame,
            "access_door.fixed_frame",
            _process(parameters, "doorFrameJoinType", "doorFrameButtWrapMode"), parameters,
        )
        inset = door_frame.width + _number(parameters, "doorGap")
        leaf_left, leaf_right = door_left + inset, door_right - inset
        leaf_bottom, leaf_top = door_bottom + inset, door_top - inset
        _add_processed_rectangle(
            items, counters, "access_door.leaf.frame", "活动窗扇框",
            leaf_left, leaf_bottom, leaf_right, leaf_top, leaf_frame,
            "access_door.leaf",
            _process(parameters, "doorLeafFrameJoinType", "doorLeafFrameButtWrapMode"), parameters,
        )
        inner_left, inner_right = leaf_left + leaf_frame.width, leaf_right - leaf_frame.width
        inner_bottom, inner_top = leaf_bottom + leaf_frame.width, leaf_top - leaf_frame.width
        door_horizontal = _profile(parameters, "doorHorizontal")
        door_vertical = _profile(parameters, "doorVertical")
        for index, z in enumerate(_even_positions(_integer(parameters, "doorHorizontalCount"), inner_bottom, inner_top), start=1):
            # Equal-size horizontal stock meets the leaf frame at its inner
            # face as a butt joint; it cannot be pushed into a smaller cavity.
            _add_part(items, counters, "access_door.leaf.horizontal", f"窗内横杆 {index}", (inner_left, 0, z), (inner_right, 0, z), door_horizontal, "access_door.leaf",
                      category_name="窗内横杆")
        for index, x in enumerate(_even_positions(_integer(parameters, "doorVerticalCount"), inner_left, inner_right), start=1):
            _add_part(items, counters, "access_door.leaf.vertical", f"窗内竖杆 {index}", (x, 0, inner_bottom - leaf_frame.width / 2), (x, 0, inner_top + leaf_frame.width / 2), door_vertical, "access_door.leaf",
                      category_name="窗内竖杆")

    panel = None
    infill_mode = str(parameters.get("mainInfillMode", "tube_grid"))
    if infill_mode not in {"tube_grid", "center_plate"}:
        raise ValueError("不支持的中间填充形式")
    if infill_mode == "center_plate":
        region = ((inner_left, inner_bottom, inner_right, inner_top) if door_enabled
                  else (vertical_left, frame.width if has_top_bottom_frame else 0.0,
                        vertical_right, height - frame.width if has_top_bottom_frame else height))
        panel = _add_center_plate_frame(parameters, items, counters, region,
                                        door_horizontal if door_enabled else horizontal,
                                        "access_door.leaf" if door_enabled else "main")

    straight_parts = [item for item in items if isinstance(item, Part)]
    # Crossing analysis only feeds manufacturing cutters. Display requests do
    # not even compute this production-only intermediate structure.
    crossing_map = _crossings(straight_parts) if purpose != "display" else {}
    raw_geometry: dict[str, tuple[str, str]] = {
        part.key: _emit_tube_geometry(model, part, shared_geometry) for part in straight_parts
    }
    representations: dict[str, tuple[str, str]] = {}
    clearance = _number(parameters, "assemblyClearance")
    for part in straight_parts:
        display = raw_geometry[part.key][1]
        export = display
        if purpose == "display":
            representations[part.key] = (display, display)
            continue
        cutters = [
            _emit_crossing_cutter(model, part, inserted, index, clearance)
            for index, inserted in enumerate(crossing_map.get(part.key, []), start=1)
        ]
        if part.start_cut == "miter-45" or part.end_cut == "miter-45":
            cutters.extend(_emit_miter_cutters(model, part))
        if cutters:
            export = model.geometry(
                f"{part.key}.solid.final", "boolean", inputs=[display, *cutters],
                arguments={"operation": "subtract", "target": display, "tools": cutters},
            )
        representations[part.key] = (display, export)

    bend_locations: dict[str, list[float]] = {}
    for item in items:
        if isinstance(item, ContinuousFrame):
            display, export, locations = _emit_continuous_frame_geometry(
                model, item, shared_geometry, purpose, straight_parts,
            )
            representations[item.key] = (display, export)
            bend_locations[item.key] = locations

    item_keys: list[str] = []
    rows: list[dict[str, Any]] = []
    main_joints: dict[str, dict[str, Any]] = {}
    joint_labels = {"insert": "插接", "butt_weld": "贴合焊接", "free": "自由端"}
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
            if item.key.startswith("main_grid.horizontal."):
                main_joints[item.key] = _main_horizontal_joints(
                    item, items, connection=horizontal_connection, reserve=horizontal_reserve,
                    outer_start=horizontal_start, outer_end=horizontal_end,
                    has_side_frame=has_side_frame,
                )
                properties["tubeDesigner.jointProcess"] = main_joints[item.key]
            elif item.key.startswith("access_door.leaf.horizontal."):
                properties["tubeDesigner.jointProcess"] = {
                    "start": "butt_weld", "end": "butt_weld",
                    "receiver": "access_door.leaf.frame", "insertionDepth": 0.0,
                }
            elif item.key.startswith("access_door.leaf.vertical."):
                properties["tubeDesigner.jointProcess"] = {
                    "start": "insert", "end": "insert",
                    "receiver": "access_door.leaf.frame",
                    "insertionDepth": _profile(parameters, "doorLeafFrame").width / 2,
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
                "connection": (" / ".join(joint_labels[main_joints[item.key][end]] for end in ("start", "end"))
                               if item.key in main_joints else ""),
            },
        })

    for item_key, joints in main_joints.items():
        for end in ("start", "end"):
            if not joints[f"{end}Receiver"]:
                continue
            model.relationship(
                f"{item_key}.joint.{end}", "weld" if joints[end] == "butt_weld" else "assembly",
                [item_key, joints[f"{end}Receiver"]],
                properties={"connection": joints[end], "branchEnd": end,
                            "insertionDepth": joints[f"{end}InsertionDepth"],
                            "participantRoles": ["branch", "receiver"]},
            )

    if panel is not None:
        plate_key = "center_plate.panel.0001"
        geometry = emit_rectangular_plate(
            model, plate_key, width=panel["width"], height=panel["height"],
            thickness=panel["thickness"], center=panel["center"],
        )
        properties = plate_properties(panel["width"], panel["height"], panel["thickness"],
                                      str(parameters.get("materialGrade", "")),
                                      category_key="center_plate.panel", category_name="中间封板")
        number = f"{parameters['productCode']}-{len(items) + 1:03d}"
        properties.update(partNumber=number, group=panel["group"])
        item_key = model.item(plate_key, "中间封板", representations={"display": geometry, "export": geometry},
                              properties=properties)
        item_keys.append(item_key)
        rows.append({"key": f"row.{plate_key}", "parentKey": str(parameters["productCode"]),
                     "itemKey": item_key, "values": {"partNumber": number, "name": "中间封板",
                                                       "quantity": 1, "length": max(panel["width"], panel["height"])}})
        model.relationship("center_plate.panel.weld", "weld",
                           [plate_key, *[item.key for item in items if item.key.startswith("center_plate.frame.")]],
                           properties={"assemblyGroup": panel["group"], "connection": "edge_weld"})

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
            {"key": "connection", "displayName": "端部装配", "valueType": "string"},
        ],
        rows=rows,
    )
    return finish_geometry_request(model, context)


def _add_center_plate_frame(parameters: dict[str, Any], items: list[ModelItem],
                            counters: dict[str, int], region: tuple[float, float, float, float],
                            profile: Profile, group: str) -> dict[str, Any]:
    """Replace intersecting grid spans with a supported, edge-welded real plate."""
    width = _number(parameters, "centerPlateWidth")
    height = _number(parameters, "centerPlateHeight")
    thickness = _number(parameters, "centerPlateThickness")
    plate_properties(width, height, thickness)
    if thickness > profile.depth:
        raise ValueError("封板厚度不能大于封板框管的截面深度")
    center_x = (region[0] + region[2]) / 2 + _number(parameters, "centerPlateHorizontalOffset")
    center_z = (region[1] + region[3]) / 2 + _number(parameters, "centerPlateVerticalOffset")
    left, right = center_x - width / 2 - profile.width, center_x + width / 2 + profile.width
    bottom, top = center_z - height / 2 - profile.width, center_z + height / 2 + profile.width
    if (left < region[0] or right > region[2] or bottom < region[1] or top > region[3]):
        raise ValueError("中间封板及边框超出可用净空，请减小板宽高或调整偏移")
    prefix = "access_door.leaf." if group == "access_door.leaf" else "main_grid."
    rebuilt: list[ModelItem] = []
    for part in items:
        if (not isinstance(part, Part) or part.group != group or not part.key.startswith(prefix)
                or ".frame." in part.key):
            rebuilt.append(part)
            continue
        along = 2 if part.vertical else 0
        across = part.start[0] if part.vertical else part.start[2]
        low, high = (left, right) if part.vertical else (bottom, top)
        if across + part.profile.width / 2 <= low or across - part.profile.width / 2 >= high:
            rebuilt.append(part)
            continue
        cut_low, cut_high = (bottom, top) if part.vertical else (left, right)
        start_value, end_value = part.start[along], part.end[along]
        if end_value <= cut_low or start_value >= cut_high:
            rebuilt.append(part)
            continue
        for suffix, first, last in (("before", start_value, min(end_value, cut_low)),
                                    ("after", max(start_value, cut_high), end_value)):
            if last - first <= 1e-7:
                continue
            start, end = list(part.start), list(part.end)
            start[along], end[along] = first, last
            rebuilt.append(replace(part, key=f"{part.key}.panel_{suffix}",
                                   name=part.name + (" 封板前段" if suffix == "before" else " 封板后段"),
                                   start=tuple(start), end=tuple(end)))
    items[:] = rebuilt
    _add_processed_rectangle(items, counters, "center_plate.frame", "封板框",
                             left, bottom, right, top, profile, group,
                             CornerProcess("miter_45"), parameters)
    return {"width": width, "height": height, "thickness": thickness,
            "center": (center_x, 0.0, center_z), "group": group}


def generate(parameters: dict[str, Any], context: dict[str, Any]) -> dict[str, Any]:
    return generate_reviewed(parameters, context, layout="single-face",
                             load_profile=_profile, kernel=_generate_geometry)
