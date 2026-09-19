from __future__ import annotations

from copy import deepcopy
from dataclasses import dataclass
import hashlib
import importlib.util
import math
from pathlib import Path
import sys
from typing import Any

from icax_template_sdk import NeutralModel


def _path(points):
    return {"kind": "path", "closed": True, "segments": [
        {"kind": "line", "start": list(points[i]), "end": list(points[(i + 1) % len(points)])}
        for i in range(len(points))
    ]}



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
    tool_role: str = ""
    user_mould_root: str = ""

    @property
    def bend_allowance(self) -> float:
        bindings = self.parameters.get("tubeDesignerToolBindings", {})
        binding = bindings.get(self.tool_role) if isinstance(bindings, dict) and self.tool_role else None
        values = binding.get("parameters", {}) if isinstance(binding, dict) else {}
        if not isinstance(values, dict) or not values.get("bendCompensation", False):
            return 0.0
        angle = values.get("angle", 90.0)
        if isinstance(angle, bool) or not isinstance(angle, (int, float)) or not math.isfinite(angle):
            raise ValueError("槽口模具折弯角必须是有限数值")
        if values.get("useDefaultKFactor", True):
            factor = 0.62
        else:
            factor = values.get("kFactor", 0.62)
            if isinstance(factor, bool) or not isinstance(factor, (int, float)) or not math.isfinite(factor):
                raise ValueError("槽口模具 K 因子必须是有限数值")
            if not 0 <= factor <= 1:
                raise ValueError("槽口模具 K 因子须介于 0 和 1 之间")
        return math.radians(float(angle)) * float(factor) * self.profile.wall

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
    # Every continuous-frame variant now resolves to an actual mould package.
    # Its own package performs the section applicability and cutter validation;
    # do not keep a second, product-local V-slot interpretation here.
    return


def _validate_bending_section(profile: Profile) -> None:
    # Inspect actual support edges, not a profile ID or its advertised kind.
    loops = profile.contours(swap_axes=True)
    if len(loops) != 2:
        raise ValueError("折弯框需要一个外轮廓和一个内轮廓")
    levels = []
    for loop in loops:
        if loop['kind'] == 'polygon':
            points = loop['points']
            edges = [{'kind':'line','start':a,'end':b} for a,b in zip(points,points[1:]+points[:1])]
        elif loop['kind'] == 'path':
            edges = loop['segments']
        else:
            raise ValueError("折弯框截面需要上下平直支承壁")
        supports = [e['start'][1] for e in edges if e['kind']=='line'
                    and abs(e['start'][1]-e['end'][1])<1e-6
                    and min(e['start'][0],e['end'][0]) < -1e-6
                    and max(e['start'][0],e['end'][0]) > 1e-6]
        if len(supports)!=2:
            raise ValueError("折弯框截面需要上下平直支承壁")
        levels.append(sorted(supports))
    outer, inner = levels
    if (abs(outer[0]+profile.width/2)>1e-5 or abs(outer[1]-profile.width/2)>1e-5
            or abs(inner[0]-outer[0]-profile.wall)>1e-5
            or abs(outer[1]-inner[1]-profile.wall)>1e-5):
        raise ValueError("折弯框的截面基准、上下实际壁厚必须与展开参数一致")


def emit_surface_frame(model: NeutralModel, shared: SharedTubeGeometry, parameters: dict[str, Any],
                       *, prefix: str, name: str, profile: Profile, group: str,
                       bounds: tuple[float, float, float, float], process: CornerProcess,
                       placement: dict[str, Any], inserted_parts: list[Part], purpose: str,
                       tool_role: str = "", user_mould_root: str = "") -> tuple[list[dict[str, Any]], dict[str, str]]:
    """Use the same planar cuts/unfolding for a frame on any opening plane.

    Assembly coordinates are transformed onto the selected surface. A folded
    frame's manufacturing representation remains one straight unfolded stock.
    The returned mapping also redirects hinges and piercing receivers to that
    one stock item instead of leaving references to four nonexistent sides.
    """
    items: list[ModelItem] = []
    _add_processed_rectangle(items, {}, prefix, name, *bounds, profile, group, process, parameters,
                             tool_role, user_mould_root)
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
        if style != "tool_library":
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
    group: str, process: CornerProcess, parameters: dict[str, Any], tool_role: str = "",
    user_mould_root: str = "",
) -> None:
    if right - left <= profile.width * 2 or top - bottom <= profile.width * 2:
        raise ValueError(f"{name}尺寸不足以形成封闭框")
    if process.join_type == "v_groove_90":
        role = f"{prefix}.continuous"
        items.append(ContinuousFrame(
            _next_key(counters, role), name, left, bottom, right, top,
            profile, group, process, parameters, tool_role, user_mould_root,
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
            "contours": [points if isinstance(points,dict) else _path(points)],
        },
    )
    return model.geometry(
        f"{key}.solid", "extrude", inputs=[profile],
        arguments={"vector": [0.0, half_depth * 2, 0.0]},
    )


