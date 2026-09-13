"""Piecewise guardrail elevation and tread-constrained post placement.

Inclined members keep rigid, orthonormal sections. Caps terminate at the posts;
the posts remain vertical. Receivers' solid outer envelopes create real copes.
This is NOT a continuous bent handrail or an engineering safety certification.
"""
from __future__ import annotations

import math


def number(p, key, default, minimum, maximum):
    value = p.get(key, default)
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or not minimum <= value <= maximum:
        raise ValueError(f"{key}必须是{minimum:g}至{maximum:g}的有限数值")
    return float(value)


def tread_count(p, segment):
    if p.get("pathMode", "level") == "level":
        return 0
    source = p.get("elevationSource", "angle")
    if source not in {"angle", "treads"}:
        raise ValueError("高程来源不受支持")
    if source != "treads":
        return 0
    value = p.get(f"treadCount{segment + 1}", 10 if segment != 1 else 0)
    if isinstance(value, bool) or not isinstance(value, int) or not 0 <= value <= 500:
        raise ValueError("踏步间隔数必须是0至500的整数，0表示水平平台")
    return value


def tread_post_positions(p, a, b, maximum_spacing, large_mode):
    going = number(p, "treadGoing", 280, 1, 2000)
    first, last = math.ceil(a / going - 1e-8), math.floor(b / going + 1e-8)
    maximum_steps = math.floor(maximum_spacing / going + 1e-8)
    if first >= last or maximum_steps < 1:
        raise ValueError("当前踏步间隔及最大柱距无法布置两根立柱")
    anchors = [first, last]
    if large_mode == "middle":
        middle = (first + last) // 2
        if middle in anchors:
            raise ValueError("踏步数量不足以增加中间大立柱")
        anchors.insert(1, middle)
    result = [first * going]
    for start, end in zip(anchors, anchors[1:]):
        count = math.ceil((end - start) / maximum_steps)
        result.extend((start + math.floor(i * (end - start) / count + .5)) * going for i in range(1, count + 1))
    return result


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def add(point, direction, distance):
    return tuple(point[i] + direction[i] * distance for i in range(3))


