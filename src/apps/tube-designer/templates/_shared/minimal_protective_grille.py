"""Reusable topology, layout, joint, hole and BOM rules for minimal tube grilles.

The product template owns the assembly topology.  This shared module owns the
manufacturing recipes so the same half-hole joint and face-hole pattern can be
reused by future railings, racks and frames without naming a product family.
"""
from __future__ import annotations

from copy import deepcopy
from dataclasses import dataclass, field
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import sys
from typing import Any

from icax_template_sdk import NeutralModel


TEMPLATE_ID = "minimal-protective-grille"
TEMPLATE_VERSION = "1.0.0"
EPS = 1.0e-7


def _load_shared(filename: str):
    path = Path(__file__).resolve().with_name(filename)
    name = "icax_minimal_grille_" + path.stem + "_" + hashlib.sha256(path.read_bytes()).hexdigest()[:16]
    if name not in sys.modules:
        spec = importlib.util.spec_from_file_location(name, path)
        if spec is None or spec.loader is None:
            raise RuntimeError(f"无法加载共享规则：{filename}")
        module = importlib.util.module_from_spec(spec)
        sys.modules[name] = module
        spec.loader.exec_module(module)
    return sys.modules[name]


_catalog = _load_shared("tube_profile_catalog.py")
_frames = _load_shared("security_window_frame_geometry.py")
_shared_geometry = _load_shared("shared_tube_geometry.py")
Profile = _catalog.Profile
Part = _frames.Part
SharedTubeGeometry = _shared_geometry.SharedTubeGeometry
finish_geometry_request = _shared_geometry.finish_geometry_request
request_geometry_purpose = _shared_geometry.request_geometry_purpose


def _number(parameters: dict[str, Any], key: str, low: float | None = None,
            high: float | None = None) -> float:
    value = parameters.get(key)
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError(f"{key} 必须是有限数值")
    result = float(value)
    if low is not None and result < low - EPS:
        raise ValueError(f"{key} 不能小于 {low:g}")
    if high is not None and result > high + EPS:
        raise ValueError(f"{key} 不能大于 {high:g}")
    return result


def _integer(parameters: dict[str, Any], key: str, low: int = 0, high: int = 1000) -> int:
    value = _number(parameters, key, low, high)
    if value != int(value):
        raise ValueError(f"{key} 必须是整数")
    return int(value)


def _choice(parameters: dict[str, Any], key: str, values: tuple[str, ...]) -> str:
    value = parameters.get(key)
    if value not in values:
        raise ValueError(f"{key} 选项无效")
    return str(value)


def _boolean(parameters: dict[str, Any], key: str) -> bool:
    value = parameters.get(key)
    if not isinstance(value, bool):
        raise ValueError(f"{key} 必须是布尔值")
    return value


@dataclass(frozen=True)
class OrientedProfile:
    """A profile view rotated around the member axis, without changing its source."""
    source: Any
    quarter_turn: bool

    def __getattr__(self, key: str) -> Any:
        return getattr(self.source, key)

    @property
    def width(self) -> float:
        return self.source.depth if self.quarter_turn else self.source.width

    @property
    def depth(self) -> float:
        return self.source.width if self.quarter_turn else self.source.depth

    @property
    def specification(self) -> str:
        suffix = "（截面旋转 90°）" if self.quarter_turn else ""
        return self.source.specification + suffix

    @property
    def hollow(self) -> bool:
        return self.source.hollow

    def contours(self, *, clearance: float = 0.0, swap_axes: bool = False) -> list[dict[str, Any]]:
        return self.source.contours(
            clearance=clearance, swap_axes=bool(swap_axes) ^ self.quarter_turn)

    def properties(self) -> dict[str, Any]:
        result = self.source.properties()
        result.update({
            "width":self.width,"depth":self.depth,"specification":self.specification,
            "contours":self.contours(),
            "sectionOrientation":"rotate_90" if self.quarter_turn else "as_defined",
        })
        return result


def _load_profile(parameters: dict[str, Any], role: str) -> Any:
    source = _catalog.load_profile(parameters, role)
    orientation = _choice(
        parameters, f"{role}ProfileOrientation", ("as_defined", "rotate_90"))
    profile = OrientedProfile(source, orientation == "rotate_90")
    if not profile.hollow:
        raise ValueError(f"{role} 必须选择具有内轮廓的管型")
    return profile


