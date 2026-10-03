from __future__ import annotations

from dataclasses import dataclass, replace
from copy import deepcopy
import hashlib
import importlib.util
import math
from pathlib import Path
import sys
from typing import Any

from icax_template_sdk import NeutralModel, to_resource_model, display_context, to_display_model
from icax_template_sdk import manufacturing_context, manufacturing_declaration, to_manufacturing_model


TEMPLATE_ID = "single-face-security-window"
TEMPLATE_VERSION = "3.9.2"
DOOR_HINGE_SIDE = "left"
DOOR_HINGE_COUNT = 2

PROFILE_CATALOG_SCRIPT = Path(__file__).resolve().parent.parent.parent / "_shared" / "tube_profile_catalog.py"
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
_center_spacing_positions = sys.modules[REVIEW_RULES_MODULE].center_spacing_positions

FRAME_SCRIPT = PROFILE_CATALOG_SCRIPT.parent / "security_window_frame_geometry.py"
FRAME_MODULE = "icax_security_window_frames_" + hashlib.sha256(FRAME_SCRIPT.read_bytes()).hexdigest()[:16]
if FRAME_MODULE not in sys.modules:
    _frame_spec = importlib.util.spec_from_file_location(FRAME_MODULE, FRAME_SCRIPT)
    if _frame_spec is None or _frame_spec.loader is None:
        raise RuntimeError("无法加载共用窗框加工规则")
    _frame_module = importlib.util.module_from_spec(_frame_spec)
    sys.modules[FRAME_MODULE] = _frame_module
    _frame_spec.loader.exec_module(_frame_module)
_frame_geometry = sys.modules[FRAME_MODULE]
CornerProcess = _frame_geometry.CornerProcess
Part = _frame_geometry.Part
ContinuousFrame = _frame_geometry.ContinuousFrame
_number = _frame_geometry._number
_integer = _frame_geometry._integer
_profile = _frame_geometry._profile
_process = _frame_geometry._process
_even_positions = _frame_geometry._even_positions
_validate_bar_positions = _frame_geometry._validate_bar_positions
_next_key = _frame_geometry._next_key
_add_part = _frame_geometry._add_part
_add_processed_rectangle = _frame_geometry._add_processed_rectangle
_profile_arguments = _frame_geometry._profile_arguments
_emit_tube_geometry = _frame_geometry._emit_tube_geometry
_unfold_frame_point = _frame_geometry._unfold_frame_point
_emit_continuous_frame_geometry = _frame_geometry._emit_continuous_frame_geometry
_crossings = _frame_geometry._crossings
_profile_properties = _frame_geometry._profile_properties
_frame_relationship_item = _frame_geometry._frame_relationship_item
_validate_through_fit = _frame_geometry._validate_through_fit
_validate_insertion = _frame_geometry._validate_insertion
_main_horizontal_joints = _frame_geometry._main_horizontal_joints
ModelItem = Part | ContinuousFrame

STOCK_SCRIPT = PROFILE_CATALOG_SCRIPT.parent / "assembly_stock_allowance.py"
STOCK_MODULE = "icax_security_window_stock_" + hashlib.sha256(STOCK_SCRIPT.read_bytes()).hexdigest()[:16]
if STOCK_MODULE not in sys.modules:
    _stock_spec = importlib.util.spec_from_file_location(STOCK_MODULE, STOCK_SCRIPT)
    _stock_module = importlib.util.module_from_spec(_stock_spec)
    sys.modules[STOCK_MODULE] = _stock_module
    _stock_spec.loader.exec_module(_stock_module)
_stock_rules = sys.modules[STOCK_MODULE]


def _outer_process(parameters: dict[str, Any]) -> CornerProcess:
    mode = parameters.get("frameManufacturingMode", "segment_weld")
    if mode not in {"segment_weld", "plane_v_notch", "spatial_v_notch"}:
        raise ValueError("外框制造方式不受支持")
    if mode != "segment_weld":
        return CornerProcess("v_groove_90", "tool_library")
    process = _process(parameters, "frameJoinType", "frameButtWrapMode")
    # A retained draft V selection cannot override the manufacturing mode.
    return CornerProcess("miter_45") if process.join_type == "v_groove_90" else process


