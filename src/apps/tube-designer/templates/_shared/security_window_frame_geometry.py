from __future__ import annotations

from copy import deepcopy
from dataclasses import dataclass, replace
import hashlib
import importlib.util
import math
from pathlib import Path
import sys
from typing import Any

from icax_template_sdk import NeutralModel

_process_spec = importlib.util.spec_from_file_location("icax_window_frame_process_adapter",
    Path(__file__).with_name("security_window_process_adapter.py"))
_process_adapter = importlib.util.module_from_spec(_process_spec)
_process_spec.loader.exec_module(_process_adapter)
_machining = _process_adapter._load('security_window_machining_adapter')


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
    start_joint: str = ""
    end_joint: str = ""
    start_joint_mode: str = "butt"
    end_joint_mode: str = "butt"
    start_insertion: float | str = 0.
    end_insertion: float | str = 0.

    @property
    def length(self) -> float:
        return math.dist(self.start, self.end)

    @property
    def vertical(self) -> bool:
        return abs(self.end[2] - self.start[2]) > abs(self.end[0] - self.start[0])


@dataclass
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
    corner_reserve: float | None = None
    resolved_allowance: float | None = None

    @property
    def retained_datum(self) -> float:
        return self.profile.wall if self.corner_reserve is None else self.profile.width/2-self.corner_reserve

    @property
    def bend_allowance(self) -> float:
        # The sizing pass resolves shared visibility conditions first. An
        # inactive compensation draft must not allocate material or reject it.
        return 0. if self.resolved_allowance is None else self.resolved_allowance

    @property
    def horizontal_run(self) -> float:
        return self.right - self.left - self.retained_datum * 2

    @property
    def vertical_run(self) -> float:
        return self.top - self.bottom - self.retained_datum * 2

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
            props.update(deepcopy(getattr(model, "_assembly_process_records", {}).get(item.key, {})))
            props["tubeDesigner.sourceSpans"] = frame_source_spans(item, placement)
            props.update({"manufacturing.categoryKey": prefix + ".continuous", "manufacturing.categoryName": name,
                          "tubeDesigner.cornerProcess": {"joinType": process.join_type, "grooveStyle": process.groove_style,
                                                         "bendAllowance": item.bend_allowance, "bendLocations": bends}})
            sides.update({side: item.key for side in ("left", "right", "bottom", "top")})
        else:
            arguments = _profile_arguments(item.start, item.end, item.profile)
            def world(point):
                return [placement['origin'][i] + sum(point[j] * placement[name][i]
                        for j, name in enumerate(('xAxis', 'yAxis', 'zAxis'))) for i in range(3)]
            local_placement = arguments['placement']
            arguments['placement'] = {"origin":world(item.start), **{
                name:[sum(local_placement[name][j] * placement[axis][i]
                          for j, axis in enumerate(('xAxis','yAxis','zAxis'))) for i in range(3)]
                for name in ('xAxis','yAxis')}}
            props["tubeDesigner.designSegment"] = {"start":world(item.start), "end":world(item.end),
                "profileCoordinateMap": ([[0.,1.],[1.,0.]] if abs(item.end[0]-item.start[0]) >= abs(item.end[2]-item.start[2])
                                         else [[1.,0.],[0.,1.]]), **arguments}
            display = _emit_tube_geometry(model, item, shared)[1]
            export = display
            if purpose != "display":
                export = _machining.planar_part(model, display, item, crossing_map.get(item.key, []),
                    _number(parameters, 'assemblyClearance'), straight,
                    lambda p: _profile_arguments(p.start,p.end,p.profile))
                props.update(deepcopy(getattr(model, '_assembly_process_records', {}).get(item.key, {})))
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
            _machining.transform_source(props,placement)
            getattr(model,'_assembly_process_records',{})[item.key]=deepcopy({
                key:value for key,value in props.items() if key.startswith('tubeDesigner.assemblyProcess')})
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
    if process.join_type=='butt_90':
        left_part,right_part,bottom_part,top_part=items[-4:]
        args=lambda p:_profile_arguments(p.start,p.end,p.profile)
        connections=((bottom_part,left_part,right_part),(top_part,left_part,right_part)) if process.butt_wrap=='side_wraps_horizontal' else (
            (left_part,bottom_part,top_part),(right_part,bottom_part,top_part))
        for part,start_mate,end_mate in connections:
            allocated=replace(part,
                start=_machining.allocate_joint(part,start_mate,'start','butt',0.,0.,args),
                end=_machining.allocate_joint(part,end_mate,'end','butt',0.,0.,args),
                start_joint=start_mate.key,end_joint=end_mate.key)
            items[items.index(part)]=allocated
    elif process.join_type=='miter_45' and not parameters.get('_assemblyDesignOnly',False):
        frame_parts=items[-4:]
        for part in list(frame_parts):
            ends=_machining.planar_miter_ends(part,frame_parts)
            direction=_machining._load('assembly_tube_machining').unit(_machining.sub(part.end,part.start))
            nominal=replace(part,
                start=tuple(part.start[i]+half*direction[i] for i in range(3)),
                end=tuple(part.end[i]-half*direction[i] for i in range(3)))
            allocated=replace(part,
                start=_machining.allocate_miter(nominal,'start',ends[0][2],lambda p:_profile_arguments(p.start,p.end,p.profile)),
                end=_machining.allocate_miter(nominal,'end',ends[1][2],lambda p:_profile_arguments(p.start,p.end,p.profile)))
            items[items.index(part)]=allocated


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
    half, wall = frame.profile.width / 2, frame.retained_datum
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


