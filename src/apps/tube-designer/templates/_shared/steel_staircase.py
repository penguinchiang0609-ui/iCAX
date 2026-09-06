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

SHARED_GEOMETRY_SCRIPT = PROFILE_CATALOG_SCRIPT.parent / "shared_tube_geometry.py"
SHARED_GEOMETRY_MODULE = "icax_shared_tube_geometry_" + hashlib.sha256(
    SHARED_GEOMETRY_SCRIPT.read_bytes()
).hexdigest()[:16]
if SHARED_GEOMETRY_MODULE not in sys.modules:
    _shared_geometry_spec = importlib.util.spec_from_file_location(
        SHARED_GEOMETRY_MODULE, SHARED_GEOMETRY_SCRIPT,
    )
    if _shared_geometry_spec is None or _shared_geometry_spec.loader is None:
        raise RuntimeError("无法加载中性结果请求规则")
    _shared_geometry_module = importlib.util.module_from_spec(_shared_geometry_spec)
    sys.modules[SHARED_GEOMETRY_MODULE] = _shared_geometry_module
    _shared_geometry_spec.loader.exec_module(_shared_geometry_module)
request_geometry_purpose = sys.modules[SHARED_GEOMETRY_MODULE].request_geometry_purpose
finish_geometry_request = sys.modules[SHARED_GEOMETRY_MODULE].finish_geometry_request


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
    assembly_group: str

    @property
    def length(self) -> float:
        return math.dist(self.start, self.end)


@dataclass(frozen=True)
class Flight:
    key: str
    name: str
    origin: Point
    direction: tuple[float, float]
    risers: int


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
    depth_key = "treadDepthProfile" if prefix == "tread" else f"{prefix}Depth"
    return _profile_catalog.load_profile(
        parameters, prefix, depth_key=depth_key,
    )


def _add(start: Point, vector: Vector, scale: float = 1.0) -> Point:
    return (
        start[0] + vector[0] * scale,
        start[1] + vector[1] * scale,
        start[2] + vector[2] * scale,
    )


def _subtract(left: Point, right: Point) -> Vector:
    return (left[0] - right[0], left[1] - right[1], left[2] - right[2])


def _tube(
    key: str,
    name: str,
    start: Point,
    end: Point,
    profile: Profile,
    profile_x_axis: Vector,
    profile_y_axis: Vector,
    category_key: str,
    category_name: str,
    group: str,
) -> Part:
    if math.dist(start, end) <= 0.01:
        raise ValueError(f"{name} 的长度无效")
    return Part(
        key, name, start, end, profile, profile_x_axis, profile_y_axis,
        category_key, category_name, group,
    )


def _profile_arguments(part: Part) -> dict[str, Any]:
    return {
        "placement": {
            "origin": list(part.start),
            "xAxis": list(part.profile_x_axis),
            "yAxis": list(part.profile_y_axis),
        },
        "contours": part.profile.contours(swap_axes=True),
    }


def _emit_tube(model: NeutralModel, part: Part) -> str:
    vector = list(_subtract(part.end, part.start))
    profile = model.geometry(
        f"{part.key}.profile", "profile2d",
        arguments=_profile_arguments(part),
    )
    return model.geometry(
        f"{part.key}.solid", "extrude", inputs=[profile],
        arguments={"vector": vector},
    )


def _profile_properties(profile: Profile) -> dict[str, Any]:
    return profile.properties()


def _validate(parameters: dict[str, Any], layout: str) -> None:
    floor_height = _number(parameters, "floorHeight")
    stair_width = _number(parameters, "stairWidth")
    tread_depth = _number(parameters, "treadDepth")
    risers = _integer(parameters, "totalRiserCount")
    if floor_height <= 0 or stair_width <= 0 or tread_depth <= 0:
        raise ValueError("层高、梯段宽度和踏步宽度必须大于0")
    if not 3 <= risers <= 60:
        raise ValueError("总级数必须在3到60之间")
    if floor_height / risers > 220:
        raise ValueError("按当前层高和级数计算的踏步高度超过220mm")
    if layout != "straight":
        first = _integer(parameters, "firstFlightRiserCount")
        if first < 2 or risers - first < 2:
            raise ValueError("两个梯段都必须至少包含2级")
        if _number(parameters, "landingLength") < stair_width:
            raise ValueError("平台长度不能小于梯段宽度")
    if layout == "u_turn" and _number(parameters, "wellGap") < 0:
        raise ValueError("梯井净宽不能小于0")
    if str(parameters["railingSide"]) not in {"both", "left", "right", "none"}:
        raise ValueError("护栏侧别不受支持")