def _validate(parameters: dict[str, Any], manufacturing=True) -> None:
    width, height = _number(parameters, "width"), _number(parameters, "height")
    pattern = str(parameters.get("infillPattern", "grid"))
    if pattern not in {"grid", "horizontal", "vertical"}:
        raise ValueError("不支持的填充杆件方向")
    frame = _profile(parameters, "frame")
    horizontal = _profile(parameters, "horizontal") if pattern != "vertical" else None
    vertical = _profile(parameters, "vertical") if pattern != "horizontal" else None
    if width <= frame.width * 2 or height <= frame.width * 2:
        raise ValueError("产品宽高必须大于外框宽度的两倍")
    if (any(member.width >= frame.width or member.depth >= frame.depth
            for member in (horizontal, vertical) if member is not None)
            or horizontal is not None and vertical is not None and not (
                horizontal.width > vertical.width and horizontal.depth > vertical.depth)):
        raise ValueError("杆件宽深必须满足：外框 > 横杆 > 竖杆")
    clearance = _number(parameters, "assemblyClearance")
    if clearance < 0:
        raise ValueError("装配间隙不能为负数")
    if manufacturing and horizontal is not None and vertical is not None:
        _validate_through_fit(horizontal, vertical, clearance, "主横杆与主竖杆")
    for key in ("horizontalBranchReserve", "verticalBranchReserve"):
        if (key.startswith("horizontal") and horizontal is None
                or key.startswith("vertical") and vertical is None):
            continue
        insertion = _number(parameters, key)
        if not 0 <= insertion <= 20:
            raise ValueError(f"{key} 必须在0到20 mm之间")
    layout = str(parameters["frameLayout"])
    if layout not in {"left_right", "top_bottom", "four_sides"}:
        raise ValueError(f"不支持的外框布置：{layout}")
    horizontal_margin = frame.width if layout != "left_right" else 0.0
    horizontal_positions = [] if pattern == "vertical" else _center_spacing_positions(
        parameters, 0.0, height,
        start_offset_key="lastHorizontalBottomOffset",
        end_offset_key="firstHorizontalTopOffset",
        maximum_spacing_key="horizontalMaximumCenterSpacing", label="主横杆",
    )
    if horizontal is not None:
        _validate_bar_positions(horizontal_positions, horizontal_margin,
                                height - horizontal_margin, horizontal.width, "主横杆")
    vertical_margin = frame.width if layout != "top_bottom" else 0.0
    vertical_positions = [] if pattern == "horizontal" else _center_spacing_positions(
        parameters, vertical_margin, width - vertical_margin,
        start_offset_key="verticalLeftCenterOffset",
        end_offset_key="verticalRightCenterOffset",
        maximum_spacing_key="verticalMaximumCenterSpacing", label="主竖杆",
    )
    if vertical is not None:
        _validate_bar_positions(vertical_positions, vertical_margin, width - vertical_margin,
                                vertical.width, "主竖杆")
    if layout != "top_bottom" and horizontal_positions:
        reserve = _number(parameters, "horizontalBranchReserve")
        if reserve == 0:
            raise ValueError("主横杆插接入榫深度必须大于0")
        if manufacturing:
            _validate_insertion(frame, horizontal, reserve, clearance, "外框与主横杆")
    if layout != "left_right" and vertical_positions:
        reserve = _number(parameters, "verticalBranchReserve")
        if manufacturing:
            _validate_insertion(frame, vertical, reserve, clearance, "外框与主竖杆")
    processes: list[tuple[CornerProcess, Profile]] = []
    if layout == "four_sides":
        processes.append((_outer_process(parameters), frame))
    if parameters["accessDoorEnabled"]:
        processes.extend((
            (_process(parameters, "doorFrameJoinType", "doorFrameButtWrapMode"), _profile(parameters, "doorFrame")),
            (_process(parameters, "doorLeafFrameJoinType", "doorLeafFrameButtWrapMode"), _profile(parameters, "doorLeafFrame")),
        ))
    if manufacturing:
        _frame_geometry.validate_frame_processes(parameters, processes)
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
    door_horizontal = _profile(parameters, "doorHorizontal") if pattern != "vertical" else None
    door_vertical = _profile(parameters, "doorVertical") if pattern != "horizontal" else None
    if manufacturing and door_horizontal is not None and door_vertical is not None:
        _validate_through_fit(door_horizontal, door_vertical, clearance, "窗内横杆与窗内竖杆")
    gap = _number(parameters, "doorGap")
    required = 2 * (door_frame.width + leaf_frame.width + gap)
    if gap < 0 or door_width <= required or door_height <= required:
        raise ValueError("逃生窗尺寸不足以容纳窗框、窗扇框和间隙")
    inset = door_frame.width + gap + leaf_frame.width
    pattern = str(parameters.get("infillPattern", "grid"))
    horizontal_positions = [] if pattern == "vertical" else _center_spacing_positions(
        parameters, inset, door_height - inset,
        start_offset_key="doorHorizontalBottomCenterOffset",
        end_offset_key="doorHorizontalTopCenterOffset",
        maximum_spacing_key="doorHorizontalMaximumCenterSpacing", label="窗内横杆",
    )
    vertical_positions = [] if pattern == "horizontal" else _center_spacing_positions(
        parameters, inset, door_width - inset,
        start_offset_key="doorVerticalLeftCenterOffset",
        end_offset_key="doorVerticalRightCenterOffset",
        maximum_spacing_key="doorVerticalMaximumCenterSpacing", label="窗内竖杆",
    )
    if door_horizontal is not None:
        _validate_bar_positions(horizontal_positions, inset, door_height - inset,
                                door_horizontal.width, "窗内横杆")
    if door_vertical is not None:
        _validate_bar_positions(vertical_positions, inset, door_width - inset,
                                door_vertical.width, "窗内竖杆")
    if manufacturing and vertical_positions:
        _validate_insertion(leaf_frame, door_vertical, leaf_frame.width / 2, clearance,
                            "窗扇框与窗内竖杆")


def _external_end_anchor(part: Part, end: str, point: tuple[float, float, float],
                         approach_face: str | None = None,
                         stock_allowance: float = 0.0) -> dict[str, Any]:
    anchor = {"kind": "end", "end": end, "stockAllowance": stock_allowance}
    if approach_face is not None:
        anchor["approachFace"] = approach_face
        anchor["contactInset"] = stock_allowance
    return {"itemKey": part.key, "kind": "end", "end": end, "anchor": anchor,
            "localAxialStation": 0.0 if end == "start" else part.length,
            "centerlinePoint": list(point)}


def _external_face(part: Part, normal: tuple[float, float, float]) -> str:
    axis = [(part.end[index] - part.start[index]) / part.length for index in range(3)]
    local_z = (-axis[2], 0.0, axis[0])
    across_y = normal[1]
    across_z = sum(normal[index] * local_z[index] for index in range(3))
    if abs(across_y) > 0.99:
        return "right" if across_y > 0 else "left"
    if abs(across_z) > 0.99:
        return "top" if across_z > 0 else "bottom"
    raise ValueError("装配节点的侧面法向与构件轴线不垂直")


