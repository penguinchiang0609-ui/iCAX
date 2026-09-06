"""One manufacturing model for straight/L/U, two/three-rail guardrails.

All plan segments are orthogonal. Rail dimensions use lateral width / vertical
depth; vertical infill uses along-bay width / lateral depth. Each bay is solved
between the actual post faces, never by distributing bars over the whole run.
"""
from __future__ import annotations

from dataclasses import dataclass, field
import hashlib
import importlib.util
import math
from pathlib import Path
import sys
from typing import Any

from icax_template_sdk import NeutralModel


def _shared(filename: str):
    path = Path(__file__).with_name(filename)
    name = "icax_guardrail_" + path.stem + "_" + hashlib.sha256(path.read_bytes()).hexdigest()[:16]
    if name not in sys.modules:
        spec = importlib.util.spec_from_file_location(name, path)
        if spec is None or spec.loader is None:
            raise RuntimeError(f"无法加载护栏共享模块：{filename}")
        module = importlib.util.module_from_spec(spec)
        sys.modules[name] = module
        spec.loader.exec_module(module)
    return sys.modules[name]


_catalog = _shared("tube_profile_catalog.py")
_geometry = _shared("shared_tube_geometry.py")
_plates = _shared("plate_geometry.py")
Point = tuple[float, float, float]
Z = (0.0, 0.0, 1.0)
EPS = 1.0e-7
MAX_PART_COUNT = 2000


@dataclass
class Tube:
    key: str
    name: str
    start: Point
    end: Point
    profile: Any
    x_axis: Point
    y_axis: Point
    category: str
    group: str
    clips: list[str] = field(default_factory=list)

    @property
    def length(self) -> float:
        return math.dist(self.start, self.end)


@dataclass
class Post:
    key: str
    point: Point
    profile: Any
    large: bool = False

    def half_extent(self, direction: Point) -> float:
        return (abs(direction[0]) * self.profile.width + abs(direction[1]) * self.profile.depth) / 2


@dataclass
class Bay:
    key: str
    direction: Point
    left: Post
    right: Post
    clear: float
    bar_centers: list[float]
    clear_gaps: list[float]


@dataclass
class Plate:
    key: str
    name: str
    width: float
    height: float
    thickness: float
    center: Point
    x_axis: Point
    y_axis: Point
    category: str
    group: str
    holes: tuple[dict[str, float], ...] = ()


@dataclass
class Layout:
    tubes: list[Tube]
    plates: list[Plate]
    bays: list[Bay]
    posts: list[Post]


def _number(p: dict[str, Any], key: str, minimum: float = 0.0) -> float:
    value = p[key]
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{key}必须是数值")
    result = float(value)
    if not math.isfinite(result) or result < minimum:
        raise ValueError(f"{key}必须是不小于{minimum:g}的有限数值")
    return result


def _choice(p: dict[str, Any], key: str, values: set):
    value = p[key]
    if value not in values:
        raise ValueError(f"{key}的选项不受支持")
    return value


def _add(point: Point, axis: Point, amount: float) -> Point:
    return tuple(point[i] + axis[i] * amount for i in range(3))


def _dot(a: Point, b: Point) -> float:
    return sum(x * y for x, y in zip(a, b))


def _lateral(direction: Point) -> Point:
    return (-direction[1], direction[0], 0.0)


def _profile(p: dict[str, Any], prefix: str):
    profile = _catalog.load_profile(p, prefix)
    if profile.kind not in {"rect", "round"}:
        raise ValueError("组合式护栏当前支持矩形管和圆管；异型管需要独立的节点校核")
    return profile


def distribute_bars(clear: float, width: float, maximum_gap: float, mode: str) -> tuple[list[float], list[float]]:
    """Return centers from the left clear face and all edge/internal gaps."""
    if min(clear, width, maximum_gap) <= 0 or not all(math.isfinite(v) for v in (clear, width, maximum_gap)):
        raise ValueError("柱间净空、竖杆宽度和净间距必须大于0")
    count = max(0, math.ceil((clear - maximum_gap) / (width + maximum_gap) - EPS))
    if count > 500 or count * width >= clear - EPS:
        raise ValueError("竖杆规格或间距无法装入柱间净空")
    if count == 0:
        return [], [clear]
    if mode == "equal":
        gap = (clear - count * width) / (count + 1)
        return [gap + width / 2 + i * (width + gap) for i in range(count)], [gap] * (count + 1)
    if mode != "fixed_center":
        raise ValueError("竖杆排列模式不受支持")
    # If very thick bars make the requested inner pitch impossible, equalize
    # instead of producing negative edge gaps or overlapping bars.
    internal = min(maximum_gap, (clear - count * width) / max(1, count - 1))
    edge = (clear - count * width - (count - 1) * internal) / 2
    return ([edge + width / 2 + i * (width + internal) for i in range(count)],
            [edge] + [internal] * (count - 1) + [edge])


