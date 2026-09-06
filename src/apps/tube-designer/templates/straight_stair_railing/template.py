from __future__ import annotations

from dataclasses import dataclass
import hashlib
import importlib.util
import math
from pathlib import Path
import sys
from typing import Any

from icax_template_sdk import NeutralModel


TEMPLATE_ID = "straight-stair-railing"
TEMPLATE_VERSION = "1.1.0"

Point = tuple[float, float, float]
Vector = tuple[float, float, float]

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
    group: str
    category_key: str
    category_name: str

    @property
    def length(self) -> float:
        return math.dist(self.start, self.end)


@dataclass(frozen=True)
class VerticalBay:
    left_face: float
    right_face: float
    positions: tuple[float, ...]
    clear_gap: float


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


def _subtract(left: Point, right: Point) -> Vector:
    return tuple(left[index] - right[index] for index in range(3))  # type: ignore[return-value]


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
        f"{part.key}.profile",
        "profile2d",
        arguments=_profile_arguments(part),
    )
    return model.geometry(
        f"{part.key}.solid",
        "extrude",
        inputs=[profile],
        arguments={"vector": vector},
    )


def _profile_properties(profile: Profile) -> dict[str, Any]:
    return profile.properties()


def _validate(parameters: dict[str, Any], profiles: tuple[Profile, Profile, Profile]) -> None:
    run = _number(parameters, "flightRun")
    rise = _number(parameters, "flightRise")
    height = _number(parameters, "railingHeight")
    if run <= 0:
        raise ValueError("水平投影长度必须大于0")
    if rise < 0:
        raise ValueError("垂直提升高度不能小于0")
    if height <= 0:
        raise ValueError("护栏高度必须大于0")
    if not 2 <= _integer(parameters, "postCount") <= 50:
        raise ValueError("立柱数量必须在2到50之间")
    # Vertical profiles are emitted with swap_axes=True: their actual X extent
    # is depth, not width. Keep this orientation for all existing profile types.
    if run / (_integer(parameters, "postCount") - 1) <= profiles[1].depth:
        raise ValueError("立柱间距不足，立柱截面会重叠")
    if _number(parameters, "startExtension") < 0 or _number(parameters, "endExtension") < 0:
        raise ValueError("扶手沿坡延伸不能小于0")
    infill_type = str(parameters["infillType"])
    if infill_type not in {"vertical", "horizontal", "none"}:
        raise ValueError("栏杆形式不受支持")
    if infill_type == "vertical":
        bottom = _number(parameters, "bottomRailHeight")
        if bottom >= height - profiles[0].width:
            raise ValueError("下横杆距坡面过大，已没有竖杆安装空间")
        mode = str(parameters["verticalLayoutMode"])
        if mode not in {"maximum_clear_gap", "manual_count"}:
            raise ValueError("竖杆布置方式不受支持")
        if _number(parameters, "maximumVerticalClearGap") <= 0:
            raise ValueError("竖杆最大净间距必须大于0")
        if not 0 <= _integer(parameters, "verticalBarCount") <= 200:
            raise ValueError("竖杆总数量必须在0到200之间")
    if infill_type == "horizontal":
        if _integer(parameters, "horizontalRailCount") < 1:
            raise ValueError("横档数量至少为1")
        if _number(parameters, "horizontalBottomClearance") >= height - profiles[0].width:
            raise ValueError("最低横档距坡面过大，已没有横档安装空间")