def _external_side_anchor(part: Part, point: tuple[float, float, float],
                          normal: tuple[float, float, float],
                          contact: tuple[float, float, float]) -> dict[str, Any]:
    station = math.dist(part.start, point)
    face = _external_face(part, normal)
    return {"itemKey": part.key, "kind": "side", "face": face,
            "anchor": {"kind": "side", "face": face, "reference": "start", "station": station},
            "localAxialStation": station, "centerlinePoint": list(point),
            "faceNormal": list(normal), "contactPoint": list(contact)}


def _external_rectangular_mirror_center(contours: list[dict[str, Any]]) -> float:
    center = None
    for contour in contours:
        segments = contour.get("segments", [])
        if (contour.get("kind") != "path" or contour.get("closed") is not True
                or len(segments) != 4 or any(
                    segment.get("kind") != "line"
                    or ((segment["start"][0] == segment["end"][0])
                        == (segment["start"][1] == segment["end"][1]))
                    for segment in segments)):
            raise ValueError("防盗窗提供截面的横向原管暂只支持镜像对称矩形轮廓")
        points = {tuple(point) for segment in segments
                  for point in (segment["start"], segment["end"])}
        xs, ys = sorted({point[0] for point in points}), sorted({point[1] for point in points})
        if len(xs) != 2 or len(ys) != 2 or points != {
                (x, y) for x in xs for y in ys}:
            raise ValueError("防盗窗提供截面的横向原管暂只支持镜像对称矩形轮廓")
        current = (ys[0] + ys[1]) / 2
        if center is not None and abs(center - current) > 1.0e-8:
            raise ValueError("防盗窗提供截面的内外轮廓没有共同镜像基准")
        center = current
    return center


def _external_section_geometry(part: Part, stock_part: Part,
                               stock_interval: dict[str, float]) -> dict[str, Any]:
    # Use the same profile2d declaration as the emitted stock. In particular,
    # horizontal sections swap their contour coordinates in that declaration.
    arguments = _profile_arguments(stock_part.start, stock_part.end, part.profile)
    placement = arguments["placement"]
    axis = [(part.end[i] - part.start[i]) / part.length for i in range(3)]
    x_axis, y_axis = placement["xAxis"], placement["yAxis"]
    canonical = part.profile.contours()
    emitted = arguments["contours"]
    if emitted != canonical:
        if emitted != [_profile_catalog._swap_contour_axes(contour) for contour in canonical]:
            raise ValueError("防盗窗原管截面与提供的管型轮廓不一致")
        x_axis, y_axis = y_axis, x_axis
    cross = [x_axis[1] * y_axis[2] - x_axis[2] * y_axis[1],
             x_axis[2] * y_axis[0] - x_axis[0] * y_axis[2],
             x_axis[0] * y_axis[1] - x_axis[1] * y_axis[0]]
    alignment = sum(cross[i] * axis[i] for i in range(3))
    mirror_offset = 0.0
    old_y_axis = y_axis
    if alignment < -1.0 + 1.0e-8:
        if part.profile.properties().get("geometrySource") == "providedBoundary":
            mirror_offset = 2 * _external_rectangular_mirror_center(canonical)
        y_axis = [-value for value in y_axis]
    elif alignment < 1.0 - 1.0e-8:
        raise ValueError("防盗窗原管截面与构件轴向不一致")
    origin = [placement["origin"][i] + mirror_offset * old_y_axis[i]
              - axis[i] * stock_interval["startStation"]
              for i in range(3)]
    delta = [part.start[i] - origin[i] for i in range(3)]
    top = [-axis[2], 0.0, axis[0]]
    return {"originAtStart": origin, "xAxis": x_axis, "yAxis": y_axis,
            "centerlineUV": [sum(delta[i] * x_axis[i] for i in range(3)),
                             sum(delta[i] * y_axis[i] for i in range(3))],
            "faceNormals": {"top": top, "bottom": [-value for value in top],
                            "right": [0.0, 1.0, 0.0], "left": [0.0, -1.0, 0.0]}}