def build_layout(p: dict[str, Any]) -> Layout:
    layout = _choice(p, "layout", {"straight", "left_l", "right_l", "u"})
    rails = _choice(p, "railCount", {2, 3})
    corner_mode = _choice(p, "cornerPostMode", {"shared", "double"})
    large_mode = _choice(p, "largePostMode", {"none", "middle", "ends"})
    dimensions = _choice(p, "dimensionMode", {"outside_to_outside", "center_to_center"})
    distribution = _choice(p, "barDistribution", {"equal", "fixed_center"})
    infill_type = _choice(p, "infillType", {"bars", "plate", "lower_plate"})
    installation = _choice(p, "installation", {"embedded", "base_plate"})
    height = _number(p, "guardHeight", 300)
    bottom = _number(p, "bottomClearance")
    drop = _number(p, "upperRailDrop", 1)
    extension = _number(p, "endExtension")
    max_post = _number(p, "maximumPostSpacing", 100)
    maximum_gap = _number(p, "maximumVerticalClearGap", 1)
    double_gap = _number(p, "doublePostClearGap")
    handrail, post, infill, rail = (_profile(p, key) for key in ("handrail", "post", "infill", "rail"))
    large_parameters = dict(p)
    large_size = _number(p, "largePostSize", max(post.width, post.depth) if large_mode != "none" else 1)
    large_size = max(large_size, post.width, post.depth)
    large_parameters.update(postWidth=large_size, postDepth=large_size)
    # Imported-profile overrides are not resized by changing dimensions.
    large_parameters.pop("tubeDesignerProfileOverrides", None)
    large_post = _profile(large_parameters, "post")
    base_z = _number(p, "basePlateThickness", 1) if installation == "base_plate" else 0.0
    if bottom < base_z:
        raise ValueError("底部净空不能小于安装底板厚度")
    cap_z = height - handrail.depth / 2
    top_infill_z = height - handrail.depth if rails == 2 else height - drop - rail.depth / 2
    lower_top_z = bottom + rail.depth
    if rails == 3 and height - drop + rail.depth / 2 >= height - handrail.depth - EPS:
        raise ValueError("上装饰横档距顶过小，与扶手相碰")
    if top_infill_z <= lower_top_z + 1:
        raise ValueError("横档之间没有足够的竖杆或板件安装空间")
    if post.width > max_post or post.depth > max_post:
        raise ValueError("立柱最大间距必须大于立柱截面尺寸")
    if rail.width > min(post.width, post.depth) + EPS:
        raise ValueError("横档侧向宽度不能超过立柱两向截面尺寸，否则转角横档会相碰")
    if infill.depth > min(rail.width, handrail.width) + EPS:
        raise ValueError("竖杆侧向深度不能超过横档或扶手侧向宽度")

    directions: list[Point] = [(1.0, 0.0, 0.0)]
    if layout != "straight":
        directions.append((0.0, -1.0 if layout == "right_l" else 1.0, 0.0))
    if layout == "u":
        directions.append((-1.0, 0.0, 0.0))
    vertices: list[Point] = [(0.0, 0.0, 0.0)]
    for index, direction in enumerate(directions):
        length = _number(p, f"sideLength{index + 1}", 100)
        if dimensions == "outside_to_outside":
            left_profile = large_post if large_mode == "ends" and index == 0 else post
            right_profile = large_post if large_mode == "ends" and index == len(directions) - 1 else post
            length -= (abs(direction[0]) * (left_profile.width + right_profile.width)
                       + abs(direction[1]) * (left_profile.depth + right_profile.depth)) / 2
        if length <= max(post.width, post.depth) + 1:
            raise ValueError(f"第{index + 1}边长度不足以安装两根立柱")
        vertices.append(_add(vertices[-1], direction, length))

    if layout == "u":
        # The first and third sides are parallel in X, so compare their actual
        # centerline separation with the widest total Y envelope. This also
        # prevents an oversized cap from pushing the middle cap beyond its bay.
        occupied_width = max(handrail.width, post.depth, rail.width)
        if large_mode != "none":
            occupied_width = max(occupied_width, large_post.depth)
        if infill_type != "plate":
            occupied_width = max(occupied_width, infill.depth)
        if infill_type != "bars":
            occupied_width = max(occupied_width, _number(p, "panelThickness", 0.1))
        if installation == "base_plate":
            column_size = max(post.width, post.depth)
            if large_mode != "none":
                column_size = max(column_size, large_post.width, large_post.depth)
            occupied_width = max(
                occupied_width, _number(p, "basePlateSize", 20),
                column_size + 2 * _number(p, "baseBoltEdgeDistance", 1) + _number(p, "baseBoltDiameter", 1),
            )
        separation = abs(vertices[2][1] - vertices[1][1])
        if separation <= occupied_width + EPS:
            raise ValueError(
                f"U型两平行边中心距为{separation:g} mm，不足以容纳扶手、立柱、板件或底板的横向尺寸；"
                f"请增加第2边长度，使中心距大于{occupied_width:g} mm，或减小相应截面/底板尺寸。"
            )

    tubes: list[Tube] = []
    plates: list[Plate] = []
    bays: list[Bay] = []
    posts: list[Post] = []
    by_position: dict[Point, Post] = {}
    segment_posts: list[list[Post]] = []
    double_pairs: dict[int, list[Post]] = {}

    def reserve_part() -> None:
        # Enforce the budget on lightweight layout records, before NeutralModel
        # creates geometry graphs or native code constructs any solid bodies.
        if len(tubes) + len(plates) >= MAX_PART_COUNT:
            raise ValueError(
                f"当前护栏零件总数将超过{MAX_PART_COUNT}件（管材和板件合计），可能导致预览或导出耗时过长；"
                "请缩短每边长度、调整合理的竖杆间距，或将护栏分段建模。"
            )

    def add_plate(plate: Plate) -> None:
        reserve_part()
        plates.append(plate)

    def register_post(point: Point, large: bool = False) -> Post:
        signature = tuple(round(v, 7) for v in point)
        if signature not in by_position:
            item = Post(f"post.{len(posts) + 1:04d}", point, large_post if large else post, large)
            posts.append(item)
            by_position[signature] = item
        return by_position[signature]

    def setback(direction: Point) -> float:
        if post.kind == "round":
            return (post.width + double_gap) / math.sqrt(2)
        return abs(direction[0]) * post.width + abs(direction[1]) * post.depth + double_gap / math.sqrt(2)

    for index, direction in enumerate(directions):
        length = math.dist(vertices[index], vertices[index + 1])
        start_offset = setback(direction) if index > 0 and corner_mode == "double" else 0.0
        end_offset = setback(direction) if index < len(directions) - 1 and corner_mode == "double" else 0.0
        a, b = start_offset, length - end_offset
        if b - a <= max(post.width, post.depth) + 1:
            raise ValueError("转角双立柱占用空间过大，请增加边长或减小双柱间距")
        anchor_positions = [a, (a + b) / 2, b] if large_mode == "middle" else [a, b]
        positions = [a]
        for left, right in zip(anchor_positions, anchor_positions[1:]):
            count = max(1, math.ceil((right - left) / max_post))
            positions.extend(left + (right - left) * n / count for n in range(1, count + 1))
        current: list[Post] = []
        for distance in positions:
            large = ((large_mode == "middle" and abs(distance - (a + b) / 2) < EPS)
                     or (large_mode == "ends" and ((index == 0 and abs(distance - a) < EPS)
                         or (index == len(directions) - 1 and abs(distance - b) < EPS))))
            current.append(register_post(_add(vertices[index], direction, distance), large))
        segment_posts.append(current)
        if corner_mode == "double":
            if index > 0:
                double_pairs.setdefault(index, []).append(current[0])
            if index < len(directions) - 1:
                double_pairs.setdefault(index + 1, []).append(current[-1])

    def add_tube(key: str, name: str, start: Point, end: Point, profile,
                 x_axis: Point, y_axis: Point, category: str, group: str,
                 clips: list[str] | None = None) -> Tube:
        if math.dist(start, end) < 0.1:
            raise ValueError(f"{name}长度不足，请调整护栏尺寸")
        reserve_part()
        tube = Tube(key, name, start, end, profile, x_axis, y_axis, category, group, list(clips or []))
        tubes.append(tube)
        return tube

    # All normal posts terminate below the cap. Round cap/post joints are
    # explicitly coped by subtracting the receiving tube's solid outer envelope.
    caps_by_segment: list[list[Tube]] = []
    for index, direction in enumerate(directions):
        origin, finish = vertices[index], vertices[index + 1]
        side = _lateral(direction)
        current = segment_posts[index]
        cap_start = _add(origin, direction, -extension if index == 0 else handrail.width / 2)
        cap_end = _add(finish, direction, extension if index == len(directions) - 1 else handrail.width / 2)
        if handrail.kind == "round" and index > 0:
            cap_start = origin
        cuts: list[tuple[Point, Post | None]] = [(cap_start, None)]
        cuts.extend((column.point, column) for column in current if column.large)
        cuts.append((cap_end, None))
        current_caps: list[Tube] = []
        for cap_index, ((left, left_post), (right, right_post)) in enumerate(zip(cuts, cuts[1:]), 1):
            # End large posts consume the end overhang; no microscopic cap stub.
            if (left_post is None and right_post is not None and current[0] is right_post):
                continue
            if (left_post is not None and right_post is None and current[-1] is left_post):
                continue
            clips: list[str] = []
            if left_post:
                if left_post.profile.kind == "round":
                    clips.append(left_post.key)
                else:
                    left = _add(left, direction, left_post.half_extent(direction))
            if right_post:
                if right_post.profile.kind == "round":
                    clips.append(right_post.key)
                else:
                    right = _add(right, direction, -right_post.half_extent(direction))
            if index > 0 and cap_index == 1 and handrail.kind == "round":
                clips.append(caps_by_segment[index - 1][-1].key)
            cap = add_tube(f"segment.{index + 1}.cap.{cap_index}", f"第{index + 1}边扶手 {cap_index}",
                          (left[0], left[1], cap_z), (right[0], right[1], cap_z), handrail,
                          side, Z, "guardrail.handrail", f"segment.{index + 1}", clips)
            current_caps.append(cap)
        caps_by_segment.append(current_caps)

    for column in posts:
        end_z = height if column.large else height - handrail.depth
        clips: list[str] = []
        if not column.large and handrail.kind == "round":
            end_z = cap_z
            for index, columns in enumerate(segment_posts):
                if column in columns:
                    clips.extend(cap.key for cap in caps_by_segment[index]
                                 if _dot(tuple(column.point[j] - cap.start[j] for j in range(3)), directions[index]) >= -EPS
                                 and _dot(tuple(cap.end[j] - column.point[j] for j in range(3)), directions[index]) >= -EPS)
        add_tube(column.key, ("大立柱" if column.large else "立柱") + " " + column.key.split(".")[-1],
                 (column.point[0], column.point[1], base_z), (column.point[0], column.point[1], end_z),
                 column.profile, (1.0, 0.0, 0.0), (0.0, 1.0, 0.0), "guardrail.post", "posts", list(dict.fromkeys(clips)))

    for index, (direction, columns) in enumerate(zip(directions, segment_posts), 1):
        side = _lateral(direction)
        for bay_index, (left, right) in enumerate(zip(columns, columns[1:]), 1):
            key = f"segment.{index}.bay.{bay_index}"
            start_face = _add(left.point, direction, left.half_extent(direction))
            end_face = _add(right.point, direction, -right.half_extent(direction))
            clear = _dot(tuple(end_face[j] - start_face[j] for j in range(3)), direction)
            if clear <= infill.width:
                raise ValueError("立柱尺寸或布置导致柱间没有有效净空")
            centers, gaps = distribute_bars(clear, infill.width, maximum_gap, distribution)
            bays.append(Bay(key, direction, left, right, clear, centers, gaps))

            def cross_rail(suffix: str, label: str, z: float) -> Tube:
                a, b = start_face, end_face
                clips: list[str] = []
                if left.profile.kind == "round":
                    a = left.point
                    clips.append(left.key)
                if right.profile.kind == "round":
                    b = right.point
                    clips.append(right.key)
                return add_tube(key + "." + suffix, f"第{index}边第{bay_index}跨{label}",
                                (a[0], a[1], z), (b[0], b[1], z), rail, side, Z,
                                "guardrail.cross_rail", key, clips)

            bottom_rail = cross_rail("bottom", "下横档", bottom + rail.depth / 2)
            upper_rail = cross_rail("upper", "上横档", height - drop) if rails == 3 else None
            bar_lower, bar_upper = lower_top_z, top_infill_z
            lower_support = bottom_rail
            if infill_type != "bars":
                gap = _number(p, "panelEdgeClearance")
                panel_top = top_infill_z
                if infill_type == "lower_plate":
                    panel_top = lower_top_z + _number(p, "lowerPanelHeight", 1)
                    if panel_top + rail.depth >= top_infill_z - 1:
                        raise ValueError("下半封板高度过大，没有剩余竖杆空间")
                    lower_support = cross_rail("divider", "封板上横档", panel_top + rail.depth / 2)
                    bar_lower = panel_top + rail.depth
                panel_width, panel_height = clear - 2 * gap, panel_top - lower_top_z - 2 * gap
                thickness = _number(p, "panelThickness", 0.1)
                if panel_width <= thickness or panel_height <= thickness:
                    raise ValueError("板边间隙过大或板厚不合适，板件无法装入框架")
                center = _add(start_face, direction, clear / 2)
                add_plate(Plate(key + ".panel", f"第{index}边第{bay_index}跨封板", panel_width,
                                    panel_height, thickness, (center[0], center[1], (lower_top_z + panel_top) / 2),
                                    direction, Z, "plate.infill", key))
            if infill_type != "plate":
                for bar_index, distance in enumerate(centers, 1):
                    point = _add(start_face, direction, distance)
                    z0, z1 = bar_lower, bar_upper
                    clips: list[str] = []
                    if rail.kind == "round":
                        z0 = lower_support.start[2]
                        clips.append(lower_support.key)
                    if upper_rail is not None and rail.kind == "round":
                        z1 = upper_rail.start[2]
                        clips.append(upper_rail.key)
                    elif upper_rail is None and handrail.kind == "round":
                        z1 = cap_z
                        clips.extend(cap.key for cap in caps_by_segment[index - 1])
                    add_tube(key + f".bar.{bar_index:03d}", f"第{index}边第{bay_index}跨竖杆 {bar_index}",
                             (point[0], point[1], z0), (point[0], point[1], z1), infill, direction, side,
                             "guardrail.vertical_bar", key, clips)

    if installation == "base_plate":
        plate_size = _number(p, "basePlateSize", 20)
        bolt_diameter = _number(p, "baseBoltDiameter", 1)
        edge = _number(p, "baseBoltEdgeDistance", 1)
        grouped: set[str] = set()
        groups = list(double_pairs.values()) + [[column] for column in posts]
        for group in groups:
            if any(column.key in grouped for column in group):
                continue
            grouped.update(column.key for column in group)
            size = max(plate_size, *(max(c.profile.width, c.profile.depth) + 2 * edge + bolt_diameter for c in group))
            min_x, max_x = min(c.point[0] for c in group), max(c.point[0] for c in group)
            min_y, max_y = min(c.point[1] for c in group), max(c.point[1] for c in group)
            width, depth = max_x - min_x + size, max_y - min_y + size
            if edge <= bolt_diameter / 2 or edge >= min(width, depth) / 2:
                raise ValueError("底板螺栓边距必须大于孔半径并小于底板半宽")
            holes = tuple({"x": x * (width / 2 - edge), "y": y * (depth / 2 - edge), "diameter": bolt_diameter}
                          for x in (-1, 1) for y in (-1, 1))
            add_plate(Plate(f"base.{group[0].key}", "转角双柱共用底板" if len(group) > 1 else "立柱底板",
                                width, depth, base_z, ((min_x + max_x) / 2, (min_y + max_y) / 2, base_z / 2),
                                (1.0, 0.0, 0.0), (0.0, 1.0, 0.0), "plate.base", "bases", holes))
        bases = [plate for plate in plates if plate.category == "plate.base"]
        for index, first in enumerate(bases):
            for second in bases[index + 1:]:
                if (abs(first.center[0] - second.center[0]) < (first.width + second.width) / 2 - EPS
                        and abs(first.center[1] - second.center[1]) < (first.height + second.height) / 2 - EPS):
                    raise ValueError("相邻立柱底板互相重叠，请增大立柱间距、减小底板尺寸或另行设计共用底板")
    return Layout(tubes, plates, bays, posts)