def _rectangular_tube(profile: Profile) -> bool:
    """Classify a two-loop rectangular tube from section semantics and contours."""
    contours = profile.contours()
    if len(contours) != 2:
        return False
    # Parametric and fitted profile packages expose geometric sectionKind as
    # Profile.kind.  This is intentionally not a package-ID allow-list.
    if profile.kind == "rect":
        return True
    # A frozen/imported section can still prove the capability directly.
    return all(contour.get("kind") == "roundedRectangle" for contour in contours)


@dataclass(frozen=True)
class LinearLayout:
    centers: tuple[float, ...]
    actual_max_clear_gap: float
    removed_centers: tuple[float, ...] = ()
    intervals: tuple[tuple[float, float], ...] = ()


def _interval_centers(low: float, high: float, size: float, mode: str, *,
                      count: int, maximum_gap: float, pitch: float,
                      alignment: str) -> tuple[list[float], float]:
    available = high - low
    if available < size - EPS:
        return [], max(0.0, available)
    if mode == "fixed_count":
        if count == 0:
            return [], available
        gap = (available - count * size) / (count + 1)
        if gap < -EPS:
            raise ValueError("固定内杆根数超过当前可布置高度")
        gap = max(0.0, gap)
        return [low + gap + size / 2 + index * (size + gap) for index in range(count)], gap
    if mode == "maximum_gap":
        count = max(0, math.ceil((available - maximum_gap) / (size + maximum_gap) - EPS))
        gap = (available - count * size) / (count + 1)
        return [low + gap + size / 2 + index * (size + gap) for index in range(count)], gap
    count = max(1, math.floor((available - size) / pitch + EPS) + 1)
    occupied = size + (count - 1) * pitch
    residual = available - occupied
    start = low + size / 2
    if alignment == "center":
        start += residual / 2
    elif alignment == "top":
        start += residual
    return [start + index * pitch for index in range(count)], max(
        start - size / 2 - low, high - (start + (count - 1) * pitch + size / 2), pitch - size if count > 1 else 0.0)


def solve_linear_layout(low: float, high: float, size: float, parameters: dict[str, Any],
                        forbidden: tuple[float, float] | None) -> LinearLayout:
    """Solve fixed count, maximum clear gap or fixed pitch without manual coordinates."""
    if high - low <= size + EPS:
        raise ValueError("边框内净高度不足以布置内杆")
    mode = _choice(parameters, "barLayoutMode", ("fixed_count", "maximum_gap", "fixed_pitch"))
    count = _integer(parameters, "barCount", 0, 500) if mode == "fixed_count" else 0
    maximum_gap = _number(parameters, "maximumClearGap", 1.0, 2000.0) if mode == "maximum_gap" else 1.0
    pitch = _number(parameters, "barPitch", size + EPS, 2000.0) if mode == "fixed_pitch" else size
    alignment = _choice(parameters, "pitchAlignment", ("bottom", "center", "top")) if mode == "fixed_pitch" else "center"
    distribution = _choice(parameters, "handleDistribution", ("uniform_remove", "segment_equal"))
    if forbidden is None:
        centers, gap = _interval_centers(low, high, size, mode, count=count,
                                         maximum_gap=maximum_gap, pitch=pitch, alignment=alignment)
        return LinearLayout(tuple(centers), gap, intervals=((low, high),))

    forbidden_low = max(low, forbidden[0])
    forbidden_high = min(high, forbidden[1])
    if forbidden_high <= forbidden_low + EPS:
        centers, gap = _interval_centers(low, high, size, mode, count=count,
                                         maximum_gap=maximum_gap, pitch=pitch, alignment=alignment)
        return LinearLayout(tuple(centers), gap, intervals=((low, high),))
    if forbidden_low <= low + EPS and forbidden_high >= high - EPS:
        raise ValueError("把手禁区占满了全部可布置高度")
    if distribution == "uniform_remove":
        centers, gap = _interval_centers(low, high, size, mode, count=count,
                                         maximum_gap=maximum_gap, pitch=pitch, alignment=alignment)
        kept = [center for center in centers
                if center + size / 2 <= forbidden_low + EPS
                or center - size / 2 >= forbidden_high - EPS]
        removed = [center for center in centers if center not in kept]
        if not kept:
            raise ValueError("把手禁区移除全部内杆，请调整排布或禁区")
        return LinearLayout(tuple(kept), gap, tuple(removed), ((low, high),))

    intervals = [(a, b) for a, b in ((low, forbidden_low), (forbidden_high, high))
                 if b - a >= size - EPS]
    if not intervals:
        raise ValueError("把手禁区两侧均不足以布置内杆")
    counts = [0] * len(intervals)
    if mode == "fixed_count":
        capacities = [max(0, math.floor((b - a + EPS) / size)) for a, b in intervals]
        if count > sum(capacities):
            raise ValueError("固定内杆根数超过把手禁区两侧容量")
        weights = [b - a for a, b in intervals]
        for _ in range(count):
            candidates = [index for index in range(len(intervals)) if counts[index] < capacities[index]]
            target = max(candidates, key=lambda index: weights[index] / (counts[index] + 1))
            counts[target] += 1
    centers: list[float] = []
    gaps: list[float] = []
    for index, (a, b) in enumerate(intervals):
        current, gap = _interval_centers(a, b, size, mode, count=counts[index],
                                         maximum_gap=maximum_gap, pitch=pitch, alignment=alignment)
        centers.extend(current)
        gaps.append(gap)
    if not centers:
        raise ValueError("当前排布没有生成任何内杆")
    return LinearLayout(tuple(sorted(centers)), max(gaps, default=0.0), intervals=tuple(intervals))