def _generate_external_geometry(parameters: dict[str, Any], context: dict[str, Any]) -> dict[str, Any]:
    """Emit a finished layout and uncut members; node templates own every cut."""
    if parameters.get("faceType", "single") != "single":
        raise ValueError("独立装配工艺目前仅支持单面防盗窗")
    if parameters.get("frameLayout") != "four_sides":
        raise ValueError("独立装配工艺目前需要完整四边框")
    if parameters.get("accessDoorEnabled", False):
        raise ValueError("独立装配工艺目前不支持开启口")
    pattern = str(parameters.get("infillPattern", "grid"))
    if pattern not in {"horizontal", "vertical"}:
        raise ValueError("横竖贯穿格栅需要交叉节点工艺；独立装配目前只支持纯横杆或纯竖杆")
    width, height = _number(parameters, "width"), _number(parameters, "height")
    frame = _profile(parameters, "frame")
    branch = _profile(parameters, "horizontal" if pattern == "horizontal" else "vertical")
    if width <= frame.width * 2 or height <= frame.width * 2:
        raise ValueError("产品宽高必须大于外框宽度的两倍")
    if branch.width >= frame.width or branch.depth >= frame.depth:
        raise ValueError("填充杆件的宽深须小于外框")
    half = frame.width / 2
    items: list[Part] = []
    counters: dict[str, int] = {}
    for role, name, start, end in (
            ("outer_frame.left", "左边框", (half, 0, half), (half, 0, height - half)),
            ("outer_frame.right", "右边框", (width - half, 0, half), (width - half, 0, height - half)),
            ("outer_frame.bottom", "下边框", (half, 0, half), (width - half, 0, half)),
            ("outer_frame.top", "上边框", (half, 0, height - half), (width - half, 0, height - half))):
        _add_part(items, counters, role, name, start, end, frame,
                  category_key="outer_frame", category_name="外框")

    if pattern == "horizontal":
        positions = _center_spacing_positions(
            parameters, 0.0, height, start_offset_key="lastHorizontalBottomOffset",
            end_offset_key="firstHorizontalTopOffset",
            maximum_spacing_key="horizontalMaximumCenterSpacing", label="主横杆")
        _validate_bar_positions(positions, frame.width, height - frame.width,
                                branch.width, "主横杆")
        for index, z in enumerate(positions, start=1):
            _add_part(items, counters, "main_grid.horizontal", f"主横杆 {index}",
                      (half, 0, z), (width - half, 0, z), branch,
                      category_key="main_grid.horizontal", category_name="主横杆")
    else:
        positions = _center_spacing_positions(
            parameters, frame.width, width - frame.width,
            start_offset_key="verticalLeftCenterOffset",
            end_offset_key="verticalRightCenterOffset",
            maximum_spacing_key="verticalMaximumCenterSpacing", label="主竖杆")
        _validate_bar_positions(positions, frame.width, width - frame.width,
                                branch.width, "主竖杆")
        for index, x in enumerate(positions, start=1):
            _add_part(items, counters, "main_grid.vertical", f"主竖杆 {index}",
                      (x, 0, half), (x, 0, height - half), branch,
                      category_key="main_grid.vertical", category_name="主竖杆")

    model = NeutralModel(
        template_id=TEMPLATE_ID, template_version=TEMPLATE_VERSION,
        package_digest=str(context.get("template", {}).get("packageDigest", "")),
        parameters=deepcopy(parameters))
    geometry = SharedTubeGeometry(model)
    by_key = {part.key: part for part in items}
    horizontal_section = _profile_arguments((0, 0, 0), (1, 0, 0), frame)
    vertical_section = _profile_arguments((0, 0, 0), (0, 0, 1), frame)
    vertical_allowance = _stock_rules.opposing_half_extent(horizontal_section, (0, 0, 1), node_point=(0, 0, 0))
    horizontal_allowance = _stock_rules.opposing_half_extent(vertical_section, (1, 0, 0), node_point=(0, 0, 0))
    stock_spans = {}
    for part in items:
        allowance = vertical_allowance if part.vertical else horizontal_allowance
        stock_spans[part.key] = _stock_rules.stock_span(part.start, part.end, allowance, allowance)
    item_keys = []
    rows = []
    for index, part in enumerate(items, start=1):
        span = stock_spans[part.key]
        stock_part = replace(part, start=tuple(span["start"]), end=tuple(span["end"]))
        stock = _emit_tube_geometry(model, stock_part, geometry)[1]
        section_frame = _external_section_geometry(part, stock_part, span["stockInterval"])
        axis = [(part.end[coordinate] - part.start[coordinate]) / part.length
                for coordinate in range(3)]
        item_keys.append(model.item(part.key, part.name,
            representations={"display": stock, "export": stock}, properties={
                "partNumber": f"{parameters['productCode']}-{index:03d}",
                "quantity": 1, "length": round(span["length"], 3),
                "manufacturing.partKind": "tube", "manufacturing.sourcing": "made",
                "manufacturing.materialCategory": "tube",
                "manufacturing.categoryKey": part.category_key,
                "manufacturing.categoryName": part.category_name,
                "tubeDesigner.profile": _profile_properties(part.profile),
                "tubeDesigner.manufacturingAxis": axis,
                "tubeDesigner.manufacturingStartToEnd": axis,
                "tubeDesigner.endProcess": {"startCut": "square", "endCut": "square",
                                             "lengthBasis": "blank_axial_extent"},
                "tubeDesigner.assemblyPlanning": {"stockState": "uncut", "ready": False},
                "assemblyFrame.member": {"start": list(part.start), "end": list(part.end),
                                         "stockState": "uncut", "axisLength": part.length,
                                         "sectionFrame": section_frame,
                                         "stockInterval": span["stockInterval"]},
            }))
        rows.append({"key": f"row.{part.key}", "itemKey": part.key,
                     "values": {"partNumber": f"{parameters['productCode']}-{index:03d}",
                                "name": part.name, "quantity": 1,
                                "length": round(span["length"], 3)}})

    corners = (
        ("bottom-left", "outer_frame.left.0001", "start", (1, 0, 0),
         "outer_frame.bottom.0001", "start", (0, 0, 1), (half, 0, half)),
        ("bottom-right", "outer_frame.right.0001", "start", (-1, 0, 0),
         "outer_frame.bottom.0001", "end", (0, 0, 1), (width - half, 0, half)),
        ("top-left", "outer_frame.left.0001", "end", (1, 0, 0),
         "outer_frame.top.0001", "start", (0, 0, -1), (half, 0, height - half)),
        ("top-right", "outer_frame.right.0001", "end", (-1, 0, 0),
         "outer_frame.top.0001", "end", (0, 0, -1), (width - half, 0, height - half)),
    )
    for label, vertical_key, vertical_end, vertical_normal, horizontal_key, horizontal_end, horizontal_normal, point in corners:
        vertical, horizontal = by_key[vertical_key], by_key[horizontal_key]
        model.relationship(f"outer_frame.corner.{label}", "assembly",
            [vertical_key, horizontal_key], properties={
                "topology": "L", "centerlinePoint": list(point),
                "participantAnchors": [
                    _external_end_anchor(vertical, vertical_end, point,
                                         _external_face(vertical, vertical_normal),
                                         stock_spans[vertical_key]["allowances"][vertical_end]),
                    _external_end_anchor(horizontal, horizontal_end, point,
                                         _external_face(horizontal, horizontal_normal),
                                         stock_spans[horizontal_key]["allowances"][horizontal_end]),
                ]})
    for branch_part in items[4:]:
        for side in ("start", "end"):
            if pattern == "horizontal":
                left = side == "start"
                host = by_key["outer_frame.left.0001" if left else "outer_frame.right.0001"]
                point = branch_part.start if left else branch_part.end
                normal = (1, 0, 0) if left else (-1, 0, 0)
                contact = (frame.width if left else width - frame.width, 0, point[2])
            else:
                bottom = side == "start"
                host = by_key["outer_frame.bottom.0001" if bottom else "outer_frame.top.0001"]
                point = branch_part.start if bottom else branch_part.end
                normal = (0, 0, 1) if bottom else (0, 0, -1)
                contact = (point[0], 0, frame.width if bottom else height - frame.width)
            host_anchor = _external_side_anchor(host, point, normal, contact)
            branch_anchor = _external_end_anchor(
                branch_part, side, point,
                stock_allowance=stock_spans[branch_part.key]["allowances"][side])
            branch_anchor["anchor"]["contactInset"] = math.dist(point, contact)
            model.relationship(f"outer_frame.junction.{branch_part.key}.{side}",
                "assembly", [host.key, branch_part.key], properties={
                    "topology": "T", "centerlinePoint": list(point),
                    "participantAnchors": [host_anchor, branch_anchor]})
    model.output("display.default", "display", item_keys)
    model.output("export.manufacturing", "export", item_keys)
    model.table("parts", "原管清单", columns=[
        {"key": "partNumber", "displayName": "零件编号", "valueType": "string"},
        {"key": "name", "displayName": "名称", "valueType": "string"},
        {"key": "quantity", "displayName": "数量", "valueType": "integer"},
        {"key": "length", "displayName": "原管长度", "valueType": "number", "unit": "mm"},
    ], rows=rows)
    document = finish_geometry_request(model, context)
    document.setdefault("extensions", {})["tubeDesigner.specificationAnnotations"] = [
        {"id": f"security-window.front.{key}", "parameter": key, "kind": "linear",
         "face": "front", "start": list(start), "end": list(end),
         "offset": list(offset), "generatedValue": parameters[key]}
        for key, start, end, offset in (
            ("width", (0, 0, 0), (width, 0, 0), (0, 0, -max(140, frame.width * 3.5))),
            ("height", (0, 0, 0), (0, 0, height), (-max(340, frame.width * 9), 0, 0)),
        )]
    document["extensions"]["tubeDesigner.securityWindowReview"] = {
        "reviewVersion": 2, "layout": "single-face", "dimensions": "outside",
        "boundary": "four_sided_frame", "productFamily": "BAR_GRILLE",
        "envelope": "FLAT", "infillPattern": pattern, "openingSystem": "FIXED",
        "siteVerification": "required", "structuralVerification": "required",
        "complianceCertified": False, "outerFrameConnection": "awaiting_node_templates",
        "outerFrameManufacturing": {"mode": "external_templates",
                                    "members": [part.key for part in items[:4]]},
        "construction": "各 L/T 节点选择独立装配工艺后，复核整件下料和连接。",
        "performance": {key: "not_verified" for key in (
            "fallProtection", "intrusionResistance", "escape", "anchorage")},
    }
    return document