def frame_source_spans(frame: ContinuousFrame, placement: dict[str, Any] | None = None) -> list[dict[str, Any]]:
    """Map four logical frame sides to the actual five unfolded straight spans."""
    placement = placement or {"origin":[0,0,0],"xAxis":[1,0,0],"yAxis":[0,1,0],"zAxis":[0,0,1]}
    half = frame.profile.width / 2
    reserve = half - frame.retained_datum
    mid = (frame.left + frame.right) / 2
    corners = {"bl":(frame.left+half,0,frame.bottom+half),
               "br":(frame.right-half,0,frame.bottom+half),
               "tr":(frame.right-half,0,frame.top-half),
               "tl":(frame.left+half,0,frame.top-half),
               "seam":(mid,0,frame.bottom+half)}
    segments = [("bottom","seam","br",0,reserve,0),
                ("right","br","tr",reserve,reserve,0),
                ("top","tr","tl",reserve,reserve,0),
                ("left","tl","bl",reserve,reserve,0),
                ("bottom","bl","seam",reserve,0,frame.length)]
    axes = ('xAxis','yAxis','zAxis')
    def world(point):
        return [placement['origin'][i]+sum(point[j]*placement[name][i]
                for j,name in enumerate(axes)) for i in range(3)]
    def local(point):
        return tuple(sum((point[i]-placement['origin'][i])*placement[name][i]
                         for i in range(3)) for name in axes)
    prefix = frame.key.rsplit('.continuous.',1)[0]
    result, cursor = [], 0.0
    for index,(side,start,end,start_reserve,end_reserve,seam_shift) in enumerate(segments):
        a,b = corners[start],corners[end]
        def mapped(point):
            return _unfold_frame_point(frame,side,local(point),seam_shift)
        origin = mapped((0,0,0))
        result.append({"itemKey":prefix+'.'+side+'.0001',"start":world(a),"end":world(b),
            "stockStart":cursor,"startReserve":start_reserve,"endReserve":end_reserve,
            "placement":{"origin":list(origin), **{name:[mapped(tuple(1 if k==j else 0 for k in range(3)))[i]-origin[i]
                for i in range(3)] for j,name in enumerate(axes)}}})
        cursor += math.dist(a,b)+start_reserve+end_reserve
        if index < 4:
            cursor += frame.bend_allowance
    if abs(cursor-frame.length)>1e-6:
        raise ValueError('连续框成品管段映射与实际下料长度不一致')
    return result