def generate(parameters: dict[str, Any], context: dict[str, Any]) -> dict[str, Any]:
    _geometry.request_geometry_purpose(context)
    built = build_layout(parameters)
    model = NeutralModel(template_id="modular-guardrail", template_version="1.0.0",
                         package_digest=str(context.get("template", {}).get("packageDigest", "")), parameters=parameters)
    shared = _geometry.SharedTubeGeometry(model)
    originals: dict[str, str] = {}
    envelopes: dict[str, str] = {}
    needed_tools = {key for part in built.tubes for key in part.clips}
    for part in built.tubes:
        arguments = {"placement": {"origin": list(part.start), "xAxis": list(part.x_axis), "yAxis": list(part.y_axis)},
                     "contours": part.profile.contours()}
        vector = [part.end[i] - part.start[i] for i in range(3)]
        originals[part.key] = shared.emit_tube(part.key, profile_arguments=arguments, extrude_arguments={"vector": vector})
        if part.key in needed_tools:
            envelopes[part.key] = shared.emit_tube(part.key + ".envelope",
                profile_arguments={**arguments, "contours": part.profile.contours()[:1]}, extrude_arguments={"vector": vector})
    item_keys: list[str] = []
    rows: list[dict[str, Any]] = []

    def add_item(key: str, name: str, representation: str, properties: dict[str, Any]):
        number = f"{parameters['productCode']}-{len(item_keys) + 1:03d}"
        properties.update(partNumber=number, quantity=1)
        item_keys.append(model.item(key, name, representations={"display": representation, "export": representation}, properties=properties))
        rows.append({"key": "row." + key, "parentKey": str(parameters["productCode"]), "itemKey": key,
                     "values": {"partNumber": number, "name": name, "quantity": 1, "length": properties["length"],
                                "materialType": "板件" if properties.get("manufacturing.partKind") == "plate" else "管材"}})

    for part in built.tubes:
        representation = originals[part.key]
        start_cut, end_cut = "square", "square"
        if part.clips:
            tools = [envelopes[key] for key in dict.fromkeys(part.clips)]
            representation = model.geometry(part.key + ".finished", "boolean", inputs=[representation, *tools],
                arguments={"operation": "subtract", "target": representation, "tools": tools})
            direction = tuple((part.end[i] - part.start[i]) / part.length for i in range(3))
            receivers = {receiver.key: receiver for receiver in built.tubes}
            for key in part.clips:
                receiver = receivers[key]
                relative = tuple((receiver.start[i] + receiver.end[i]) / 2 - part.start[i] for i in range(3))
                if _dot(relative, direction) <= part.length / 2:
                    start_cut = "cope"
                else:
                    end_cut = "cope"
        add_item(part.key, part.name, representation, {
            "group": part.group, "length": round(part.length, 3), "manufacturing.partKind": "tube",
            "manufacturing.categoryKey": part.category, "manufacturing.categoryName": {
                "guardrail.handrail": "扶手管", "guardrail.post": "立柱", "guardrail.cross_rail": "横档", "guardrail.vertical_bar": "竖杆"}[part.category],
            "tubeDesigner.profile": part.profile.properties(),
            "tubeDesigner.endProcess": {"startCut": start_cut, "endCut": end_cut,
                                        "connection": "weld", "lengthBasis": "blank_axial_extent" if part.clips else "finished"},
        })
        for index, receiver in enumerate(dict.fromkeys(part.clips), 1):
            model.relationship(f"joint.{part.key}.{index}", "weld", [part.key, receiver], properties={"geometry": "outer-envelope-cope"})
    for part in built.plates:
        representation = _plates.emit_rectangular_plate(model, part.key, width=part.width, height=part.height,
            thickness=part.thickness, center=part.center, x_axis=part.x_axis, y_axis=part.y_axis, holes=part.holes)
        properties = _plates.plate_properties(part.width, part.height, part.thickness,
            material_grade=str(parameters.get("plateMaterial", "")), category_key=part.category,
            category_name="底板" if part.category == "plate.base" else "封板")
        properties["group"] = part.group
        add_item(part.key, part.name, representation, properties)
    model.output("display.default", "display", item_keys)
    model.output("export.manufacturing", "export", item_keys)
    model.table("parts", "护栏零件清单", columns=[
        {"key": "partNumber", "displayName": "零件编号", "valueType": "string"},
        {"key": "name", "displayName": "名称", "valueType": "string"},
        {"key": "materialType", "displayName": "材料类型", "valueType": "string"},
        {"key": "quantity", "displayName": "数量", "valueType": "integer"},
        {"key": "length", "displayName": "长度", "valueType": "number", "unit": "mm"}], rows=rows)
    model.diagnostic("info", "guardrail.design-review", "模板提供尺寸及加工几何，不代替项目规范、结构承载与锚固设计校核。")
    return _geometry.finish_geometry_request(model, context)