def _generate_geometry(parameters: dict[str, Any], context: dict[str, Any]) -> dict[str, Any]:
    purpose = request_geometry_purpose(context)
    process_parameters={**parameters,'_assemblyDesignOnly':purpose=='display'}
    _validate(parameters, purpose != "display")
    user_mould_root = str(context.get("userMouldRoot", "")).strip()
    template = context["template"]
    model = NeutralModel(
        template_id=TEMPLATE_ID, template_version=TEMPLATE_VERSION,
        package_digest=str(template.get("packageDigest", "")), parameters=parameters,
    )
    shared_geometry = SharedTubeGeometry(model)
    width, height = _number(parameters, "width"), _number(parameters, "height")
    frame = _profile(parameters, "frame")
    pattern = str(parameters.get("infillPattern", "grid"))
    horizontal = _profile(parameters, "horizontal") if pattern != "vertical" else None
    vertical = _profile(parameters, "vertical") if pattern != "horizontal" else None
    layout = str(parameters["frameLayout"])
    items: list[ModelItem] = []
    counters: dict[str, int] = {}
    half = frame.width / 2

    if layout == "four_sides":
        _add_processed_rectangle(
            items, counters, "outer_frame", "大外框", 0, 0, width, height,
            frame, "main", _outer_process(parameters), process_parameters,
            "outerFrameGroove", user_mould_root,
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

    horizontal_reserve = _number(parameters, "horizontalBranchReserve") if horizontal is not None else 0.0
    vertical_reserve = _number(parameters, "verticalBranchReserve") if vertical is not None else 0.0
    has_side_frame = layout != "top_bottom"
    has_top_bottom_frame = layout != "left_right"
    horizontal_start = (frame.width-horizontal_reserve if purpose=='display' else half) if has_side_frame else 0.0
    horizontal_end = width-horizontal_start if has_side_frame else width
    vertical_start = (frame.width-vertical_reserve if purpose=='display' else half) if has_top_bottom_frame else 0.0
    vertical_end = height-vertical_start if has_top_bottom_frame else height
    door_enabled = bool(parameters["accessDoorEnabled"])
    door_left = door_bottom = door_right = door_top = 0.0
    if door_enabled:
        door_left, door_bottom = _number(parameters, "doorLeft"), _number(parameters, "doorBottom")
        door_right = door_left + _number(parameters, "doorWidth")
        door_top = door_bottom + _number(parameters, "doorHeight")

    pattern = str(parameters.get("infillPattern", "grid"))
    main_horizontal_positions = [] if pattern == "vertical" else _center_spacing_positions(
        parameters, 0.0, height,
        start_offset_key="lastHorizontalBottomOffset",
        end_offset_key="firstHorizontalTopOffset",
        maximum_spacing_key="horizontalMaximumCenterSpacing", label="主横杆",
    )
    for index, z in enumerate(main_horizontal_positions, start=1):
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
    main_vertical_positions = [] if pattern == "horizontal" else _center_spacing_positions(
        parameters, vertical_left, vertical_right,
        start_offset_key="verticalLeftCenterOffset",
        end_offset_key="verticalRightCenterOffset",
        maximum_spacing_key="verticalMaximumCenterSpacing", label="主竖杆",
    )
    for index, x in enumerate(main_vertical_positions, start=1):
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
            _process(parameters, "doorFrameJoinType", "doorFrameButtWrapMode"), process_parameters,
            "doorFrameGroove", user_mould_root,
        )
        inset = door_frame.width + _number(parameters, "doorGap")
        leaf_left, leaf_right = door_left + inset, door_right - inset
        leaf_bottom, leaf_top = door_bottom + inset, door_top - inset
        _add_processed_rectangle(
            items, counters, "access_door.leaf.frame", "活动窗扇框",
            leaf_left, leaf_bottom, leaf_right, leaf_top, leaf_frame,
            "access_door.leaf",
            _process(parameters, "doorLeafFrameJoinType", "doorLeafFrameButtWrapMode"), process_parameters,
            "doorLeafFrameGroove", user_mould_root,
        )
        inner_left, inner_right = leaf_left + leaf_frame.width, leaf_right - leaf_frame.width
        inner_bottom, inner_top = leaf_bottom + leaf_frame.width, leaf_top - leaf_frame.width
        door_horizontal = _profile(parameters, "doorHorizontal") if pattern != "vertical" else None
        door_vertical = _profile(parameters, "doorVertical") if pattern != "horizontal" else None
        pattern = str(parameters.get("infillPattern", "grid"))
        door_horizontal_positions = [] if pattern == "vertical" else _center_spacing_positions(
            parameters, inner_bottom, inner_top,
            start_offset_key="doorHorizontalBottomCenterOffset",
            end_offset_key="doorHorizontalTopCenterOffset",
            maximum_spacing_key="doorHorizontalMaximumCenterSpacing", label="窗内横杆",
        )
        door_vertical_positions = [] if pattern == "horizontal" else _center_spacing_positions(
            parameters, inner_left, inner_right,
            start_offset_key="doorVerticalLeftCenterOffset",
            end_offset_key="doorVerticalRightCenterOffset",
            maximum_spacing_key="doorVerticalMaximumCenterSpacing", label="窗内竖杆",
        )
        for index, z in enumerate(door_horizontal_positions, start=1):
            # Equal-size horizontal stock meets the leaf frame at its inner
            # face as a butt joint; it cannot be pushed into a smaller cavity.
            _add_part(items, counters, "access_door.leaf.horizontal", f"窗内横杆 {index}", (inner_left, 0, z), (inner_right, 0, z), door_horizontal, "access_door.leaf",
                      category_name="窗内横杆")
        for index, x in enumerate(door_vertical_positions, start=1):
            _add_part(items, counters, "access_door.leaf.vertical", f"窗内竖杆 {index}", (x, 0, inner_bottom - leaf_frame.width / 2), (x, 0, inner_top + leaf_frame.width / 2), door_vertical, "access_door.leaf",
                      category_name="窗内竖杆")

    if purpose!='display':
        machining=_frame_geometry._machining
        for part in list(items):
            if not isinstance(part,Part) or not part.key.startswith(('main_grid.','access_door.leaf.horizontal.','access_door.leaf.vertical.')):
                continue
            prefixes=('access_door.leaf.frame.',) if part.group=='access_door.leaf' else ('outer_frame.','access_door.fixed_frame.')
            updates={}
            for end in ('start','end'):
                receiver,mate=machining.select_end_mate(items,part,end,prefixes)
                if receiver is None:
                    continue
                if receiver.key.startswith('outer_frame.'):
                    mode='insert'
                    depth=vertical_reserve if part.vertical else horizontal_reserve
                elif part.group=='access_door.leaf' and part.vertical:
                    mode,depth='insert','mid-section'
                else:
                    mode,depth='butt',0.
                updates[end]=machining.allocate_joint(part,mate,end,mode,depth,
                    _number(parameters,'assemblyClearance'),lambda p:_profile_arguments(p.start,p.end,p.profile))
                updates[end+'_joint']=receiver.key
                updates[end+'_joint_mode']=mode
                updates[end+'_insertion']=depth
            if updates:
                items[items.index(part)]=_frame_geometry.replace(part,**updates)
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
        export = _frame_geometry._machining.planar_part(model,display,part,
            crossing_map.get(part.key,[]),clearance,items,
            lambda p: _profile_arguments(p.start,p.end,p.profile))
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
            properties["tubeDesigner.sourceSpans"] = _frame_geometry.frame_source_spans(item)
            properties["manufacturing.categoryKey"] = item.key.rsplit(".", 1)[0]
            properties["tubeDesigner.cornerProcess"] = {
                "joinType": item.process.join_type,
                "grooveStyle": item.process.groove_style,
                "bendAllowance": item.bend_allowance,
                "bendLocations": bend_locations[item.key],
            }
            if item.key.startswith("outer_frame."):
                properties["tubeDesigner.frameManufacturing"] = {
                    "mode":"plane_v_notch",
                    "closed":True, "closure":"straight_mid_edge",
                    "foldOrder":"from_last_station_to_first",
                    "bends":[{"sequence":i+1, "station":position, "angle":90.0, "rotation":0.0}
                             for i,position in enumerate(bend_locations[item.key])],
                }
        else:
            properties["tubeDesigner.designSegment"] = {"start":list(item.start), "end":list(item.end),
                "profileCoordinateMap": ([[0.,1.],[1.,0.]] if abs(item.end[0]-item.start[0]) >= abs(item.end[2]-item.start[2])
                                         else [[1.,0.],[0.,1.]]),
                **_profile_arguments(item.start, item.end, item.profile)}
            properties["manufacturing.categoryName"] = item.category_name or item.name
            properties["manufacturing.categoryKey"] = item.category_key or item.key.rsplit(".", 1)[0]
            properties["tubeDesigner.endProcess"] = {
                "startCut": item.start_cut, "endCut": item.end_cut,
            }
            if item.key.startswith("main_grid.horizontal."):
                main_joints[item.key] = _main_horizontal_joints(
                    item, items, reserve=horizontal_reserve,
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
        properties.update(deepcopy(getattr(model, "_assembly_process_records", {}).get(item.key, {})))
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

    if door_enabled:
        hinge_side = DOOR_HINGE_SIDE
        fixed_item = _frame_relationship_item(
            items, "access_door.fixed_frame", "access_door.fixed_frame", hinge_side,
        )
        moving_item = _frame_relationship_item(
            items, "access_door.leaf", "access_door.leaf.frame", hinge_side,
        )
        hinge_x = door_left if hinge_side == "left" else door_right
        for index in range(DOOR_HINGE_COUNT):
            hinge_z = door_bottom + (door_top - door_bottom) * (index + 1) / (DOOR_HINGE_COUNT + 1)
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
    document = finish_geometry_request(model, context)
    annotations: list[dict[str, Any]] = []

    def annotate(parameter: str, start: tuple[float, float, float],
                 end: tuple[float, float, float], offset: tuple[float, float, float],
                 *, kind: str = "linear", face: str = "front") -> None:
        annotations.append({
            "id": f"security-window.{face}.{parameter}",
            "parameter": parameter,
            "kind": kind,
            "face": face,
            "start": list(start),
            "end": list(end),
            "offset": list(offset),
            "generatedValue": parameters.get(parameter),
        })

    # These anchors are emitted by the same geometry kernel as the members.
    # The UI therefore receives model coordinates instead of reconstructing
    # the security-window layout from parameter names.
    # Keep semantically different dimensions on separate annotation lanes.
    # Labels stay at each dimension line's midpoint; only the line offsets are
    # deliberately staggered here so the viewport does not have to guess the
    # security-window layout from parameter names.
    outer_horizontal_lane = max(140.0, frame.width * 3.5)
    outer_vertical_lane = max(340.0, frame.width * 9.0)
    grid_side_lane = max(155.0, frame.width * 4.0)
    grid_top_lane = max(105.0, frame.width * 2.8)
    edge_detail_lane = max(88.0, frame.width * 2.3)
    door_side_lane = max(62.0, door_frame.width * 2.0) if door_enabled else 62.0
    door_top_lane = max(68.0, door_frame.width * 2.2) if door_enabled else 68.0
    door_bottom_lane = max(92.0, door_frame.width * 2.8) if door_enabled else 92.0

    annotate("width", (0.0, 0.0, 0.0), (width, 0.0, 0.0),
             (0.0, 0.0, -outer_horizontal_lane))
    annotate("height", (0.0, 0.0, 0.0), (0.0, 0.0, height),
             (-outer_vertical_lane, 0.0, 0.0))
    if main_horizontal_positions:
        annotate("firstHorizontalTopOffset", (width - frame.width, 0.0, height),
                 (width - frame.width, 0.0, main_horizontal_positions[-1]),
                 (edge_detail_lane, 0.0, 0.0))
        annotate("lastHorizontalBottomOffset", (width - frame.width, 0.0, 0.0),
                 (width - frame.width, 0.0, main_horizontal_positions[0]),
                 (edge_detail_lane * 1.8, 0.0, 0.0))
        if len(main_horizontal_positions) > 1:
            annotate("horizontalMaximumCenterSpacing",
                     (width, 0.0, main_horizontal_positions[0]),
                     (width, 0.0, main_horizontal_positions[1]),
                     (grid_side_lane, 0.0, 0.0), kind="spacing")
    if main_vertical_positions:
        annotate("verticalLeftCenterOffset", (vertical_left, 0.0, height - frame.width),
                 (main_vertical_positions[0], 0.0, height - frame.width),
                 (0.0, 0.0, grid_top_lane))
        annotate("verticalRightCenterOffset", (vertical_right, 0.0, height - frame.width),
                 (main_vertical_positions[-1], 0.0, height - frame.width),
                 (0.0, 0.0, grid_top_lane * 1.7))
        if len(main_vertical_positions) > 1:
            annotate("verticalMaximumCenterSpacing",
                     (main_vertical_positions[0], 0.0, height - frame.width),
                     (main_vertical_positions[1], 0.0, height - frame.width),
                     (0.0, 0.0, grid_top_lane * 2.4), kind="spacing")

    if door_enabled:
        door_frame_width = _profile(parameters, "doorFrame").width
        clear_left = door_left + door_frame_width
        clear_right = door_right - door_frame_width
        clear_bottom = door_bottom + door_frame_width
        clear_top = door_top - door_frame_width
        annotate("doorClearWidth", (clear_left, 0.0, clear_bottom),
                 (clear_right, 0.0, clear_bottom), (0.0, 0.0, -door_bottom_lane),
                 face="escape-window")
        annotate("doorClearHeight", (clear_left, 0.0, clear_bottom),
                 (clear_left, 0.0, clear_top), (-door_side_lane, 0.0, 0.0),
                 face="escape-window")
        leaf_left = clear_left + _number(parameters, "doorGap")
        annotate("doorGap", (clear_left, 0.0, clear_bottom),
                 (leaf_left, 0.0, clear_bottom), (0.0, 0.0, -door_bottom_lane * 3.0),
                 face="escape-window")
        annotate("doorLeft", (0.0, 0.0, door_bottom), (door_left, 0.0, door_bottom),
                 (0.0, 0.0, -door_bottom_lane * 4.6), face="escape-window")
        annotate("doorBottom", (door_left, 0.0, 0.0), (door_left, 0.0, door_bottom),
                 (-door_side_lane * 4.4, 0.0, 0.0), face="escape-window")
        horizontal_positions = [] if str(parameters.get("infillPattern", "grid")) == "vertical" else _center_spacing_positions(
            parameters, inner_bottom, inner_top,
            start_offset_key="doorHorizontalBottomCenterOffset",
            end_offset_key="doorHorizontalTopCenterOffset",
            maximum_spacing_key="doorHorizontalMaximumCenterSpacing", label="窗内横杆",
        )
        vertical_positions = [] if str(parameters.get("infillPattern", "grid")) == "horizontal" else _center_spacing_positions(
            parameters, inner_left, inner_right,
            start_offset_key="doorVerticalLeftCenterOffset",
            end_offset_key="doorVerticalRightCenterOffset",
            maximum_spacing_key="doorVerticalMaximumCenterSpacing", label="窗内竖杆",
        )
        if horizontal_positions:
            annotate("doorHorizontalTopCenterOffset", (inner_right, 0.0, inner_top),
                     (inner_right, 0.0, horizontal_positions[-1]), (door_side_lane, 0.0, 0.0),
                     face="escape-window")
            annotate("doorHorizontalBottomCenterOffset", (inner_right, 0.0, inner_bottom),
                     (inner_right, 0.0, horizontal_positions[0]), (door_side_lane * 1.8, 0.0, 0.0),
                     face="escape-window")
            if len(horizontal_positions) > 1:
                annotate("doorHorizontalMaximumCenterSpacing", (inner_right, 0.0, horizontal_positions[0]),
                         (inner_right, 0.0, horizontal_positions[1]), (door_side_lane * 2.6, 0.0, 0.0),
                         kind="spacing", face="escape-window")
        if vertical_positions:
            annotate("doorVerticalLeftCenterOffset", (inner_left, 0.0, inner_top),
                     (vertical_positions[0], 0.0, inner_top), (0.0, 0.0, door_top_lane),
                     face="escape-window")
            annotate("doorVerticalRightCenterOffset", (inner_right, 0.0, inner_top),
                     (vertical_positions[-1], 0.0, inner_top), (0.0, 0.0, door_top_lane * 1.8),
                     face="escape-window")
            if len(vertical_positions) > 1:
                annotate("doorVerticalMaximumCenterSpacing", (vertical_positions[0], 0.0, inner_top),
                         (vertical_positions[1], 0.0, inner_top), (0.0, 0.0, door_top_lane * 2.6),
                         kind="spacing", face="escape-window")

    document.setdefault("extensions", {})["tubeDesigner.specificationAnnotations"] = annotations
    return document


def _generate_manufacturing_geometry(parameters: dict[str, Any], context: dict[str, Any]) -> dict[str, Any]:
    planning_mode = parameters.get("assemblyPlanningMode", "builtin_rules")
    if parameters.get("faceType", "single") != "single":
        planning_mode = "builtin_rules"
    if planning_mode not in {"builtin_rules", "external_templates"}:
        raise ValueError("装配工艺来源不受支持")
    if planning_mode == "external_templates":
        return _generate_external_geometry(parameters, context)
    face_type = parameters.get("faceType", "single")
    if face_type not in {"single", "two", "three", "five"}:
        raise ValueError("防盗窗面型不受支持")
    if face_type != "single":
        script = PROFILE_CATALOG_SCRIPT.parent / "multi_face_security_window.py"
        name = "icax_security_window_multi_" + hashlib.sha256(script.read_bytes()).hexdigest()[:16]
        if name not in sys.modules:
            spec = importlib.util.spec_from_file_location(name, script)
            module = importlib.util.module_from_spec(spec)
            sys.modules[name] = module
            spec.loader.exec_module(module)
        effective = dict(parameters)
        effective["frontWidth"] = parameters["width"]
        effective["accessDoorFace"] = parameters.get("accessDoorFace" + {"two": "2", "three": "3", "five": "5"}[face_type], "front")
        document = sys.modules[name].generate_multi_face(effective, context, template_id=TEMPLATE_ID,
                                                        template_version=TEMPLATE_VERSION, layout=face_type + "-face")
        document["parameters"] = deepcopy(parameters)
        return document
    return generate_reviewed(parameters, context, layout="single-face",
                             load_profile=_profile, kernel=_generate_geometry)


def _generate_resource_document(parameters: dict[str, Any], context: dict[str, Any]) -> dict[str, Any]:
    script = PROFILE_CATALOG_SCRIPT.parent / "security_window_product_contract.py"
    name = "icax_security_window_contract_" + hashlib.sha256(script.read_bytes()).hexdigest()[:16]
    if name not in sys.modules:
        spec = importlib.util.spec_from_file_location(name, script)
        module = importlib.util.module_from_spec(spec)
        sys.modules[name] = module
        spec.loader.exec_module(module)
    return to_resource_model(sys.modules[name].generate(parameters, context, sys.modules[__name__]))


def display(parameter_values):
    """Generate display data from the values owned by the product instance."""
    return to_display_model(_generate_resource_document(parameter_values, display_context(__file__)))




def manufacturing(parameter_values):
    """Return manufacturing declarations from the values owned by the host."""
    script = PROFILE_CATALOG_SCRIPT.parent / "product_window_manufacturing.py"
    name = "icax_window_declaration_" + hashlib.sha256(script.read_bytes()).hexdigest()[:16]
    if name not in sys.modules:
        spec = importlib.util.spec_from_file_location(name, script)
        module = importlib.util.module_from_spec(spec)
        sys.modules[name] = module
        spec.loader.exec_module(module)
    with manufacturing_declaration():
        return to_manufacturing_model(sys.modules[name].build_window_manufacturing(
            parameter_values, manufacturing_context(__file__), sys.modules[__name__]))