def _circle_from_points(first: list[float], middle: list[float], last: list[float]) -> tuple[list[float], float]:
    """Return the exact circle through a path arc's three protocol points."""
    ax, ay = first
    bx, by = middle
    cx, cy = last
    denominator = 2.0 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by))
    if abs(denominator) <= 1.0e-10:
        raise ValueError("模具目标管型含退化圆弧")
    a2, b2, c2 = ax * ax + ay * ay, bx * bx + by * by, cx * cx + cy * cy
    center = [
        (a2 * (by - cy) + b2 * (cy - ay) + c2 * (ay - by)) / denominator,
        (a2 * (cx - bx) + b2 * (ax - cx) + c2 * (bx - ax)) / denominator,
    ]
    return center, math.dist(center, first)


def _section_edges(contour: dict[str, Any]) -> list[dict[str, Any]]:
    """Translate the profile protocol into the exact mould-section protocol.

    This is deliberately an analytic translation: lines and arcs remain lines
    and arcs.  It never samples a tube profile to make a mould appear usable.
    """
    kind = str(contour.get("kind", ""))
    if kind == "circle":
        radius = _number(contour, "radius")
        if radius <= 0:
            raise ValueError("模具目标管型圆截面半径必须大于 0")
        return [{"kind": "circleArc", "start": [radius, 0.0], "end": [radius, 0.0],
                 "center": [0.0, 0.0], "xAxis": [1.0, 0.0], "yAxis": [0.0, 1.0],
                 "radius": radius, "first": 0.0, "last": math.tau}]
    if kind == "polygon":
        points = contour.get("points")
        if not isinstance(points, list) or len(points) < 3:
            raise ValueError("模具目标管型多边形轮廓无效")
        return [{"kind": "line", "start": deepcopy(first), "end": deepcopy(last)}
                for first, last in zip(points, points[1:] + points[:1])]
    if kind != "path" or contour.get("closed") is not True:
        raise ValueError("所选槽口模具需要闭合的直线或圆弧管型截面")
    edges: list[dict[str, Any]] = []
    for segment in contour.get("segments", []):
        if segment.get("kind") == "line":
            edges.append({"kind": "line", "start": deepcopy(segment["start"]), "end": deepcopy(segment["end"])})
            continue
        if segment.get("kind") != "arc":
            raise ValueError("所选槽口模具不支持该管型的曲线类型")
        first, middle, last = (deepcopy(segment[name]) for name in ("start", "middle", "end"))
        center, radius = _circle_from_points(first, middle, last)
        edges.append({"kind": "circleArc", "start": first, "end": last, "center": center,
                      "xAxis": [1.0, 0.0], "yAxis": [0.0, 1.0], "radius": radius,
                      "first": math.atan2(first[1] - center[1], first[0] - center[0]),
                      "last": math.atan2(last[1] - center[1], last[0] - center[0])})
    if not edges:
        raise ValueError("模具目标管型轮廓无有效边")
    return edges


def _mould_target_section(profile: Profile, length: float) -> tuple[dict[str, Any], dict[str, list[float]]]:
    contours = profile.contours(swap_axes=True)
    loops = [{"inner": index > 0, "closed": True, "edges": _section_edges(contour)}
             for index, contour in enumerate(contours)]
    all_points = [point for loop in loops for edge in loop["edges"]
                  for point in (edge["start"], edge["end"])]
    if not all_points:
        raise ValueError("模具目标管型截面为空")
    bounds = {"min": [0.0, *(min(point[index] for point in all_points) for index in (0, 1))],
              "max": [float(length), *(max(point[index] for point in all_points) for index in (0, 1))]}
    return {"schema": "icax.mold-section", "schemaVersion": 1, "status": "available",
            "tolerance": 0.001, "coordinateSpace": "section-centered-yz", "contours": loops}, bounds


def _punch_runtime():
    path = Path(__file__).with_name("punch_tool_runtime.py")
    module_key = "icax_product_mould_runtime_" + hashlib.sha256(path.read_bytes()).hexdigest()[:16]
    if module_key not in sys.modules:
        spec = importlib.util.spec_from_file_location(module_key, path)
        if spec is None or spec.loader is None:
            raise RuntimeError("无法加载模具运行时")
        module = importlib.util.module_from_spec(spec)
        sys.modules[module_key] = module
        spec.loader.exec_module(module)
    return sys.modules[module_key]