@dataclass
class ProductPart:
    part: Part
    role: str
    material: str
    features: dict[str, Any] = field(default_factory=dict)


def _frame_parts(width: float, height: float, profile: Profile, frame_type: str,
                 corner: str) -> list[ProductPart]:
    half = profile.width / 2
    cut = "miter-45" if corner == "miter_45" else "square"
    parts = [
        ProductPart(Part("frame.left.0001", "左边框", (half, 0, 0), (half, 0, height), profile,
                         "frame", "frame.vertical", "边框竖管", cut, cut), "frame.left", ""),
        ProductPart(Part("frame.right.0001", "右边框", (width-half, 0, 0), (width-half, 0, height), profile,
                         "frame", "frame.vertical", "边框竖管", cut, cut), "frame.right", ""),
    ]
    if frame_type == "two_vertical":
        return parts
    if corner == "horizontal_wrap":
        vertical_low, vertical_high = profile.width, height - profile.width
        parts[0].part = Part("frame.left.0001", "左边框", (half, 0, vertical_low), (half, 0, vertical_high), profile,
                             "frame", "frame.vertical", "边框竖管")
        parts[1].part = Part("frame.right.0001", "右边框", (width-half, 0, vertical_low), (width-half, 0, vertical_high), profile,
                             "frame", "frame.vertical", "边框竖管")
        horizontal_left, horizontal_right = 0.0, width
    elif corner == "vertical_wrap":
        horizontal_left, horizontal_right = profile.width, width - profile.width
    else:
        horizontal_left, horizontal_right = 0.0, width
    parts.extend([
        ProductPart(Part("frame.bottom.0001", "下边框", (horizontal_left, 0, half),
                         (horizontal_right, 0, half), profile, "frame", "frame.horizontal", "边框横管", cut, cut),
                    "frame.bottom", ""),
        ProductPart(Part("frame.top.0001", "上边框", (horizontal_left, 0, height-half),
                         (horizontal_right, 0, height-half), profile, "frame", "frame.horizontal", "边框横管", cut, cut),
                    "frame.top", ""),
    ])
    return parts


def _handle_interval(parameters: dict[str, Any], low: float, high: float) -> tuple[float, float] | None:
    if not _boolean(parameters, "handleEnabled"):
        return None
    reference = _choice(parameters, "handleReference", ("bottom", "center", "top"))
    reference_y = _number(parameters, "handleReferenceY", 0.0, high)
    height = _number(parameters, "handleHeight", 1.0, high-low)
    bottom = reference_y if reference == "bottom" else (
        reference_y-height if reference == "top" else reference_y-height/2)
    top = bottom+height
    bottom -= _number(parameters, "handleKeepoutBottom", 0.0, high-low)
    top += _number(parameters, "handleKeepoutTop", 0.0, high-low)
    if bottom <= low + EPS or top >= high - EPS:
        raise ValueError("把手禁区必须完整位于边框可用高度内")
    return bottom, top


def _head_contour(left: float, right: float, width: float, corner: str, size: float,
                  tip: str, center_z: float) -> dict[str, Any]:
    length = right - left
    if length <= EPS or width <= EPS:
        raise ValueError("公头尺寸必须大于0")
    if corner == "round":
        radius = min(size, length / 2 - EPS, width / 2 - EPS)
        if radius <= 0:
            raise ValueError("公头圆角尺寸无效")
        return {"kind": "roundedRectangle", "width": length, "height": width,
                "radius": radius, "center": [(left+right)/2, center_z]}
    half = width / 2
    if corner == "chamfer":
        leg = min(size, length-EPS, half-EPS)
        if leg <= 0:
            raise ValueError("公头倒角尺寸无效")
        if tip == "left":
            points = [[left, center_z-half+leg], [left+leg, center_z-half],
                      [right, center_z-half], [right, center_z+half],
                      [left+leg, center_z+half], [left, center_z+half-leg]]
        else:
            points = [[left, center_z-half], [right-leg, center_z-half],
                      [right, center_z-half+leg], [right, center_z+half-leg],
                      [right-leg, center_z+half], [left, center_z+half]]
        return {"kind": "polygon", "points": points}
    return {"kind": "polygon", "points": [
        [left,center_z-half],[right,center_z-half],
        [right,center_z+half],[left,center_z+half]]}


