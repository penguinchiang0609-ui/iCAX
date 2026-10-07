"""Two or four shallow side-wall sockets for round-ended tube ears.

The short tube's saved section supplies each wall's thickness and position.
The long tube's actual flat receiving face supplies
the cutting plane and wall depth. No opposite wall is pierced.
"""
import math


EPS = 0.01


def _capsule(length, width):
    radius = width / 2
    center = length / 2 - radius
    if center <= EPS:
        raise ValueError("母槽长度须大于短管壁厚")
    return {"kind": "path", "closed": True, "segments": [
        {"kind": "line", "start": [-center, -radius], "end": [center, -radius]},
        {"kind": "arc", "start": [center, -radius], "middle": [center + radius, 0], "end": [center, radius]},
        {"kind": "line", "start": [center, radius], "end": [-center, radius]},
        {"kind": "arc", "start": [-center, radius], "middle": [-center - radius, 0], "end": [-center, -radius]},
    ]}


def _shell(profile, label):
    if not isinstance(profile, dict):
        raise ValueError(label + "需要方矩管截面")
    metrics = section_geometry.closed_shell_metrics(section_geometry.from_profile(profile))
    if metrics.get("circularShell"):
        raise ValueError(label + "当前仅支持有平直壁面的方矩管")
    return metrics


def _vertical_support(contour, at, tolerance):
    matches = []
    for edge in contour["edges"]:
        if edge["kind"] != "line":
            continue
        a, b = edge["start"], edge["end"]
        if abs(a[0] - at) <= tolerance and abs(b[0] - at) <= tolerance:
            matches.append(sorted((a[1], b[1])))
    if len(matches) != 1:
        raise ValueError("所选承接面不是唯一的平直壁面")
    return matches[0]


def _stock_axes(shell, bounds):
    outer = shell["outside"]
    section_spans = [outer["max"][i] - outer["min"][i] for i in (0, 1)]
    stock_spans = [bounds["max"][i] - bounds["min"][i] for i in (1, 2)]
    tolerance = max(0.05, shell["tolerance"] * 10)
    direct = all(abs(a - b) <= tolerance for a, b in zip(section_spans, stock_spans))
    turned = all(abs(a - b) <= tolerance for a, b in zip(reversed(section_spans), stock_spans))
    if direct and turned and not section_geometry.quarter_turn_symmetric(shell):
        raise ValueError("方形截面的轴向有歧义，两个方向的轮廓不相同")
    if direct:
        return 0, 1  # saved section coordinates match physical Y, Z
    if turned:
        return 1, 0  # product extrusion quarter-turned the saved section
    raise ValueError("实际承接管横截面与母槽模板保存的截面尺寸不一致")


def _face(host, face, bounds):
    outer, inner = host["outside"], host["inside"]
    tol = host["tolerance"]
    contours = host["contours"]
    section_y, section_z = _stock_axes(host, bounds)
    lo, hi = bounds["min"], bounds["max"]
    if face in ("top", "bottom"):
        side = "max" if face == "top" else "min"
        normal_coordinate, across_coordinate = 2, 1
        normal_axis, across_axis = section_z, section_y
        outward = [0, 0, 1 if face == "top" else -1]
        across = [0, 1, 0]
    elif face in ("left", "right"):
        side = "min" if face == "left" else "max"
        normal_coordinate, across_coordinate = 1, 2
        normal_axis, across_axis = section_y, section_z
        outward = [0, -1 if face == "left" else 1, 0]
        across = [0, 0, 1]
    else:
        raise ValueError("成对母槽须指定方矩管的 top、bottom、left 或 right 面")
    outside = hi[normal_coordinate] if side == "max" else lo[normal_coordinate]
    if normal_axis == 1:
        first = section_geometry.horizontal_support(contours[0], outer[side][1], tol)["range"]
        second = section_geometry.horizontal_support(contours[1], inner[side][1], tol)["range"]
    else:
        first = _vertical_support(contours[0], outer[side][0], tol)
        second = _vertical_support(contours[1], inner[side][0], tol)
    section_center = (outer["min"][across_axis] + outer["max"][across_axis]) / 2
    center = (lo[across_coordinate] + hi[across_coordinate]) / 2
    translation = center - section_center
    first = [value + translation for value in first]
    second = [value + translation for value in second]
    flat = [max(first[0], second[0]), min(first[1], second[1])]
    wall = abs(outer[side][normal_axis] - inner[side][normal_axis])
    if wall <= tol or flat[1] <= flat[0]:
        raise ValueError("承接管所选面的内外平直壁面无共同范围")
    return {"outside": outside, "wall": wall, "flat": flat, "outward": outward,
            "across": across, "center": center, "normalIndex": normal_coordinate,
            "acrossIndex": across_coordinate}