def _apply_continuous_apertures(
    model, frame, raw, parts, clearance,
):
    stock = _machining.unfolded_stock(model,frame)
    current = raw
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
                instance_id=f"{frame.key}.aperture.{side}.{part.key}.{seam_index}"
                branch=_machining.mapped_branch(model,part,
                    lambda p:_profile_arguments(p.start,p.end,p.profile),
                    lambda point:_unfold_frame_point(frame,side,tuple(point),seam_shift),
                    key=part.key+f".unfolded.{side}.{seam_index}")
                current=_machining.aperture(model,current,stock,branch,instance_id,clearance,
                                            clip_interval=[0.,frame.length])
    return current


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
    provisional_bends = [{"sequence":index, "station":center, "angle":90., "rotation":0.}
                         for index, center in enumerate(centers,start=1)]
    _, sizing = _process_adapter.frame_plan(model,frame,provisional_bends)
    frame.resolved_allowance = _process_adapter.bend_allowance(sizing)
    reserves = [_process_adapter.corner_reserve(fold) for fold in sizing['forming']]
    if max(reserves)-min(reserves)>1e-6:
        raise ValueError('平面闭合框的四个槽根必须提供一致的材料基准')
    frame.corner_reserve = reserves[0]
    centers, cursor = [], 0.
    for segment in (frame.horizontal_run/2,frame.vertical_run,frame.horizontal_run,frame.vertical_run):
        centers.append(cursor+segment+frame.bend_allowance/2)
        cursor += segment+frame.bend_allowance
    base = Part(
        f"{frame.key}.export.base", "", (0.0, 0.0, 0.0), (frame.length, 0.0, 0.0),
        frame.profile, frame.group,
    )
    _, export_shape = _emit_tube_geometry(model, base, shared_geometry)

    export_shape = _apply_continuous_apertures(
        model,frame,export_shape,inserted_parts or [],_number(frame.parameters,"assemblyClearance"))
    secondary_cutters = []
    bends = [{"sequence":index, "station":center, "angle":90., "rotation":0.}
             for index, center in enumerate(centers, start=1)]
    source, plan = _process_adapter.frame_plan(model, frame, bends)
    cutters = [*_process_adapter.emit_cutters(model, frame, plan), *secondary_cutters]
    records = getattr(model, "_assembly_process_records", {})
    properties = deepcopy(getattr(model,"_assembly_process_records",{}).get(frame.key,{}))
    _process_adapter.attach(properties, source, plan, secondary_cutters)
    properties['length'] = round(frame.length,3)
    properties['tubeDesigner.assemblyProcessTargetSpanCheck'] = _process_adapter.target_span_check(
        frame_source_spans(frame),plan,frame.bend_allowance)
    records[frame.key] = properties
    model._assembly_process_records = records
    export_shape = _process_adapter._load('assembly_geometry_process_runtime').apply_frozen_cutters(
        model,export_shape,cutters,frame.key+'.resolved-folds',frame.key,plan)
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
    _machining.check_profiles(receiver,inserted,0.,clearance,label,through=True)


def _validate_insertion(receiver: Profile, inserted: Profile, depth: float, clearance: float, label: str,
                        *, receiver_span: float | None = None) -> None:
    _machining.check_profiles(receiver,inserted,depth,clearance,label,receiver_span)


def _main_horizontal_joints(
    part: Part, items: list[ModelItem], *, reserve: float,
    outer_start: float, outer_end: float, has_side_frame: bool,
) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for end_name, point in (("start", part.start), ("end", part.end)):
        assigned=getattr(part,end_name+'_joint','')
        if assigned:
            assigned_mode=getattr(part,end_name+'_joint_mode','butt')
            result[end_name]='butt_weld' if assigned_mode=='butt' else assigned_mode
            result[end_name+'Receiver']=assigned
            result[end_name+'InsertionDepth']=getattr(part,end_name+'_insertion',0.)
            continue
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
