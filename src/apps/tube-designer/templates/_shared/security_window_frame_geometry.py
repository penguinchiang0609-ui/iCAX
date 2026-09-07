from __future__ import annotations

from dataclasses import dataclass
import hashlib
import importlib.util
import math
from pathlib import Path
import sys
from typing import Any

from icax_template_sdk import NeutralModel



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


def validate_frame_processes(parameters: dict[str, Any], processes: list[tuple[CornerProcess, Profile]]) -> None:
    grooves = [(process, profile) for process, profile in processes if process.join_type == "v_groove_90"]
    if not grooves:
        return
    if any(process.groove_style != "sharp_v" for process, _ in grooves):
        if any(bool(parameters[key]) for key in ("vGrooveBottomCut", "vGrooveReliefHole", "vGrooveWallOvercut")):
            raise ValueError("底部切除、释放孔和壁厚过切仅适用于尖角 V 槽")
    if not 0 <= _number(parameters, "vGrooveKFactor") <= 1:
        raise ValueError("展开 K 因子必须在 0 到 1 之间")
    distance = _number(parameters, "vGrooveBottomDistance")
    if distance < 0 or any(distance >= profile.width - profile.wall for _, profile in grooves):
        raise ValueError("V 槽底距离必须非负且小于各加工框的截面宽度减壁厚")
    if (any(process.groove_style == "rounded_v" for process, _ in grooves)
            or parameters["vGrooveBottomCut"] or parameters["vGrooveReliefHole"]):
        if _number(parameters, "vGrooveRadius") < 0:
            raise ValueError("V 槽半径不能为负数")
    if parameters["vGrooveReliefHole"] and _number(parameters, "vGrooveReliefDiameter") < 0:
        raise ValueError("V 槽释放孔直径不能为负数")


def emit_surface_frame(model: NeutralModel, shared: SharedTubeGeometry, parameters: dict[str, Any],
                       *, prefix: str, name: str, profile: Profile, group: str,
                       bounds: tuple[float, float, float, float], process: CornerProcess,
                       placement: dict[str, Any], inserted_parts: list[Part], purpose: str) -> tuple[list[dict[str, Any]], dict[str, str]]:
    """Use the same planar cuts/unfolding for a frame on any opening plane.

    Assembly coordinates are transformed onto the selected surface. A folded
    frame's manufacturing representation remains one straight unfolded stock.
    The returned mapping also redirects hinges and piercing receivers to that
    one stock item instead of leaving references to four nonexistent sides.
    """
    items: list[ModelItem] = []
    _add_processed_rectangle(items, {}, prefix, name, *bounds, profile, group, process, parameters)
    straight = [item for item in items if isinstance(item, Part)]
    crossing_map = _crossings([*straight, *inserted_parts]) if purpose != "display" else {}
    records = []
    sides = {}
    for item in items:
        props = {"quantity": 1, "group": group, "length": round(item.length, 3),
                 "tubeDesigner.profile": _profile_properties(profile)}
        if isinstance(item, ContinuousFrame):
            display, export, bends = _emit_continuous_frame_geometry(model, item, shared, purpose, inserted_parts)
            props.update({"manufacturing.categoryKey": prefix + ".continuous", "manufacturing.categoryName": name,
                          "tubeDesigner.cornerProcess": {"joinType": process.join_type, "grooveStyle": process.groove_style,
                                                         "bendAllowance": item.bend_allowance, "bendLocations": bends}})
            sides.update({side: item.key for side in ("left", "right", "bottom", "top")})
        else:
            display = _emit_tube_geometry(model, item, shared)[1]
            export = display
            if purpose != "display":
                cutters = [_emit_crossing_cutter(model, item, inserted, index, _number(parameters, "assemblyClearance"))
                           for index, inserted in enumerate(crossing_map.get(item.key, []), start=1)]
                if item.start_cut == "miter-45":
                    cutters.extend(_emit_miter_cutters(model, item))
                if cutters:
                    export = model.geometry(item.key + ".solid.final", "boolean", inputs=[display, *cutters],
                                            arguments={"operation": "subtract", "target": display, "tools": cutters})
            props.update({"manufacturing.categoryKey": item.category_key, "manufacturing.categoryName": item.category_name,
                          "tubeDesigner.endProcess": {"startCut": item.start_cut, "endCut": item.end_cut,
                                                      "lengthReference": "outside_long_points"}})
            if item.start_cut == "miter-45":
                props["tubeDesigner.displayApproximation"] = "uncut_miter_stock"
            sides[item.key[len(prefix) + 1:].split(".")[0]] = item.key
        props["tubeDesigner.connectionProcess"] = {"cornerJoin": process.join_type, "buttWrapMode": process.butt_wrap,
                                                   "grooveStyle": process.groove_style}
        # In manufacturing-only requests the local 'display' is also unfolded.
        if purpose != "manufacturing" or not isinstance(item, ContinuousFrame):
            display = model.geometry(item.key + ".surface.display", "transform", inputs=[display], arguments={"placement": placement})
        if purpose == "display":
            export = display
        elif not isinstance(item, ContinuousFrame):
            export = model.geometry(item.key + ".surface.export", "transform", inputs=[export], arguments={"placement": placement})
        records.append({"key": item.key, "name": item.name, "properties": props,
                        "representations": {"display": display, "export": export}})
    return records, sides


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
        return CornerProcess(join_type, style)
    if encoded not in {"miter_45", "butt_90"}:
        raise ValueError(f"{join_key} 的连接工艺不支持：{encoded}")
    butt_wrap = str(parameters[butt_key]) if encoded == "butt_90" else "side_wraps_horizontal"
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
            # Opening cuts remain butt joints, including split pieces.
            # Resolve their actual surviving endpoint, not their original key.
            for candidate in items:
                if not candidate.key.startswith(("access_door.fixed_frame.",)):
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