def _branch_pair_axes(profile, face, example_rotation):
    """Map the real end-tab walls into the receiving part's verified axes."""
    frame = profile.get("sectionFrame")
    if frame is None:
        # A template example declares its pose explicitly. A bound product
        # instead uses only the verified manufacturing direction below.
        return 1, 0, example_rotation
    if frame.get("verification") != "committed-source-brep-replay":
        raise ValueError("支管制造截面尚未通过真实原管验证")
    vector = frame.get("branchManufacturingZHostToolPart")
    first, second = frame.get("xAxisToolPart"), frame.get("yAxisToolPart")
    for value in (vector, first, second):
        if (not isinstance(value, list) or len(value) != 3
                or any(isinstance(item, bool) or not isinstance(item, (int, float))
                       or not math.isfinite(item) for item in value)):
            raise ValueError("支管双插舌缺少已核对的实体壁面方向")
    if abs(math.sqrt(sum(item * item for item in vector)) - 1) > 1e-6:
        raise ValueError("支管双插舌实体壁面方向无效")
    if (any(abs(sum(item * item for item in axis) - 1) > 1e-6
            for axis in (first, second))
            or abs(sum(a * b for a, b in zip(first, second))) > 1e-6):
        raise ValueError("支管双插舌截面方向不是正交单位坐标")
    projections = [abs(sum(a * b for a, b in zip(vector, axis)))
                   for axis in (first, second)]
    if max(projections) < 1 - 1e-6:
        raise ValueError("支管双插舌与实际方矩管壁面不平行")
    z_axis = 0 if projections[0] > projections[1] else 1
    axial, across = abs(vector[0]), abs(vector[face["acrossIndex"]])
    if abs(vector[face["normalIndex"]]) > 1e-6:
        raise ValueError("支管双插舌方向未落在主管接触面内")
    if across > 1 - 1e-6:
        rotation = 0
    elif axial > 1 - 1e-6:
        rotation = 90
    else:
        raise ValueError("支管双插舌方向未与主管轴向或横向对齐")
    return z_axis, 1 - z_axis, rotation


