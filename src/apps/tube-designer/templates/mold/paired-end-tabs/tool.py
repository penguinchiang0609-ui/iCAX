"""Round insertion ears on two or four walls of a rectangular tube.

Two ears place one at the centre of each of two opposite walls; four place one
at the centre of every wall around the tube opening.
The stock keeps its original end at each rounded tip; all intervening material
is set back by tabLength. The partner's section is used only to reject ears
that would collide with its opposite wall.
"""
import math


EPS = 0.01


def _rectangle(width, height):
    points = [[-width / 2, -height / 2], [width / 2, -height / 2],
              [width / 2, height / 2], [-width / 2, height / 2]]
    return {"kind": "path", "closed": True, "segments": [
        {"kind": "line", "start": points[index], "end": points[(index + 1) % 4]}
        for index in range(4)]}


def _round_tab(depth, width):
    radius = width / 2
    if depth <= radius:
        raise ValueError("圆弧插舌长度须大于其半宽")
    return {"kind": "path", "closed": True, "segments": [
        {"kind": "line", "start": [depth + EPS, -radius], "end": [radius, -radius]},
        {"kind": "arc", "start": [radius, -radius], "middle": [0, 0], "end": [radius, radius]},
        {"kind": "line", "start": [radius, radius], "end": [depth + EPS, radius]},
        {"kind": "line", "start": [depth + EPS, radius], "end": [depth + EPS, -radius]},
    ]}


def _shell(profile, label):
    if not isinstance(profile, dict):
        raise ValueError(label + "需要方矩管截面")
    metrics = section_geometry.closed_shell_metrics(section_geometry.from_profile(profile))
    if metrics.get("circularShell"):
        raise ValueError(label + "当前仅支持有平直壁面的方矩管")
    return metrics


def _stock_axes(shell, bounds):
    """Match the saved section axes to the actual extrusion's transverse axes.

    Product generation can quarter-turn the section before extrusion.  The
    manufacturing blank, rather than the profile's metadata, is authoritative
    for where the two physical skins lie.
    """
    outer = shell["outside"]
    section_spans = [outer["max"][i] - outer["min"][i] for i in (0, 1)]
    stock_spans = [bounds["max"][i] - bounds["min"][i] for i in (1, 2)]
    tolerance = max(0.05, shell["tolerance"] * 10)
    direct = all(abs(a - b) <= tolerance for a, b in zip(section_spans, stock_spans))
    turned = all(abs(a - b) <= tolerance for a, b in zip(reversed(section_spans), stock_spans))
    if direct and turned and not section_geometry.quarter_turn_symmetric(shell):
        raise ValueError("方形截面的轴向有歧义，两个方向的轮廓不相同")
    if direct:
        return 1, 0  # physical Z is the second section coordinate
    if turned:
        return 0, 1  # physical Z is the first section coordinate
    raise ValueError("实际管材横截面与插舌模板保存的截面尺寸不一致")


def _flat_range(contour, normal_axis, height, tolerance):
    if normal_axis == 1:
        return section_geometry.horizontal_support(contour, height, tolerance)["range"]
    matches = []
    for edge in contour["edges"]:
        if edge["kind"] != "line":
            continue
        a, b = edge["start"], edge["end"]
        if abs(a[0] - height) <= tolerance and abs(b[0] - height) <= tolerance:
            matches.append(sorted((a[1], b[1])))
    if len(matches) != 1:
        raise ValueError("插舌位置不是唯一平直壁面")
    return matches[0]


