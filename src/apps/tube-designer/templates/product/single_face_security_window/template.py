from __future__ import annotations

from dataclasses import dataclass
import hashlib
import importlib.util
import math
from pathlib import Path
import sys
from typing import Any

from icax_template_sdk import NeutralModel


TEMPLATE_ID = "single-face-security-window"
TEMPLATE_VERSION = "3.0.0"

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
_vertical_count = _frame_geometry._vertical_count
_horizontal_positions = _frame_geometry._horizontal_positions
_next_key = _frame_geometry._next_key
_add_part = _frame_geometry._add_part
_add_processed_rectangle = _frame_geometry._add_processed_rectangle
_profile_arguments = _frame_geometry._profile_arguments
_emit_tube_geometry = _frame_geometry._emit_tube_geometry
_quadratic_points = _frame_geometry._quadratic_points
_groove_polygon = _frame_geometry._groove_polygon
_emit_polygon_cutter = _frame_geometry._emit_polygon_cutter
_unfold_frame_point = _frame_geometry._unfold_frame_point
_continuous_frame_cutters = _frame_geometry._continuous_frame_cutters
_emit_continuous_frame_geometry = _frame_geometry._emit_continuous_frame_geometry
_crossings = _frame_geometry._crossings
_emit_crossing_cutter = _frame_geometry._emit_crossing_cutter
_miter_triangles = _frame_geometry._miter_triangles
_emit_miter_cutters = _frame_geometry._emit_miter_cutters
_profile_properties = _frame_geometry._profile_properties
_frame_relationship_item = _frame_geometry._frame_relationship_item
_validate_through_fit = _frame_geometry._validate_through_fit
_validate_insertion = _frame_geometry._validate_insertion
_validate_flat_weld = _frame_geometry._validate_flat_weld
_main_horizontal_joints = _frame_geometry._main_horizontal_joints
ModelItem = Part | ContinuousFrame


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


def generate(parameters: dict[str, Any], context: dict[str, Any]) -> dict[str, Any]:
    return generate_reviewed(parameters, context, layout="single-face",
                             load_profile=_profile, kernel=_generate_geometry)