def _xz_prism(model: NeutralModel, key: str, contour: dict[str, Any], depth: float) -> str:
    contour = deepcopy(contour)
    center = contour.pop("center", [0.0, 0.0])
    if (not isinstance(center, list) or len(center) != 2
            or any(isinstance(value, bool) or not isinstance(value, (int, float))
                   or not math.isfinite(value) for value in center)):
        raise ValueError("二维加工包络中心必须包含两个有限坐标")
    profile = model.geometry(f"{key}.profile", "profile2d", arguments={
        "placement": {"origin": [float(center[0]), -depth/2, float(center[1])],
                      "xAxis": [1.0,0.0,0.0], "yAxis": [0.0,0.0,1.0]},
        "contours": [contour],
    })
    return model.geometry(f"{key}.solid", "extrude", inputs=[profile], arguments={"vector": [0.0,depth,0.0]})


def emit_male_head(model: NeutralModel, raw: str, key: str, visible_left: float, visible_right: float,
                   center_z: float, profile: Profile, length: float, width: float,
                   corner: str, size: float) -> str:
    """Keep the full middle stock and machine a centred head at both ends."""
    depth = profile.depth + max(4.0, profile.wall * 4)
    body = _xz_prism(model, f"{key}.male.body", {"kind":"polygon", "points":[
        [visible_left-EPS,center_z-profile.width], [visible_right+EPS,center_z-profile.width],
        [visible_right+EPS,center_z+profile.width], [visible_left-EPS,center_z+profile.width],
    ]}, depth)
    left = _xz_prism(model, f"{key}.male.left",
                     _head_contour(visible_left-length, visible_left+EPS, width,
                                   corner, size, "left", center_z), depth)
    right_contour = _head_contour(
        visible_right-EPS, visible_right+length, width, corner, size, "right", center_z)
    right = _xz_prism(model, f"{key}.male.right", right_contour, depth)
    envelope = model.geometry(f"{key}.male.envelope", "boolean", inputs=[body,left,right],
                              arguments={"operation":"union"})
    return model.geometry(f"{key}.male.finished", "boolean", inputs=[raw,envelope],
                          arguments={"operation":"intersect", "target":raw, "tools":[envelope]})


def emit_half_hole(model: NeutralModel, key: str, *, x: float, z: float, wall: float,
                   hole_depth: float, hole_height: float, radius: float = 0.0) -> str:
    contour = {"kind":"roundedRectangle", "width":hole_depth, "height":hole_height,
               "radius":min(radius, hole_depth/2-EPS, hole_height/2-EPS)}
    profile = model.geometry(f"{key}.profile", "profile2d", arguments={
        "placement":{"origin":[x-EPS,0.0,z], "xAxis":[0.0,1.0,0.0], "yAxis":[0.0,0.0,1.0]},
        "contours":[contour],
    })
    return model.geometry(f"{key}.solid", "extrude", inputs=[profile],
                          arguments={"vector":[wall+2*EPS,0.0,0.0]})


def emit_round_wall_hole(model: NeutralModel, key: str, *, origin: tuple[float,float,float],
                         x_axis: tuple[float,float,float], y_axis: tuple[float,float,float],
                         vector: tuple[float,float,float], diameter: float) -> str:
    profile = model.geometry(f"{key}.profile", "profile2d", arguments={
        "placement":{"origin":list(origin), "xAxis":list(x_axis), "yAxis":list(y_axis)},
        "contours":[{"kind":"circle", "radius":diameter/2}],
    })
    return model.geometry(f"{key}.solid", "extrude", inputs=[profile], arguments={"vector":list(vector)})