def _flight_parts(
    flight: Flight,
    *,
    riser_height: float,
    tread_depth: float,
    stair_width: float,
    stringer: Profile,
    tread: Profile,
    handrail: Profile,
    post: Profile,
    railing_side: str,
    railing_height: float,
    post_spacing: float,
) -> list[Part]:
    dx, dy = flight.direction
    side = (-dy, dx, 0.0)
    run = flight.risers * tread_depth
    rise = flight.risers * riser_height
    slope_length = math.hypot(run, rise)
    slope = (dx * run / slope_length, dy * run / slope_length, rise / slope_length)
    slope_normal = (-dx * rise / slope_length, -dy * rise / slope_length, run / slope_length)
    cross_axis = (dx, dy, 0.0)
    vertical_axis = side
    stringer_offset = max(0.0, stair_width / 2.0 - stringer.depth / 2.0)
    stringer_drop = max(stringer.width / 2.0, tread.depth + stringer.width / 2.0)
    parts: list[Part] = []

    for index, lateral in enumerate((-stringer_offset, stringer_offset), start=1):
        start = _add(_add(flight.origin, side, lateral), (0.0, 0.0, -stringer_drop))
        end = _add(start, (dx * run, dy * run, rise))
        parts.append(_tube(
            f"{flight.key}.stringer.{index:02d}", f"{flight.name}边梁 {index}",
            start, end, stringer, side, slope_normal,
            "stair.stringer", "梯段边梁", flight.key,
        ))

    for index in range(flight.risers):
        distance = (index + 1) * tread_depth
        elevation = (index + 1) * riser_height - tread.depth / 2.0
        center = _add(flight.origin, (dx, dy, 0.0), distance)
        center = _add(center, (0.0, 0.0, 1.0), elevation)
        start = _add(center, side, -stair_width / 2.0)
        end = _add(center, side, stair_width / 2.0)
        parts.append(_tube(
            f"{flight.key}.tread.{index + 1:03d}", f"{flight.name}踏步横梁 {index + 1}",
            start, end, tread, cross_axis, (0.0, 0.0, 1.0),
            "stair.tread_support", "踏步横梁", flight.key,
        ))

    selected_sides: Iterable[tuple[str, float]]
    if railing_side == "both":
        selected_sides = (("left", 1.0), ("right", -1.0))
    elif railing_side == "left":
        selected_sides = (("left", 1.0),)
    elif railing_side == "right":
        selected_sides = (("right", -1.0),)
    else:
        selected_sides = ()

    post_count = max(2, math.ceil(run / max(100.0, post_spacing)) + 1)
    for side_name, sign in selected_sides:
        lateral = sign * (stair_width / 2.0 - post.depth / 2.0)
        rail_start_base = _add(flight.origin, side, lateral)
        rail_end_base = _add(rail_start_base, (dx * run, dy * run, rise))
        rail_start = _add(rail_start_base, (0.0, 0.0, 1.0), railing_height)
        rail_end = _add(rail_end_base, (0.0, 0.0, 1.0), railing_height)
        parts.append(_tube(
            f"{flight.key}.handrail.{side_name}", f"{flight.name}{'左' if sign > 0 else '右'}侧扶手",
            rail_start, rail_end, handrail, side, slope_normal,
            "stair.handrail", "扶手管", flight.key,
        ))
        for index in range(post_count):
            ratio = index / (post_count - 1)
            base = _add(rail_start_base, (dx * run, dy * run, rise), ratio)
            top = _add(base, (0.0, 0.0, 1.0), railing_height)
            parts.append(_tube(
                f"{flight.key}.post.{side_name}.{index + 1:03d}",
                f"{flight.name}{'左' if sign > 0 else '右'}侧立柱 {index + 1}",
                base, top, post, cross_axis, side,
                "stair.railing_post", "护栏立柱", flight.key,
            ))
    return parts


def _landing_parts(
    key: str,
    name: str,
    minimum_x: float,
    maximum_x: float,
    minimum_y: float,
    maximum_y: float,
    elevation: float,
    tread: Profile,
) -> list[Part]:
    z = elevation - tread.depth / 2.0
    x_axis = (1.0, 0.0, 0.0)
    y_axis = (0.0, 1.0, 0.0)
    vertical = (0.0, 0.0, 1.0)
    parts = [
        _tube(f"{key}.edge.front", f"{name}前边梁", (minimum_x, minimum_y, z), (maximum_x, minimum_y, z), tread, y_axis, vertical, "stair.landing_frame", "平台骨架", key),
        _tube(f"{key}.edge.back", f"{name}后边梁", (minimum_x, maximum_y, z), (maximum_x, maximum_y, z), tread, y_axis, vertical, "stair.landing_frame", "平台骨架", key),
        _tube(f"{key}.edge.left", f"{name}左边梁", (minimum_x, minimum_y, z), (minimum_x, maximum_y, z), tread, x_axis, vertical, "stair.landing_frame", "平台骨架", key),
        _tube(f"{key}.edge.right", f"{name}右边梁", (maximum_x, minimum_y, z), (maximum_x, maximum_y, z), tread, x_axis, vertical, "stair.landing_frame", "平台骨架", key),
    ]
    span = maximum_x - minimum_x
    support_count = max(1, math.ceil(span / 500.0) - 1)
    for index in range(support_count):
        x = minimum_x + span * (index + 1) / (support_count + 1)
        parts.append(_tube(
            f"{key}.support.{index + 1:03d}", f"{name}中间横梁 {index + 1}",
            (x, minimum_y, z), (x, maximum_y, z), tread, x_axis, vertical,
            "stair.landing_support", "平台横梁", key,
        ))
    return parts