def generate(parameters, context):
    host = _shell(context.get("targetSection"), "承接管")
    branch_profile = context.get("feature", {}).get("section", {}).get("profile")
    branch = _shell(branch_profile, "插入管")
    feature = context["feature"]
    # Manufacturing can explicitly request an open edge socket for equal-size
    # tubes. The public standalone tool continues to require closed flat slots.
    allow_side_opening = parameters.get("allowSideOpening", False)
    if type(allow_side_opening) is not bool:
        raise ValueError("母槽侧边开口策略须为布尔值")
    face = _face(host, feature.get("face", "top"), context["bounds"])
    branch_z, branch_y, pair_rotation = _branch_pair_axes(
        branch_profile, face, parameters.get("pairRotation", 0))
    count = parameters["pairCount"]
    width, depth, clearance = (parameters[key] for key in
        ("tabWidth", "tabLength", "sideClearance"))
    allow_end_opening = parameters.get("allowEndOpening", False)
    if count not in (2, 4):
        raise ValueError("母槽数量只能为 2 或 4")
    if depth <= face["wall"] + EPS:
        raise ValueError("插舌长度必须穿过承接管进入侧壁")
    lo, hi = context["bounds"]["min"], context["bounds"]["max"]
    available = hi[face["normalIndex"]] - lo[face["normalIndex"]] - face["wall"]
    if depth >= available - EPS:
        raise ValueError("插舌长度会碰到承接管对侧壁")

    reference, station = feature.get("reference", "start"), feature.get("station", 0)
    if isinstance(station, bool) or not isinstance(station, (int, float)) or not math.isfinite(station):
        raise ValueError("母槽轴向位置无效")
    if reference == "start":
        axial = lo[0] + station
    elif reference == "end":
        axial = hi[0] - station
    elif reference == "center":
        axial = (lo[0] + hi[0]) / 2 + station
    else:
        raise ValueError("母槽定位基准无效")
    offset = feature.get("offset", 0)
    rotation = context.get("placement", {}).get("rotation", 0) + pair_rotation
    if isinstance(offset, bool) or not isinstance(offset, (int, float)) or not math.isfinite(offset):
        raise ValueError("母槽横向偏移无效")
    if isinstance(rotation, bool) or not isinstance(rotation, (int, float)) or not math.isfinite(rotation):
        raise ValueError("母槽旋转无效")
    c, s = math.cos(math.radians(rotation)), math.sin(math.radians(rotation))
    outer_branch, inner_branch = branch["outside"], branch["inside"]
    # The branch's physical Z walls define the first pair. Verified product
    # mapping chooses whether that pair runs across or along the host face.
    # The second pair turns 90 degrees so each capsule follows its wall.
    branch_centers = [
        (outer_branch["min"][axis] + outer_branch["max"][axis]) / 2
        for axis in (0, 1)
    ]
    sockets = []
    for side in ("max", "min"):
        row = (outer_branch[side][branch_z] + inner_branch[side][branch_z]) / 2 - branch_centers[branch_z]
        wall = abs(outer_branch[side][branch_z] - inner_branch[side][branch_z])
        sockets.append((0, row, wall, False))
    if count == 4:
        for side in ("max", "min"):
            column = (outer_branch[side][branch_y] + inner_branch[side][branch_y]) / 2 - branch_centers[branch_y]
            wall = abs(outer_branch[side][branch_y] - inner_branch[side][branch_y])
            sockets.append((column, 0, wall, True))
    axial_axis = [c, s * face["across"][1], s * face["across"][2]]
    across_axis = [-s, c * face["across"][1], c * face["across"][2]]
    nodes, slots = [], []
    for index, (x, row, wall, turned) in enumerate(sockets):
        opening_width = wall + 2 * clearance
        # The capsule's full-width root must cover the tab; its rounded ends
        # therefore add one opening-width to the nominal tab width.
        opening_length = width + opening_width
        if opening_length <= opening_width + EPS:
            raise ValueError("插舌宽度须大于短管壁厚")
        x_axis = across_axis if turned else axial_axis
        y_axis = [-component for component in axial_axis] if turned else across_axis
        x_extent = abs(x_axis[0]) * opening_length / 2 + abs(y_axis[0]) * opening_width / 2
        across_extent = (abs(x_axis[face["acrossIndex"]]) * opening_length / 2
                         + abs(y_axis[face["acrossIndex"]]) * opening_width / 2)
        stock_x = axial + c * x - s * row
        stock_across = face["center"] + offset + s * x + c * row
        before_start = stock_x - x_extent <= lo[0] + EPS
        after_end = stock_x + x_extent >= hi[0] - EPS
        if not allow_end_opening:
            if before_start or after_end:
                raise ValueError("母槽越过长管端部，须调整连接节点")
        elif (stock_x <= lo[0] + EPS or stock_x >= hi[0] - EPS
              or (before_start and after_end)):
            # An end-opening socket may cross one stock end, but its center
            # must still cut the host wall; never accept an off-stock cutter.
            raise ValueError("端部开口母槽中心须位于长管内，且不能贯通两端")
        crosses_flat = (stock_across - across_extent <= face["flat"][0] + EPS
                        or stock_across + across_extent >= face["flat"][1] - EPS)
        if allow_side_opening:
            across_lo, across_hi = lo[face["acrossIndex"]], hi[face["acrossIndex"]]
            if (not across_lo + EPS < stock_across < across_hi - EPS
                    or (stock_across - across_extent <= across_lo + EPS
                        and stock_across + across_extent >= across_hi - EPS)):
                raise ValueError("侧边开口母槽中心须落在实际管壁范围内，且不能贯通两侧")
        elif crosses_flat:
            raise ValueError("母槽越过长管平直侧壁，须调整管型或横移")
        # A side channel still contains material at the rounded ear tip. The
        # outside EPS must not shorten its inward reach and leave interference.
        # Closed sockets already exit into the cavity, so their tool is intact.
        cut_depth = depth + 2 * EPS if allow_side_opening and crosses_flat else depth
        if cut_depth >= available - EPS:
            raise ValueError("母槽切削余量会碰到承接管对侧壁")
        origin = [stock_x, 0, 0]
        origin[face["acrossIndex"]] = stock_across
        origin[face["normalIndex"]] = face["outside"] + EPS * face["outward"][face["normalIndex"]]
        key = f"socket-{index}"
        nodes.append({"key": key + "-profile", "operator": "profile2d", "arguments": {
            "placement": {"origin": origin, "xAxis": x_axis, "yAxis": y_axis},
            "contours": [_capsule(opening_length, opening_width)]}})
        nodes.append({"key": key, "operator": "extrude", "inputs": [key + "-profile"],
                      "arguments": {"vector": [-cut_depth * component for component in face["outward"]]}})
        slots.append(key)
    nodes.append({"key": "tool", "operator": "boolean", "inputs": slots,
                  "arguments": {"operation": "union"}})
    return {"mode": "solid", "coordinateSpace": "part", "outputKey": "tool",
            "model": {"schema": "icax.neutral-model", "schemaVersion": 1,
                      "template": {"id": "paired-side-slots", "version": "2.2.0", "packageDigest": "self-contained"},
                      "geometry": nodes}}