def _hole_positions(parameters: dict[str, Any], height: float, radius: float,
                    blocked: list[tuple[float,float,float]], endpoint_margin: float = 0.0
                    ) -> tuple[list[float], list[dict[str,float]]]:
    count = _integer(parameters, "installHoleCountPerSide", 1, 50)
    bottom = _number(parameters, "installHoleBottomOffset", 0.0, height)
    top = height - _number(parameters, "installHoleTopOffset", 0.0, height)
    minimum_web = _number(parameters, "minimumFeatureWeb", 0.0, 100.0)
    low = endpoint_margin+radius+minimum_web
    high = height-endpoint_margin-radius-minimum_web
    if bottom < low-EPS or top > high+EPS or top < bottom-EPS:
        raise ValueError("安装孔首尾位置越过端部安全边距")
    initial = [bottom] if count == 1 else [bottom + index*(top-bottom)/(count-1) for index in range(count)]
    auto = _boolean(parameters, "installHoleAutoAvoid")
    maximum_shift = _number(parameters, "installHoleMaximumShift", 0.0, 1000.0) if auto else 0.0
    actual: list[float] = []
    adjustments: list[dict[str,float]] = []
    def legal(value: float) -> bool:
        return low-EPS <= value <= high+EPS and all(
            value+conflict_radius+minimum_web <= a+EPS
            or value-conflict_radius-minimum_web >= b-EPS
            for a,b,conflict_radius in blocked)
    for source in initial:
        target = source
        if not legal(target):
            if not auto:
                raise ValueError(f"安装孔 Z={source:g} mm 与内杆母孔或端部安全区冲突")
            found = None
            step = 0.5
            for index in range(1, int(maximum_shift/step)+1):
                for candidate in (source-index*step, source+index*step):
                    if legal(candidate) and all(abs(candidate-other) >= 2*radius+minimum_web-EPS for other in actual):
                        found = candidate
                        break
                if found is not None:
                    break
            if found is None:
                raise ValueError(f"安装孔 Z={source:g} mm 在允许偏移内无法避开母孔")
            target = found
            adjustments.append({"requested":source, "actual":target, "shift":target-source})
        if any(abs(target-other) < 2*radius+minimum_web-EPS for other in actual):
            raise ValueError("安装孔之间距离不足")
        actual.append(target)
    return actual, adjustments