def _vertical_bays(parameters: dict[str, Any], post: Profile, infill: Profile) -> list[VerticalBay]:
    """Distribute within real horizontal face-to-face spaces, not over posts."""
    maximum_gap = _number(parameters, "maximumVerticalClearGap")
    run = _number(parameters, "flightRun")
    bay_count = _integer(parameters, "postCount") - 1
    bay_pitch = run / bay_count
    clear_width = bay_pitch - post.depth
    bar_width = infill.depth
    if clear_width <= 0 or maximum_gap <= 0 or bar_width <= 0:
        raise ValueError("柱间净空、竖杆宽度和净间距必须大于0")
    if str(parameters["verticalLayoutMode"]) == "manual_count":
        total = _integer(parameters, "verticalBarCount")
        if not 0 <= total <= 200:
            raise ValueError("竖杆总数量必须在0到200之间")
        count, remainder = divmod(total, bay_count)
        counts = [count] * bay_count
        # When counts cannot be equal, fill the center bays first, with a stable
        # tie break. The user's manual count is the whole railing's total.
        centered = sorted(range(bay_count), key=lambda index: (abs(index - (bay_count - 1) / 2), index))
        for index in centered[:remainder]:
            counts[index] += 1
    else:
        count = max(0, math.ceil((clear_width - maximum_gap) / (maximum_gap + bar_width) - 1.0e-9))
        counts = [count] * bay_count
    if sum(counts) > 200:
        raise ValueError("按最大净间距计算出的竖杆数量超过200")
    result: list[VerticalBay] = []
    for index, count in enumerate(counts):
        left_face = index * bay_pitch + post.depth / 2
        right_face = (index + 1) * bay_pitch - post.depth / 2
        gap = (right_face - left_face - count * bar_width) / (count + 1)
        if gap <= 1.0e-7:
            raise ValueError(f"第{index + 1}跨竖杆数量过多，截面将与立柱或相邻竖杆相碰")
        positions = tuple(left_face + gap + bar_width / 2 + n * (bar_width + gap) for n in range(count))
        result.append(VerticalBay(left_face, right_face, positions, gap))
    return result


def _build_parts(parameters: dict[str, Any]) -> list[Part]:
    run = _number(parameters, "flightRun")
    rise = _number(parameters, "flightRise")
    height = _number(parameters, "railingHeight")
    handrail = _profile(parameters, "handrail")
    post = _profile(parameters, "post")
    infill = _profile(parameters, "infill")
    _validate(parameters, (handrail, post, infill))

    slope_length = math.hypot(run, rise)
    slope_direction = (run / slope_length, 0.0, rise / slope_length)
    slope_normal = (-rise / slope_length, 0.0, run / slope_length)
    sloped_profile_x = (0.0, 1.0, 0.0)
    sloped_profile_y = slope_normal
    vertical_profile_x = (1.0, 0.0, 0.0)
    vertical_profile_y = (0.0, 1.0, 0.0)
    slope_z = lambda x: rise * x / run

    parts: list[Part] = []
    handrail_start_x = -_number(parameters, "startExtension")
    handrail_end_x = run + _number(parameters, "endExtension")
    parts.append(Part(
        "handrail.0001",
        "斜扶手",
        (handrail_start_x, 0.0, slope_z(handrail_start_x) + height),
        (handrail_end_x, 0.0, slope_z(handrail_end_x) + height),
        handrail,
        sloped_profile_x,
        sloped_profile_y,
        "stair_railing",
        "stair.handrail",
        "扶手管",
    ))

    post_count = _integer(parameters, "postCount")
    post_positions = [run * index / (post_count - 1) for index in range(post_count)]
    for index, x in enumerate(post_positions, start=1):
        base_z = slope_z(x)
        parts.append(Part(
            f"post.{index:04d}",
            f"立柱 {index}",
            (x, 0.0, base_z),
            (x, 0.0, base_z + height),
            post,
            vertical_profile_x,
            vertical_profile_y,
            "stair_railing",
            "stair.post",
            "立柱",
        ))

    infill_type = str(parameters["infillType"])
    if infill_type == "vertical":
        bottom_height = _number(parameters, "bottomRailHeight")
        bottom_rail_key = "infill.bottom_rail.0001"
        parts.append(Part(
            bottom_rail_key,
            "下横杆",
            (0.0, 0.0, bottom_height),
            (run, 0.0, rise + bottom_height),
            infill,
            sloped_profile_x,
            sloped_profile_y,
            "stair_railing",
            "stair.bottom_rail",
            "下横杆",
        ))
        positions = [x for bay in _vertical_bays(parameters, post, infill) for x in bay.positions]
        for index, x in enumerate(positions):
            base_z = slope_z(x) + bottom_height
            top_z = slope_z(x) + height
            parts.append(Part(
                f"infill.vertical.{index + 1:04d}",
                f"竖杆 {index + 1}",
                (x, 0.0, base_z),
                (x, 0.0, top_z),
                infill,
                vertical_profile_x,
                vertical_profile_y,
                "stair_railing",
                "stair.vertical_infill",
                "竖杆",
            ))
    elif infill_type == "horizontal":
        count = _integer(parameters, "horizontalRailCount")
        bottom = _number(parameters, "horizontalBottomClearance")
        top = height - max(handrail.width, infill.width)
        for index in range(count):
            offset = bottom if count == 1 else bottom + (top - bottom) * index / (count - 1)
            parts.append(Part(
                f"infill.horizontal.{index + 1:04d}",
                f"横档 {index + 1}",
                (0.0, 0.0, offset),
                (run, 0.0, rise + offset),
                infill,
                sloped_profile_x,
                sloped_profile_y,
                "stair_railing",
                "stair.horizontal_infill",
                "横档",
            ))
    return parts