def apply_elevation(built, p, Tube, KeepVolume, distribute):
    mode = p["pathMode"]
    if p["layout"] != "straight" and p["cornerPostMode"] == "double":
        raise ValueError("高程转角当前采用共用立柱；双立柱之间的跨角连接节点尚未实现")
    if p["infillType"] not in {"bars", "horizontal"} or p.get("guardrailUse", "platform") != "platform":
        raise ValueError("高程排布目前支持竖杆或横向管材填充；板件、花格和围墙出头结构需要独立的高程节点")
    if any(c.category != "accessory.post_cap" for c in built.components):
        raise ValueError("高程模式仅支持立柱柱帽，其他配件尚未提供高程安装接口")
    going = number(p, "treadGoing", 280, 1, 2000) if p.get("elevationSource") == "treads" else None
    rise = number(p, "treadRise", 175, -500, 500) if going else None
    segments = []
    elevation = 0.
    for i, (a, b) in enumerate(zip(built.path_vertices, built.path_vertices[1:])):
        length = math.dist(a, b)
        direction = tuple((b[j] - a[j]) / length for j in range(3))
        count = tread_count(p, i)
        if going:
            slope = rise / going if count else 0.
            if abs(slope) > math.tan(math.radians(60)):
                raise ValueError("踏步高宽比对应坡度超过60度")
        else:
            key = "slopeAngle" if i == 0 else f"slopeAngle{i + 1}"
            angle = number(p, key, 30 if i != 1 else 0, -60, 60)
            slope = math.tan(math.radians(angle))
        cosine = 1 / math.sqrt(1 + slope * slope)
        segments.append({"origin": a, "direction": direction, "length": length,
                         "slope": slope, "cos": cosine, "base": elevation, "treads": count})
        elevation += length * slope
    built.elevation_segments = segments

    def segment_for(bay):
        return segments[int(bay.key.split(".")[1]) - 1]

    def station(point, segment):
        return dot(tuple(point[i] - segment["origin"][i] for i in range(3)), segment["direction"])

    def terrain(point, segment):
        distance = station(point, segment)
        if going and segment["treads"]:
            return segment["base"] + math.floor(distance / going + .5 + 1e-8) * rise
        return segment["base"] + distance * segment["slope"]

    height = float(p["guardHeight"])
    cap_profile, rail_profile, infill = (built.profiles[k] for k in ("handrail", "rail", "infill"))
    # At a corner two rails meet separate faces of the same post. Limiting
    # lateral width keeps them inside the post envelope, including round posts.
    for post in built.posts:
        if cap_profile.width > min(post.profile.width, post.profile.depth) + 1e-7:
            raise ValueError("分跨扶手侧向宽度不能超过立柱截面；请选匹配立柱，避免转角接头悬空或相碰")
    extent = 10000 + 8 * (sum(s["length"] for s in segments) + abs(elevation) + height)
    posts_by_key = {post.key: post for post in built.posts}
    old_posts = {t.key: t for t in built.tubes if t.category == "guardrail.post"}
    new_tubes = []
    built.volumes.clear()
    contacts = {key: [] for key in old_posts}
    incident_directions = {key: set() for key in old_posts}
    for bay in built.bays:
        incident_directions[bay.left.key].add(bay.direction)
        incident_directions[bay.right.key].add(tuple(-v for v in bay.direction))
    corner_keeps = {}

    def corner_limits(post, outward):
        keys = []
        for other in sorted(incident_directions[post.key]):
            if abs(dot(outward, other)) > 1 - 1e-7:
                continue
            signature = (post.key, outward, other)
            if signature not in corner_keeps:
                vector = tuple(outward[j] - other[j] for j in range(3))
                length = math.sqrt(dot(vector, vector))
                normal = tuple(v / length for v in vector)
                key = f"corner.keep.{len(corner_keeps) + 1}"
                center = add(post.point, normal, extent / 2)
                built.volumes.append(KeepVolume(key, center, extent, extent, extent, normal, (0., 0., 1.)))
                corner_keeps[signature] = key
            keys.append(corner_keeps[signature])
        return keys

    def vertical_ends(tube, a, b, direction):
        # A prism bounded by the two vertical receiving planes.
        length = dot(tuple(b[j] - a[j] for j in range(3)), direction)
        if length <= .1:
            raise ValueError("分跨横梁有效长度不足")
        key = tube.key + ".elevation.ends"
        center = tuple((a[j] + b[j]) / 2 if j != 2 else 0. for j in range(3))
        built.volumes.append(KeepVolume(key, center, length, extent, extent, direction, (0., 0., 1.)))
        tube.keep_volume = key

    for bay in built.bays:
        s = segment_for(bay)
        direction = s["direction"]
        side = (-direction[1], direction[0], 0.)
        slope = s["slope"] if mode == "continuous" else 0.
        cosine = 1 / math.sqrt(1 + slope * slope)
        sine = slope * cosine
        normal = (-direction[0] * sine, -direction[1] * sine, cosine)
        along = (direction[0] * cosine, direction[1] * cosine, sine)
        base = s["base"] if mode == "continuous" else max(terrain(bay.left.point, s), terrain(bay.right.point, s))

        def z_at(point, intercept):
            return base + intercept + station(point, s) * slope

        cap_center = height - cap_profile.depth / (2 * cosine)
        lower_center = float(p["bottomClearance"]) + rail_profile.depth / (2 * cosine)
        upper_center = height - float(p["upperRailDrop"]) if p["railCount"] == 3 else cap_center
        upper_profile = rail_profile if p["railCount"] == 3 else cap_profile
        low_surface = lower_center + rail_profile.depth / (2 * cosine)
        high_surface = upper_center - upper_profile.depth / (2 * cosine)
        if high_surface <= low_surface + 1:
            raise ValueError("倾斜后的横档之间没有有效填充空间")
        if p["railCount"] == 3 and upper_center + rail_profile.depth / (2 * cosine) >= cap_center - cap_profile.depth / (2 * cosine):
            raise ValueError("倾斜后上横档与扶手相碰，请增大距顶尺寸")

        def beam(suffix, name, profile, intercept, category, vertical_width=False):
            start = bay.left.point
            end = bay.right.point
            # Raw rails reach the post centreplanes and are coped by both post
            # envelopes. This also resolves rounded rectangular corner regions.
            depth = profile.width if vertical_width else profile.depth
            margin = depth * abs(sine) / 2 + .01
            a, b = add(start, direction, -margin), add(end, direction, margin)
            tube = Tube(bay.key + suffix, name,
                        (a[0], a[1], z_at(a, intercept)), (b[0], b[1], z_at(b, intercept)),
                        profile, normal if vertical_width else side,
                        tuple(-v for v in side) if vertical_width else normal,
                        category, bay.key, [bay.left.key, bay.right.key])
            tube.diagonal = abs(slope) > 1e-9
            vertical_ends(tube, start, end, direction)
            # Partition shared-post corners with complementary plan bisectors.
            # This prevents outside-post overlap without subtracting many
            # intersecting receiving pipes from each other.
            tube.extra_keep_volumes = corner_limits(bay.left, direction) + corner_limits(bay.right, tuple(-v for v in direction))
            new_tubes.append(tube)
            return tube

        cap = beam(".elevation.cap", "分跨扶手", cap_profile, cap_center, "guardrail.handrail")
        bottom = beam(".bottom", "下横档", rail_profile, lower_center, "guardrail.cross_rail")
        upper = beam(".upper", "上横档", rail_profile, upper_center, "guardrail.cross_rail") if p["railCount"] == 3 else cap
        for post in (bay.left, bay.right):
            top = z_at(post.point, height) + post.half_extent(direction) * abs(slope)
            contacts[post.key].append((terrain(post.point, s), top, s, bay.key))

        start_face = add(bay.left.point, direction, bay.left.half_extent(direction))
        if p["infillType"] == "horizontal":
            width = infill.width / cosine
            centers, gaps = distribute(high_surface - low_surface, width,
                number(p, "maximumHorizontalClearGap", 110, 1, 2000),
                p["barDistribution"], p.get("fixedBarCount", 8))
            for i, distance in enumerate(centers, 1):
                beam(f".horizontal.{i:03d}", f"横向填充 {i}", infill,
                     low_surface + distance, "guardrail.horizontal_bar", True)
        else:
            for i, distance in enumerate(bay.bar_centers, 1):
                point = add(start_face, direction, distance)
                half_width = infill.width / 2
                lo, hi = z_at(point, lower_center), z_at(point, upper_center)
                tube = Tube(bay.key + f".bar.{i:03d}", f"竖杆 {i}",
                            (point[0], point[1], lo - half_width * abs(slope)),
                            (point[0], point[1], hi + half_width * abs(slope)),
                            infill, direction, side, "guardrail.vertical_bar", bay.key,
                            [bottom.key, upper.key])
                # Keep only the region between the receiver centreplanes, then
                # subtract their outer bodies. No disconnected far-side remnant.
                middle = base + (lower_center + upper_center) / 2
                center = (s["origin"][0], s["origin"][1], middle)
                key = tube.key + ".elevation.keep"
                built.volumes.append(KeepVolume(key, center, extent,
                    (upper_center - lower_center) * cosine, extent, along, normal))
                tube.keep_volume, tube.diagonal = key, abs(slope) > 1e-9
                new_tubes.append(tube)

    ground_by_key, top_by_key = {}, {}
    for key, tube in old_posts.items():
        neighbours = contacts[key]
        ground_values = [entry[0] for entry in neighbours]
        ground = min(ground_values)
        top = max(entry[1] for entry in neighbours)
        tube.start = (*tube.start[:2], tube.start[2] + ground)
        tube.end = (*tube.end[:2], top)
        tube.clips.clear()
        tube.keep_volume = ""
        tube.start_cut_override = tube.end_cut_override = "square"
        if tube.length < 1 or tube.end[2] <= tube.start[2]:
            raise ValueError("踏步与横梁标高导致立柱有效长度不足")
        ground_by_key[key], top_by_key[key] = ground, top
        new_tubes.append(tube)
    if len(new_tubes) + len(built.plates) + len(built.components) > 2000:
        raise ValueError("高程排布后的零件总数超过2000，请分段建模")
    built.tubes = new_tubes

    for plate in built.plates:
        if plate.category != "plate.base":
            raise ValueError("当前高程板件仅支持水平立柱底板")
        members = [post for post in built.posts if
            abs(post.point[0] - plate.center[0]) < plate.width / 2 and
            abs(post.point[1] - plate.center[1]) < plate.height / 2]
        if not members:
            raise ValueError("底板未找到对应立柱")
        ground = ground_by_key[members[0].key]
        if any(abs(ground_by_key[post.key] - ground) > 1e-6 for post in members):
            raise ValueError("共用底板的立柱落在不同标高，不能使用一块水平底板")
        if going:
            for post in members:
                for _, _, s, _ in contacts[post.key]:
                    if not s["treads"]:
                        continue
                    d = s["direction"]
                    half = (abs(d[0]) * plate.width + abs(d[1]) * plate.height) / 2
                    offset = abs(dot(tuple(plate.center[j] - post.point[j] for j in range(3)), d))
                    if half + offset >= going / 2 - 1e-7:
                        raise ValueError("底板跨越踏步边缘，请减小底板或增大踏步进深")
        plate.center = (*plate.center[:2], plate.center[2] + ground)
    for component in built.components:
        key = component.key.removesuffix(".cap")
        if key not in top_by_key:
            raise ValueError("柱帽没有可用的立柱安装基准")
        component.origin = (*component.origin[:2], top_by_key[key])
        # Frozen accessory geometry is resolved by the host. Use that exact
        # solid as a local clearance cutter; do not infer dimensions from an ID.
        adjacent_bays = {entry[3] for entry in contacts[key]}
        for tube in built.tubes:
            if tube.group in adjacent_bays:
                tube.component_clips.append(component.key)
    for post in built.posts:
        post.point = (*post.point[:2], ground_by_key[post.key])