def _layout(parameters: dict[str, Any]) -> dict[str, Any]:
    width = _number(parameters, "width", 100.0, 10000.0)
    height = _number(parameters, "height", 100.0, 10000.0)
    frame = _load_profile(parameters, "frame")
    inner = _load_profile(parameters, "inner")
    frame_type = _choice(parameters, "frameType", ("two_vertical", "closed_frame"))
    corner = (_choice(parameters, "frameCornerJoint", ("miter_45", "horizontal_wrap", "vertical_wrap"))
              if frame_type == "closed_frame" else "open")
    if width <= 2*frame.width+inner.width+EPS:
        raise ValueError("成品宽度不足以容纳两侧边框和内杆")
    if height <= (2*frame.width if frame_type == "closed_frame" else 0)+inner.width+EPS:
        raise ValueError("成品高度不足以布置边框和内杆")
    low = frame.width if frame_type == "closed_frame" else 0.0
    high = height-frame.width if frame_type == "closed_frame" else height
    handle = _handle_interval(parameters, low, high)
    linear = solve_linear_layout(low, high, inner.width, parameters, handle)
    joint = _choice(parameters, "innerJoint", ("male_female_half_hole", "flat_weld"))
    head_length = _number(parameters, "maleHeadLength", 0.1, 200.0) if joint == "male_female_half_hole" else 0.0
    head_width = _number(parameters, "maleHeadWidth", 0.1, inner.width) if joint == "male_female_half_hole" else inner.width
    corner_type = _choice(parameters, "maleCornerType", ("round", "chamfer", "square")) if joint == "male_female_half_hole" else "square"
    corner_size = _number(parameters, "maleCornerSize", 0.0, min(head_length,head_width)/2) if joint == "male_female_half_hole" and corner_type != "square" else 0.0
    clearance_depth = _number(parameters, "holeTotalClearanceDepth", 0.0, 10.0) if joint == "male_female_half_hole" else 0.0
    clearance_height = _number(parameters, "holeTotalClearanceHeight", 0.0, 10.0) if joint == "male_female_half_hole" else 0.0
    if joint == "male_female_half_hole":
        if not _rectangular_tube(frame) or not _rectangular_tube(inner):
            raise ValueError("公母管半孔当前要求接收管和插入管具有可判定的方矩管面")
        if not frame.wall+EPS < head_length < frame.width-frame.wall-EPS:
            raise ValueError("公头长度必须穿过进入侧壁并保留接收管对侧壁")
        hole_depth = inner.depth+clearance_depth
        hole_height = inner.width+clearance_height
        flat_face = frame.depth-2*frame.radius
        if hole_depth >= flat_face-EPS:
            raise ValueError("母孔深向尺寸超过边框进入侧的平直管面")
        if head_width <= inner.wall*2+EPS:
            raise ValueError("公头宽度过小，剩余壁料不足")
    else:
        hole_depth, hole_height = 0.0, 0.0
        _frames._validate_flat_weld(frame, inner, "内杆与边框")

    visible_left, visible_right = frame.width, width-frame.width
    parts = _frame_parts(width, height, frame, frame_type, corner)
    for item in parts:
        item.material = str(parameters["frameMaterial"])
    for index, z in enumerate(linear.centers, start=1):
        start = visible_left-head_length if joint == "male_female_half_hole" else visible_left
        end = visible_right+head_length if joint == "male_female_half_hole" else visible_right
        features = {"layoutIndex":index, "centerHeight":z, "joint":joint}
        if joint == "male_female_half_hole":
            features["maleHeads"] = {"length":head_length, "width":head_width,
                "cornerType":corner_type, "cornerSize":corner_size}
        parts.append(ProductPart(Part(f"inner.bar.{index:04d}", f"内横杆 {index}",
            (start,0,z),(end,0,z),inner,"inner","inner.bar","内横杆"),
            "inner.bar",str(parameters["innerMaterial"]),features))

    install_enabled = _boolean(parameters, "installHoleEnabled")
    installation: dict[str, Any] = {
        "enabled":install_enabled,"positionsBySide":{"left":[],"right":[]},
        "adjustmentsBySide":{"left":[],"right":[]}}
    if install_enabled:
        orientation = _choice(parameters, "installHoleOrientation", ("front", "side"))
        large = _number(parameters, "installHoleLargeDiameter", 0.1, 200.0)
        small = _number(parameters, "installHoleSmallDiameter", 0.1, large)
        available = frame.width-2*frame.radius if orientation == "front" else frame.depth-2*frame.radius
        if large >= available-EPS:
            raise ValueError("安装大孔超过所选边框管面的平直宽度")
        shared_blocked: list[tuple[float,float,float]] = []
        # A side-mounted inner small hole shares the receiver's inner side wall
        # with a half-hole or welded branch. Front/back holes are on orthogonal
        # walls and must not be rejected merely because their Z values match.
        if orientation == "side":
            occupied_height = hole_height if joint == "male_female_half_hole" else inner.width
            shared_blocked = [(z-occupied_height/2,z+occupied_height/2,small/2)
                              for z in linear.centers]
        endpoint_margin = frame.width if frame_type == "closed_frame" else 0.0
        installation.update({"orientation":orientation,"largeDiameter":large,"smallDiameter":small})
        for side in ("left","right"):
            blocked = list(shared_blocked)
            if handle is not None and parameters["handleSide"] == side:
                blocked.append((handle[0],handle[1],large/2))
            positions, adjustments = _hole_positions(
                parameters,height,large/2,blocked,endpoint_margin)
            installation["positionsBySide"][side] = positions
            installation["adjustmentsBySide"][side] = adjustments
        for item in parts[:2]:
            side = "left" if item.role == "frame.left" else "right"
            item.features["installationHoles"] = {
                "enabled":True,"orientation":orientation,"largeDiameter":large,
                "smallDiameter":small,"positions":deepcopy(installation["positionsBySide"][side])}
    return {"width":width,"height":height,"frame":frame,"inner":inner,"frameType":frame_type,
            "corner":corner,"linear":linear,"handle":handle,"joint":joint,
            "headLength":head_length,"headWidth":head_width,"headCorner":corner_type,
            "headCornerSize":corner_size,"holeDepth":hole_depth,"holeHeight":hole_height,
            "visibleLeft":visible_left,"visibleRight":visible_right,"parts":parts,
            "installation":installation}


def _signature(part: ProductPart) -> str:
    relevant = deepcopy(part.features)
    relevant.pop("layoutIndex", None)
    relevant.pop("centerHeight", None)
    if part.role.startswith("frame.") and relevant.get("installationHoles",{}).get("orientation") == "front":
        role = "frame.vertical"
    else:
        role = part.role
    return json.dumps({"role":role,"profile":part.part.profile.properties(),"material":part.material,
                       "length":round(part.part.length,6),"cuts":[part.part.start_cut,part.part.end_cut],
                       "features":relevant},sort_keys=True,ensure_ascii=True,separators=(",",":"))