def generate(parameters: dict[str, Any], context: dict[str, Any]) -> dict[str, Any]:
    request_geometry_purpose(context)
    template = context["template"]
    model = NeutralModel(
        template_id=TEMPLATE_ID,
        template_version=TEMPLATE_VERSION,
        package_digest=str(template.get("packageDigest", "")),
        parameters=parameters,
    )
    parts = _build_parts(parameters)
    if (str(parameters["infillType"]) == "vertical"
            and str(parameters["verticalLayoutMode"]) == "manual_count"):
        bays = _vertical_bays(parameters, _profile(parameters, "post"), _profile(parameters, "infill"))
        actual_gap = max(bay.clear_gap for bay in bays)
        maximum_gap = _number(parameters, "maximumVerticalClearGap")
        if actual_gap > maximum_gap + 1.0e-7:
            model.diagnostic(
                "warning", "stair-railing.vertical-clear-gap",
                f"手动数量下实际最大竖杆净间距为{actual_gap:.2f} mm，超过设定的{maximum_gap:g} mm；请增加竖杆总数或改用最大净距自动布置。",
                parameter_keys=["verticalBarCount", "maximumVerticalClearGap"],
            )
    item_keys: list[str] = []
    rows: list[dict[str, Any]] = []
    for index, part in enumerate(parts, start=1):
        representation = _emit_tube(model, part)
        part_number = f"{parameters['productCode']}-{index:03d}"
        properties = {
            "partNumber": part_number,
            "quantity": 1,
            "group": part.group,
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
            part.key,
            part.name,
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
                "quantity": 1,
                "length": round(part.length, 3),
            },
        })

    handrail_key = "handrail.0001"
    for index in range(_integer(parameters, "postCount")):
        model.relationship(
            f"joint.handrail_post.{index + 1:04d}",
            str(parameters["connectionType"]),
            [handrail_key, f"post.{index + 1:04d}"],
            properties={"participantRoles": ["handrail", "post"]},
        )

    model.output("display.default", "display", item_keys)
    model.output("export.manufacturing", "export", item_keys)
    model.table(
        "parts",
        "楼梯护栏零件清单",
        columns=[
            {"key": "partNumber", "displayName": "零件编号", "valueType": "string"},
            {"key": "name", "displayName": "名称", "valueType": "string"},
            {"key": "quantity", "displayName": "数量", "valueType": "integer"},
            {"key": "length", "displayName": "长度", "valueType": "number", "unit": "mm"},
        ],
        rows=rows,
    )
    return finish_geometry_request(model, context)