def generate(parameters, context):
    own = _shell(context.get("targetSection"), "圆弧插舌")
    partner = _shell(context.get("section", {}).get("profile"), "承接管")
    outer, inner = own["outside"], own["inside"]
    partner_outer, partner_inner = partner["outside"], partner["inside"]
    count = parameters["pairCount"]
    width, depth = (parameters[key] for key in ("tabWidth", "tabLength"))
    if count not in (2, 4):
        raise ValueError("圆弧插舌数量只能为 2 或 4")
    if depth <= width / 2 + EPS:
        raise ValueError("圆弧插舌长度须大于半宽")
    if depth <= partner["wallThickness"] + EPS:
        raise ValueError("插舌长度必须穿过承接管进入侧壁")
    if depth >= partner_outer["max"][1] - partner_inner["min"][1] - EPS:
        raise ValueError("插舌长度会碰到承接管对侧壁")
    place = context["placement"]
    length = context["bounds"]["max"][0] - context["bounds"]["min"][0]
    if not math.isfinite(place["trim"]) or place["trim"] < 0 or place["trim"] + depth >= length - EPS:
        raise ValueError("端部修剪量和插舌长度须小于管材长度")
    if abs(place["rotation"]) > 1e-7:
        raise ValueError("成对圆弧插舌当前要求与方矩管平直壁对齐，不支持轴向旋转")

    # Native end-local placement mirrors physical Z for end=end. Resolve the
    # saved section's quarter-turn against the actual blank first.
    section_z, section_y = _stock_axes(own, context["bounds"])
    lo, hi = context["bounds"]["min"], context["bounds"]["max"]
    walls = [(2, section_z, section_y, "max"), (2, section_z, section_y, "min")]
    if count == 4:
        walls.extend(((1, section_y, section_z, "max"),
                      (1, section_y, section_z, "min")))
    tolerance = own["tolerance"]
    for physical_normal, section_normal, section_across, side in walls:
        outer_line = _flat_range(own["contours"][0], section_normal,
                                 outer[side][section_normal], tolerance)
        inner_line = _flat_range(own["contours"][1], section_normal,
                                 inner[side][section_normal], tolerance)
        physical_across = 1 if physical_normal == 2 else 2
        section_center = (outer["min"][section_across] + outer["max"][section_across]) / 2
        stock_center = (lo[physical_across] + hi[physical_across]) / 2
        left = max(outer_line[0], inner_line[0]) + stock_center - section_center
        right = min(outer_line[1], inner_line[1]) + stock_center - section_center
        if stock_center - width / 2 <= left + EPS or stock_center + width / 2 >= right - EPS:
            raise ValueError("圆弧插舌越过方矩管平直壁，须减小宽度")

    reach = 4 * (sum(hi[i] - lo[i] for i in range(3)) + 10)
    nodes = []
    nodes.append({"key": "end-slab-profile", "operator": "profile2d", "arguments": {
        "placement": {"origin": [(depth - reach) / 2, 0, -reach],
                      "xAxis": [1, 0, 0], "yAxis": [0, 1, 0]},
        "contours": [_rectangle(depth + reach, 2 * reach)]}})
    nodes.append({"key": "end-slab", "operator": "extrude", "inputs": ["end-slab-profile"],
                  "arguments": {"vector": [0, 0, 2 * reach]}})
    ears = []
    for wall_index, (physical_normal, section_normal, _, side) in enumerate(walls):
        wall_thickness = abs(outer[side][section_normal] - inner[side][section_normal])
        if side == "max":
            n0, n1 = hi[physical_normal] - wall_thickness - EPS, hi[physical_normal] + EPS
        else:
            n0, n1 = lo[physical_normal] - EPS, lo[physical_normal] + wall_thickness + EPS
        if physical_normal == 2 and context["end"] == "end":
            n0, n1 = -n1, -n0
        origin = [0, (lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2]
        if context["end"] == "end":
            origin[2] = -origin[2]
        origin[physical_normal] = n0
        across_axis = 1 if physical_normal == 2 else 2
        y_axis = [0, 0, 0]
        y_axis[across_axis] = 1
        vector = [0, 0, 0]
        vector[physical_normal] = n1 - n0
        key = f"ear-{wall_index}-0"
        nodes.append({"key": key + "-profile", "operator": "profile2d", "arguments": {
            "placement": {"origin": origin, "xAxis": [1, 0, 0], "yAxis": y_axis},
            "contours": [_round_tab(depth, width)]}})
        nodes.append({"key": key, "operator": "extrude", "inputs": [key + "-profile"],
                      "arguments": {"vector": vector}})
        ears.append(key)
    nodes.append({"key": "all-ears", "operator": "boolean", "inputs": ears,
                  "arguments": {"operation": "union"}})
    nodes.append({"key": "tool", "operator": "boolean", "inputs": ["end-slab", "all-ears"],
                  "arguments": {"operation": "subtract"}})
    return {"mode": "solid", "coordinateSpace": "end-local", "outputKey": "tool",
            "datumCenter": None, "requireEndContact": True, "requireRetainedEndMaterial": True,
            "model": {"schema": "icax.neutral-model", "schemaVersion": 1,
                      "template": {"id": "paired-end-tabs", "version": "2.0.0", "packageDigest": "self-contained"},
                      "geometry": nodes}}