def _emit_install_holes(model: NeutralModel, state: dict[str, Any], item: ProductPart) -> list[str]:
    installation = state["installation"]
    if not installation["enabled"] or item.role not in {"frame.left", "frame.right"}:
        return []
    left = item.role == "frame.left"
    side = "left" if left else "right"
    center_x = state["frame"].width/2 if left else state["width"]-state["frame"].width/2
    frame = state["frame"]
    tools: list[str] = []
    for index,z in enumerate(installation["positionsBySide"][side], start=1):
        prefix = f"{item.part.key}.install.{index:04d}"
        if installation["orientation"] == "front":
            tools.append(emit_round_wall_hole(model,prefix+".large",
                origin=(center_x,-frame.depth/2-EPS,z),x_axis=(1,0,0),y_axis=(0,0,1),
                vector=(0,frame.wall+2*EPS,0),diameter=installation["largeDiameter"]))
            tools.append(emit_round_wall_hole(model,prefix+".small",
                origin=(center_x,frame.depth/2-frame.wall-EPS,z),x_axis=(1,0,0),y_axis=(0,0,1),
                vector=(0,frame.wall+2*EPS,0),diameter=installation["smallDiameter"]))
        else:
            outer = -EPS if left else state["width"]-frame.wall-EPS
            inner = frame.width-frame.wall-EPS if left else state["width"]-frame.width-EPS
            tools.append(emit_round_wall_hole(model,prefix+".large",origin=(outer,0,z),
                x_axis=(0,1,0),y_axis=(0,0,1),vector=(frame.wall+2*EPS,0,0),
                diameter=installation["largeDiameter"]))
            tools.append(emit_round_wall_hole(model,prefix+".small",origin=(inner,0,z),
                x_axis=(0,1,0),y_axis=(0,0,1),vector=(frame.wall+2*EPS,0,0),
                diameter=installation["smallDiameter"]))
    return tools


def _emit_half_holes(model: NeutralModel, state: dict[str, Any], item: ProductPart) -> list[str]:
    if state["joint"] != "male_female_half_hole" or item.role not in {"frame.left","frame.right"}:
        return []
    left = item.role == "frame.left"
    x = state["frame"].width-state["frame"].wall if left else state["width"]-state["frame"].width
    radius = max(0.0, state["inner"].radius + min(
        state["holeDepth"]-state["inner"].depth,
        state["holeHeight"]-state["inner"].width)/2)
    return [emit_half_hole(model,f"{item.part.key}.half-hole.{index:04d}",x=x,z=z,
        wall=state["frame"].wall,hole_depth=state["holeDepth"],hole_height=state["holeHeight"],radius=radius)
        for index,z in enumerate(state["linear"].centers,start=1)]