def _landing_guard_parts(
    key: str,
    name: str,
    segments: Iterable[tuple[Point, Point]],
    elevation: float,
    railing_height: float,
    handrail: Profile,
    post: Profile,
) -> list[Part]:
    parts: list[Part] = []
    segment_list = list(segments)
    for index, (raw_start, raw_end) in enumerate(segment_list, start=1):
        dx = raw_end[0] - raw_start[0]
        dy = raw_end[1] - raw_start[1]
        length = math.hypot(dx, dy)
        direction = (dx / length, dy / length, 0.0)
        side = (-direction[1], direction[0], 0.0)
        start = (raw_start[0], raw_start[1], elevation + railing_height)
        end = (raw_end[0], raw_end[1], elevation + railing_height)
        parts.append(_tube(
            f"{key}.handrail.{index:02d}", f"{name}扶手 {index}",
            start, end, handrail, side, (0.0, 0.0, 1.0),
            "stair.landing_handrail", "平台扶手", key,
        ))
        post_points = (raw_start, raw_end) if index == len(segment_list) else (raw_start,)
        for post_index, point in enumerate(post_points, start=1):
            parts.append(_tube(
                f"{key}.post.{index:02d}.{post_index:02d}", f"{name}立柱 {index}-{post_index}",
                (point[0], point[1], elevation),
                (point[0], point[1], elevation + railing_height),
                post, direction, side,
                "stair.landing_post", "平台护栏立柱", key,
            ))
    return parts


def _layout_parts(parameters: dict[str, Any], layout: str) -> list[Part]:
    _validate(parameters, layout)
    floor_height = _number(parameters, "floorHeight")
    stair_width = _number(parameters, "stairWidth")
    tread_depth = _number(parameters, "treadDepth")
    total_risers = _integer(parameters, "totalRiserCount")
    riser_height = floor_height / total_risers
    stringer = _profile(parameters, "stringer")
    tread = _profile(parameters, "tread")
    handrail = _profile(parameters, "handrail")
    post = _profile(parameters, "post")
    railing_side = str(parameters["railingSide"])
    railing_height = _number(parameters, "railingHeight")
    post_spacing = _number(parameters, "maximumPostSpacing")

    if layout == "straight":
        flights = [Flight("flight.1", "直跑梯段", (0.0, 0.0, 0.0), (1.0, 0.0), total_risers)]
        landing_parts: list[Part] = []
    else:
        first_risers = _integer(parameters, "firstFlightRiserCount")
        second_risers = total_risers - first_risers
        first_run = first_risers * tread_depth
        middle_height = first_risers * riser_height
        landing_length = _number(parameters, "landingLength")
        if layout == "l_turn":
            turn_sign = 1.0 if str(parameters["turnDirection"]) == "left" else -1.0
            flights = [
                Flight("flight.1", "第一梯段", (0.0, 0.0, 0.0), (1.0, 0.0), first_risers),
                Flight(
                    "flight.2", "第二梯段",
                    (first_run + landing_length / 2.0, turn_sign * stair_width / 2.0, middle_height),
                    (0.0, turn_sign), second_risers,
                ),
            ]
            landing_parts = _landing_parts(
                "landing.1", "转角平台", first_run, first_run + landing_length,
                -stair_width / 2.0, stair_width / 2.0, middle_height, tread,
            )
            if railing_side != "none":
                outer_y = -turn_sign * stair_width / 2.0
                far_x = first_run + landing_length
                landing_parts.extend(_landing_guard_parts(
                    "landing_guard.1", "转角平台",
                    [
                        ((first_run, outer_y, middle_height), (far_x, outer_y, middle_height)),
                        ((far_x, outer_y, middle_height), (far_x, -outer_y, middle_height)),
                    ],
                    middle_height, railing_height, handrail, post,
                ))
        else:
            well_gap = _number(parameters, "wellGap")
            lane_offset = (stair_width + well_gap) / 2.0
            flights = [
                Flight("flight.1", "上行梯段", (0.0, -lane_offset, 0.0), (1.0, 0.0), first_risers),
                Flight("flight.2", "返回梯段", (first_run, lane_offset, middle_height), (-1.0, 0.0), second_risers),
            ]
            landing_parts = _landing_parts(
                "landing.1", "回转平台", first_run, first_run + landing_length,
                -lane_offset - stair_width / 2.0, lane_offset + stair_width / 2.0,
                middle_height, tread,
            )
            if railing_side != "none":
                minimum_y = -lane_offset - stair_width / 2.0
                maximum_y = lane_offset + stair_width / 2.0
                far_x = first_run + landing_length
                landing_parts.extend(_landing_guard_parts(
                    "landing_guard.1", "回转平台",
                    [
                        ((first_run, minimum_y, middle_height), (far_x, minimum_y, middle_height)),
                        ((far_x, minimum_y, middle_height), (far_x, maximum_y, middle_height)),
                        ((far_x, maximum_y, middle_height), (first_run, maximum_y, middle_height)),
                    ],
                    middle_height, railing_height, handrail, post,
                ))

    parts = list(landing_parts)
    for flight in flights:
        parts.extend(_flight_parts(
            flight,
            riser_height=riser_height,
            tread_depth=tread_depth,
            stair_width=stair_width,
            stringer=stringer,
            tread=tread,
            handrail=handrail,
            post=post,
            railing_side=railing_side,
            railing_height=railing_height,
            post_spacing=post_spacing,
        ))
    return parts