def _frame_mould_binding(frame: ContinuousFrame) -> tuple[dict[str, Any], dict[str, Any]]:
    bindings = frame.parameters.get("tubeDesignerToolBindings", {})
    binding = bindings.get(frame.tool_role) if isinstance(bindings, dict) and frame.tool_role else None
    if isinstance(binding, dict):
        reference = binding.get("ref")
        values = binding.get("parameters", {})
        if not isinstance(reference, dict) or not isinstance(values, dict):
            raise ValueError("连续框槽口模具引用无效")
        return deepcopy(reference), deepcopy(values)
    selection = str(frame.parameters.get(frame.tool_role + "Tool", "system:v-notch-sharp"))
    scope, separator, tool_id = selection.partition(":")
    if separator != ":" or scope != "system" or not tool_id:
        raise ValueError("连续框槽口模具尚未从模具库选择")
    return {"scope": "system", "id": tool_id}, {}


def _rewrite_tool_value(value: Any, mapping: dict[str, str]) -> Any:
    if isinstance(value, str):
        return mapping.get(value, value)
    if isinstance(value, list):
        return [_rewrite_tool_value(item, mapping) for item in value]
    if isinstance(value, dict):
        return {key: _rewrite_tool_value(item, mapping) for key, item in value.items()}
    return deepcopy(value)


def _emit_library_groove_cutter(model: NeutralModel, frame: ContinuousFrame, center: float, index: int) -> tuple[str, tuple[float, float]]:
    reference, values = _frame_mould_binding(frame)
    section, bounds = _mould_target_section(frame.profile, frame.length)
    snapshot = _punch_runtime()._evaluate(reference, values, {
        "target": "part", "bounds": bounds, "targetSection": section,
        "lengthUnit": "mm", "feature": f"product:{frame.tool_role or 'cornerGroove'}",
    }, user_root=frame.user_mould_root or None)
    geometry = snapshot["geometry"]
    source_nodes = geometry.get("model", {}).get("geometry", [])
    output = str(geometry.get("outputKey", ""))
    if not source_nodes or not output:
        raise ValueError("所选槽口模具没有可用实体刀具")
    prefix = f"{frame.key}.export.groove.{index:04d}.mould"
    mapping = {str(node["key"]): f"{prefix}.{node['key']}" for node in source_nodes}
    if output not in mapping:
        raise ValueError("所选槽口模具输出节点无效")
    for node in source_nodes:
        model.geometry(mapping[str(node["key"])], str(node["operator"]),
                       inputs=[mapping.get(str(key), str(key)) for key in node.get("inputs", [])],
                       arguments=_rewrite_tool_value(node.get("arguments", {}), mapping))
    placed = model.geometry(f"{prefix}.placed", "transform", inputs=[mapping[output]], arguments={"placement": {
        "origin": [center, 0.0, 0.0], "xAxis": [1.0, 0.0, 0.0],
        "yAxis": [0.0, 1.0, 0.0], "zAxis": [0.0, 0.0, 1.0],
    }})
    x_values: list[float] = []
    for node in source_nodes:
        if node.get("operator") != "profile2d":
            continue
        placement = node.get("arguments", {}).get("placement", {})
        origin = placement.get("origin", [0.0, 0.0, 0.0])
        for contour in node.get("arguments", {}).get("contours", []):
            for segment in contour.get("segments", []) if isinstance(contour, dict) else []:
                for point in (segment.get("start"), segment.get("middle"), segment.get("end")):
                    if isinstance(point, list) and len(point) == 2:
                        x_values.append(float(origin[0]) + float(point[0]))
    extent = (min(x_values), max(x_values)) if x_values else (-frame.profile.width, frame.profile.width)
    return placed, extent


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

    cutters = _continuous_frame_cutters(
        model, frame, inserted_parts or [], _number(frame.parameters, "assemblyClearance"),
    )
    for index, center in enumerate(centers, start=1):
        cutter, _ = _emit_library_groove_cutter(model, frame, center, index)
        cutters.append(cutter)
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


def _main_horizontal_joints(
    part: Part, items: list[ModelItem], *, reserve: float,
    outer_start: float, outer_end: float, has_side_frame: bool,
) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for end_name, point in (("start", part.start), ("end", part.end)):
        mode, receiver, depth = "free", "", 0.0
        if has_side_frame and (abs(point[0] - outer_start) < 1.0e-7
                               or abs(point[0] - outer_end) < 1.0e-7):
            side = "left" if abs(point[0] - outer_start) < 1.0e-7 else "right"
            receiver = _frame_relationship_item(items, "main", "outer_frame", side)
            mode = "insert"
            depth = reserve
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