def generate(parameters: dict[str, Any], context: dict[str, Any]) -> dict[str, Any]:
    state = _layout(parameters)
    purpose = request_geometry_purpose(context)
    model = NeutralModel(template_id=TEMPLATE_ID,template_version=TEMPLATE_VERSION,
        package_digest=str(context.get("template",{}).get("packageDigest","")),parameters=deepcopy(parameters))
    geometry = SharedTubeGeometry(model)
    item_keys: list[str] = []
    groups: dict[str,dict[str,Any]] = {}
    bar_items = [item for item in state["parts"] if item.role == "inner.bar"]
    for item in state["parts"]:
        part = item.part
        raw = _frames._emit_tube_geometry(model,part,geometry)[1]
        display = raw
        export = raw
        cutters: list[str] = []
        if purpose != "display":
            cutters.extend(_emit_half_holes(model,state,item))
            cutters.extend(_emit_install_holes(model,state,item))
            if state["corner"] == "miter_45" and item.role.startswith("frame."):
                cutters.extend(_frames._emit_miter_cutters(model,part))
            if cutters:
                export = model.geometry(f"{part.key}.manufacturing.cut", "boolean", inputs=[export,*cutters],
                    arguments={"operation":"subtract","target":export,"tools":cutters})
            if item.role == "inner.bar" and state["joint"] == "male_female_half_hole":
                export = emit_male_head(model,export,part.key,state["visibleLeft"],state["visibleRight"],
                    part.start[2],state["inner"],state["headLength"],state["headWidth"],
                    state["headCorner"],state["headCornerSize"])
        signature = _signature(item)
        group = groups.setdefault(signature,{"key":f"bom.{len(groups)+1:04d}","members":[],"values":{
            "partNumber":f"{parameters['productCode']}-{len(groups)+1:03d}","name":part.category_name or part.name,
            "quantity":0,"length":round(part.length,3),"profile":part.profile.specification,
            "material":item.material,"features":""}})
        group["members"].append(part.key)
        group["values"]["quantity"] += 1
        feature_names = []
        if item.features.get("maleHeads"): feature_names.append("双端公头")
        if item.role in {"frame.left","frame.right"} and state["joint"] == "male_female_half_hole": feature_names.append(f"单面母孔×{len(bar_items)}")
        if item.features.get("installationHoles"):
            feature_names.append(f"大小安装孔×{len(item.features['installationHoles']['positions'])}")
        if state["corner"] == "miter_45" and item.role.startswith("frame."): feature_names.append("45°端切")
        group["values"]["features"] = "、".join(feature_names) or "平口"
        properties = {"partNumber":group["values"]["partNumber"],"quantity":1,
            "length":round(part.length,3),"manufacturing.partKind":"tube",
            "manufacturing.materialCategory":"tube","manufacturing.materialGrade":item.material,
            "manufacturing.sourcing":"made","manufacturing.categoryKey":part.category_key,
            "manufacturing.categoryName":part.category_name or part.name,
            "tubeDesigner.profile":part.profile.properties(),
            "tubeDesigner.endProcess":{"startCut":part.start_cut,"endCut":part.end_cut,"lengthBasis":"blank_axial_extent"},
            "tubeDesigner.productRule":{"product":"minimal-protective-grille","role":item.role,**deepcopy(item.features)}}
        item_keys.append(model.item(part.key,part.name,representations={"display":display,"export":export},properties=properties))

    for index,bar in enumerate(bar_items,start=1):
        for side in ("left","right"):
            model.relationship(f"joint.{index:04d}.{side}",
                "assembly" if state["joint"] == "male_female_half_hole" else "weld",
                [bar.part.key,f"frame.{side}.0001"],properties={
                    "recipe":state["joint"],"participantRoles":["inserted","receiver"],
                    "halfHole":state["joint"] == "male_female_half_hole",
                    "insertionDepth":state["headLength"],"totalClearanceDepth":state["holeDepth"]-state["inner"].depth,
                    "totalClearanceHeight":state["holeHeight"]-state["inner"].width})
    if state["frameType"] == "closed_frame":
        for index,(a,b) in enumerate((("frame.left.0001","frame.bottom.0001"),("frame.left.0001","frame.top.0001"),
                                     ("frame.right.0001","frame.bottom.0001"),("frame.right.0001","frame.top.0001")),start=1):
            model.relationship(f"frame.corner.{index:04d}","weld",[a,b],properties={"recipe":state["corner"]})
    model.output("display.default","display",item_keys)
    model.output("export.manufacturing","export",item_keys)
    model.table("parts","防护网零件清单",columns=[
        {"key":"partNumber","displayName":"零件编号","valueType":"string"},
        {"key":"name","displayName":"名称","valueType":"string"},
        {"key":"profile","displayName":"管型","valueType":"string"},
        {"key":"material","displayName":"材质","valueType":"string"},
        {"key":"length","displayName":"毛坯长度","valueType":"number","unit":"mm"},
        {"key":"quantity","displayName":"数量","valueType":"integer"},
        {"key":"features","displayName":"加工特征","valueType":"string"}],
        rows=[{"key":group["key"],"itemKey":group["members"][0],"values":group["values"]} for group in groups.values()])
    for side, adjustments in state["installation"].get("adjustmentsBySide",{}).items():
        side_name = "左边框" if side == "left" else "右边框"
        for adjustment in adjustments:
            model.diagnostic("warning","protective-grille.install-hole-auto-avoid",
                f"{side_name}安装孔由 {adjustment['requested']:g} mm 避让到 {adjustment['actual']:g} mm",
                parameter_keys=["installHoleMaximumShift"])
    document = finish_geometry_request(model,context)
    document.setdefault("extensions",{})["protectiveGrille"] = {
        "schemaVersion":1,"frameType":state["frameType"],"cornerRecipe":state["corner"],
        "jointRecipe":state["joint"],"barCenters":list(state["linear"].centers),
        "removedBarCenters":list(state["linear"].removed_centers),
        "actualMaximumClearGap":state["linear"].actual_max_clear_gap,
        "layoutIntervals":[list(value) for value in state["linear"].intervals],
        "handleKeepout":list(state["handle"]) if state["handle"] else None,
        "handle":({"side":parameters["handleSide"],"reference":parameters["handleReference"],
                   "referenceY":parameters["handleReferenceY"],"width":parameters["handleWidth"],
                   "height":parameters["handleHeight"]} if parameters["handleEnabled"] else None),
        "installation":deepcopy(state["installation"]),
        "partGroups":[group["members"] for group in groups.values()],
    }
    return document


__all__ = ["OrientedProfile","LinearLayout","solve_linear_layout","emit_male_head","emit_half_hole",
           "emit_round_wall_hole","generate"]