def generate_steel_staircase(
    parameters: dict[str, Any],
    context: dict[str, Any],
    *,
    template_id: str,
    template_version: str,
    layout: str,
) -> dict[str, Any]:
    request_geometry_purpose(context)
    model = NeutralModel(
        template_id=template_id,
        template_version=template_version,
        package_digest=str(context["template"].get("packageDigest", "")),
        parameters=parameters,
    )
    parts = _layout_parts(parameters, layout)
    item_keys: list[str] = []
    rows: list[dict[str, Any]] = []
    for index, part in enumerate(parts, start=1):
        representation = _emit_tube(model, part)
        part_number = f"{parameters['productCode']}-{index:03d}"
        properties = {
            "partNumber": part_number,
            "quantity": 1,
            "group": part.assembly_group,
            "length": round(part.length, 3),
            "manufacturing.categoryKey": part.category_key,
            "manufacturing.categoryName": part.category_name,
            "tubeDesigner.profile": _profile_properties(part.profile),
            "tubeDesigner.endProcess": {
                "startCut": "square",
                "endCut": "square",
                "connection": str(parameters["connectionType"]),
            },
        }
        item_key = model.item(
            part.key, part.name,
            representations={"display": representation, "export": representation},
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
                "category": part.category_name,
                "quantity": 1,
                "length": round(part.length, 3),
            },
        })

    groups: dict[str, list[str]] = {}
    for part in parts:
        groups.setdefault(part.assembly_group, []).append(part.key)
    for group_index, (group, keys) in enumerate(groups.items(), start=1):
        if len(keys) > 1:
            model.relationship(
                f"assembly.{group_index:03d}", "welded_assembly", keys,
                properties={"assemblyGroup": group, "connection": str(parameters["connectionType"])},
            )

    model.output("display.default", "display", item_keys)
    model.output("export.manufacturing", "export", item_keys)
    model.table(
        "parts", "钢楼梯零件清单",
        columns=[
            {"key": "partNumber", "displayName": "零件编号", "valueType": "string"},
            {"key": "name", "displayName": "名称", "valueType": "string"},
            {"key": "category", "displayName": "种类", "valueType": "string"},
            {"key": "quantity", "displayName": "数量", "valueType": "integer"},
            {"key": "length", "displayName": "长度", "valueType": "number", "unit": "mm"},
        ],
        rows=rows,
    )
    rise = _number(parameters, "floorHeight") / _integer(parameters, "totalRiserCount")
    if rise > 175.0:
        model.diagnostic(
            "warning", "stair.riser-height",
            f"当前踏步高度为 {rise:.1f}mm，高于住宅公共楼梯常用上限175mm。",
            parameter_keys=["floorHeight", "totalRiserCount"],
        )
    if _number(parameters, "treadDepth") < 260.0:
        model.diagnostic(
            "warning", "stair.tread-depth",
            "当前踏步宽度小于住宅公共楼梯常用下限260mm。",
            parameter_keys=["treadDepth"],
        )
    return finish_geometry_request(model, context)
