"""A process-neutral frame with four L corners and two T junctions."""
from __future__ import annotations

from copy import deepcopy
import hashlib
import importlib.util
import math
from pathlib import Path
import sys

from icax_template_sdk import NeutralModel, to_resource_model, display_context, to_display_model
from icax_template_sdk import manufacturing_context, manufacturing_declaration, to_manufacturing_model


TEMPLATE_ID = "assembly-frame-lt"
TEMPLATE_VERSION = "1.0.0"
SHARED_ROOT = Path(__file__).resolve().parents[2] / "_shared"


def _shared(name):
    source = SHARED_ROOT / name
    module_name = "icax_assembly_frame_lt_" + hashlib.sha256(source.read_bytes()).hexdigest()[:16]
    if module_name not in sys.modules:
        spec = importlib.util.spec_from_file_location(module_name, source)
        if spec is None or spec.loader is None:
            raise RuntimeError(f"无法加载共享管材规则：{name}")
        module = importlib.util.module_from_spec(spec)
        sys.modules[module_name] = module
        spec.loader.exec_module(module)
    return sys.modules[module_name]


def _number(parameters, key, minimum, maximum):
    value = parameters[key]
    if isinstance(value, bool) or not isinstance(value, (int, float)) \
            or not math.isfinite(value) or not minimum <= value <= maximum:
        raise ValueError(f"{key} 必须是 {minimum:g}～{maximum:g} 之间的有限数值")
    return float(value)


def _point(x, z):
    return [float(x), 0.0, float(z)]


def _end_anchor(item_key, end, length, point, approach_face=None, stock_allowance=0.0):
    anchor = {"kind": "end", "end": end, "stockAllowance": stock_allowance}
    if approach_face is not None:
        anchor["approachFace"] = approach_face
        anchor["contactInset"] = stock_allowance
    return {"itemKey": item_key, "kind": "end", "end": end,
            "anchor": anchor,
            "localAxialStation": 0.0 if end == "start" else length,
            "centerlinePoint": deepcopy(point)}


def _side_anchor(item_key, station, point, face, normal, contact):
    return {"itemKey": item_key, "kind": "side", "face": face,
            "anchor": {"kind": "side", "face": face, "reference": "start", "station": station},
            "localAxialStation": station, "centerlinePoint": deepcopy(point),
            "faceNormal": list(normal), "contactPoint": deepcopy(contact)}


def _manufacturing_face(axis, world_normal):
    # The native manufacturing frame puts the directed member axis on +X.
    # For this X/Z-plane frame, world +Y remains local +Y and X cross Y is
    # local +Z. Record the process anchor in that local frame, while retaining
    # faceNormal/contactPoint separately in product/world coordinates.
    local_z = (-axis[2], 0.0, axis[0])
    across_y = world_normal[1]
    across_z = sum(world_normal[i] * local_z[i] for i in range(3))
    if abs(across_y) > abs(across_z) and abs(across_y) > 0.99:
        return "right" if across_y > 0 else "left"
    if abs(across_z) > 0.99:
        return "top" if across_z > 0 else "bottom"
    raise ValueError("节点侧面法向与构件制造坐标系不对齐")


def _rectangular_mirror_center(contours):
    center = None
    for contour in contours:
        segments = contour.get("segments", [])
        if (contour.get("kind") != "path" or contour.get("closed") is not True
                or len(segments) != 4 or any(
                    segment.get("kind") != "line"
                    or ((segment["start"][0] == segment["end"][0])
                        == (segment["start"][1] == segment["end"][1]))
                    for segment in segments)):
            raise ValueError("装配框提供截面的横向原管暂只支持镜像对称矩形轮廓")
        points = {tuple(point) for segment in segments
                  for point in (segment["start"], segment["end"])}
        xs, ys = sorted({point[0] for point in points}), sorted({point[1] for point in points})
        if len(xs) != 2 or len(ys) != 2 or points != {
                (x, y) for x in xs for y in ys}:
            raise ValueError("装配框提供截面的横向原管暂只支持镜像对称矩形轮廓")
        current = (ys[0] + ys[1]) / 2
        if center is not None and abs(center - current) > 1.0e-8:
            raise ValueError("装配框提供截面的内外轮廓没有共同镜像基准")
        center = current
    return center


def _section_frame(start, end, stock_span, profile_arguments, profile, catalogue):
    placement = profile_arguments["placement"]
    length = math.dist(start, end)
    axis = [(end[i] - start[i]) / length for i in range(3)]
    x_axis, y_axis = placement["xAxis"], placement["yAxis"]
    canonical = profile.contours()
    emitted = profile_arguments["contours"]
    if emitted != canonical:
        if emitted != [catalogue._swap_contour_axes(contour) for contour in canonical]:
            raise ValueError("装配框原管截面与提供的管型轮廓不一致")
        x_axis, y_axis = y_axis, x_axis
    cross = [x_axis[1] * y_axis[2] - x_axis[2] * y_axis[1],
             x_axis[2] * y_axis[0] - x_axis[0] * y_axis[2],
             x_axis[0] * y_axis[1] - x_axis[1] * y_axis[0]]
    alignment = sum(cross[i] * axis[i] for i in range(3))
    mirror_offset = 0.0
    old_y_axis = y_axis
    if alignment < -1.0 + 1.0e-8:
        if profile.properties().get("geometrySource") == "providedBoundary":
            mirror_offset = 2 * _rectangular_mirror_center(canonical)
        y_axis = [-value for value in y_axis]
    elif alignment < 1.0 - 1.0e-8:
        raise ValueError("装配框原管截面与构件轴向不一致")
    origin = [placement["origin"][i] + mirror_offset * old_y_axis[i]
              - axis[i] * stock_span["stockInterval"]["startStation"]
              for i in range(3)]
    delta = [start[i] - origin[i] for i in range(3)]
    top = [-axis[2], 0.0, axis[0]]
    return {"originAtStart": origin, "xAxis": x_axis, "yAxis": y_axis,
            "centerlineUV": [sum(delta[i] * x_axis[i] for i in range(3)),
                             sum(delta[i] * y_axis[i] for i in range(3))],
            "faceNormals": {"top": top, "bottom": [-value for value in top],
                            "right": [0.0, 1.0, 0.0], "left": [0.0, -1.0, 0.0]}}


def _generate_resource_document(parameters, context):
    p = parameters
    width = _number(p, "width", 160, 10000)
    height = _number(p, "height", 160, 10000)
    middle_height = _number(p, "middleHeight", 60, 9940)
    middle_axis_angle = _number(p, "middleAxisAngle", 60, 120)
    if not isinstance(p["productCode"], str) or not p["productCode"].strip():
        raise ValueError("产品编号不能为空")
    for key in ("frameMaterial", "middleMaterial"):
        if not isinstance(p[key], str) or not p[key].strip():
            raise ValueError(f"{key} 不能为空")

    catalogue = _shared("tube_profile_catalog.py")
    frame = catalogue.load_profile(p, "frame")
    middle = catalogue.load_profile(p, "middle")
    if not frame.hollow or not middle.hollow:
        raise ValueError("框架与中横档必须使用中空管型")
    if width <= 2 * frame.width + middle.width \
            or height <= 2 * frame.width + middle.width:
        raise ValueError("成品尺寸不足以容纳外框和中横档")
    if not frame.width + middle.width / 2 < middle_height \
            < height - frame.width - middle.width / 2:
        raise ValueError("中横档须位于上下边框之间")

    half = frame.width / 2
    left, right = half, width - half
    bottom, top = half, height - half
    middle_right_height = (middle_height if middle_axis_angle == 90 else
                           middle_height + (right - left) / math.tan(math.radians(middle_axis_angle)))
    if not frame.width + middle.width / 2 < middle_right_height \
            < height - frame.width - middle.width / 2:
        raise ValueError("倾斜中横档的右节点超出外框内侧")
    model = NeutralModel(
        template_id=TEMPLATE_ID, template_version=TEMPLATE_VERSION,
        package_digest=str(context.get("template", {}).get("packageDigest", "")),
        parameters=deepcopy(p))
    shared_geometry = _shared("shared_tube_geometry.py")
    builder = shared_geometry.SharedTubeGeometry(model)
    stock_rules = _shared("assembly_stock_allowance.py")
    frames = _shared("security_window_frame_geometry.py")

    # These are full, square-ended raw tubes. Every joint is a separate
    # relationship; this product template never picks a miter, wrap or slot.
    members = [
        ("frame.left.0001", "左边框", _point(left, bottom), _point(left, top), frame, p["frameMaterial"]),
        ("frame.right.0001", "右边框", _point(right, bottom), _point(right, top), frame, p["frameMaterial"]),
        ("frame.bottom.0001", "下边框", _point(left, bottom), _point(right, bottom), frame, p["frameMaterial"]),
        ("frame.top.0001", "上边框", _point(left, top), _point(right, top), frame, p["frameMaterial"]),
        ("frame.middle.0001", "中横档", _point(left, middle_height), _point(right, middle_right_height), middle, p["middleMaterial"]),
    ]
    item_keys = []
    lengths = {}
    axes = {}
    allowances = {}
    rows = []
    horizontal_section = frames._profile_arguments((0, 0, 0), (1, 0, 0), frame)
    vertical_section = frames._profile_arguments((0, 0, 0), (0, 0, 1), frame)
    vertical_allowance = stock_rules.opposing_half_extent(horizontal_section, (0, 0, 1), node_point=(0, 0, 0))
    horizontal_allowance = stock_rules.opposing_half_extent(vertical_section, (1, 0, 0), node_point=(0, 0, 0))
    for index, (key, name, start, end, profile, material) in enumerate(members, start=1):
        horizontal = start[2] == end[2]
        distance = math.dist(start, end)
        lengths[key] = distance
        axes[key] = [(end[i] - start[i]) / distance for i in range(3)]
        sloped_middle = key == "frame.middle.0001" and middle_axis_angle != 90
        allowance = horizontal_allowance if horizontal or sloped_middle else vertical_allowance
        if sloped_middle:
            allowance = max(allowance, half / axes[key][0] + 10.0)
        stock_span = stock_rules.stock_span(start, end, allowance, allowance)
        allowances[key] = stock_span["allowances"]
        x_axis, y_axis = ([0.0, 1.0, 0.0], [-axes[key][2], 0.0, axes[key][0]]) \
            if sloped_middle else (([0.0, 1.0, 0.0], [0.0, 0.0, 1.0]) if horizontal
                                   else ([1.0, 0.0, 0.0], [0.0, 1.0, 0.0]))
        profile_arguments = {
            "placement": {"origin": stock_span["start"], "xAxis": x_axis, "yAxis": y_axis},
            "contours": profile.contours(swap_axes=horizontal or sloped_middle),
        }
        stock = builder.emit_tube(key, profile_arguments=profile_arguments,
                                  extrude_arguments={"vector": [stock_span["end"][i] - stock_span["start"][i]
                                                               for i in range(3)]})
        section_frame = _section_frame(start, end, stock_span, profile_arguments, profile, catalogue)
        part_number = f"{p['productCode']}-{index:03d}"
        properties = {
            "partNumber": part_number, "length": stock_span["length"], "quantity": 1,
            "manufacturing.partKind": "tube", "manufacturing.sourcing": "made",
            "manufacturing.materialCategory": "tube", "manufacturing.materialGrade": material,
            "manufacturing.categoryKey": "assembly-frame-lt.frame" if profile is frame else "assembly-frame-lt.middle",
            "manufacturing.categoryName": name,
            "tubeDesigner.profile": profile.properties(),
            "tubeDesigner.manufacturingAxis": axes[key],
            "tubeDesigner.manufacturingStartToEnd": axes[key],
            "tubeDesigner.assemblyPlanning": {"stockState": "uncut", "ready": False},
            "tubeDesigner.endProcess": {"startCut": "square", "endCut": "square",
                                        "lengthBasis": "blank_axial_extent"},
            "assemblyFrame.member": {"start": start, "end": end,
                                     "stockState": "uncut", "axisLength": distance,
                                     "sectionFrame": section_frame,
                                     "stockInterval": stock_span["stockInterval"]},
        }
        item_keys.append(model.item(key, name,
                                    representations={"display": stock, "export": stock},
                                    properties=properties))
        rows.append({"key": f"part.{index:03d}", "itemKey": key, "values": {
            "partNumber": part_number, "name": name, "profile": profile.specification,
            "material": material, "length": stock_span["length"], "quantity": 1,
        }})

    corners = [
        ("bottom-left", "frame.left.0001", "start", "frame.bottom.0001", "start", _point(left, bottom)),
        ("bottom-right", "frame.right.0001", "start", "frame.bottom.0001", "end", _point(right, bottom)),
        ("top-left", "frame.left.0001", "end", "frame.top.0001", "start", _point(left, top)),
        ("top-right", "frame.right.0001", "end", "frame.top.0001", "end", _point(right, top)),
    ]
    for label, vertical, vertical_end, horizontal, horizontal_end, point in corners:
        vertical_toward_horizontal = (1, 0, 0) if "left" in label else (-1, 0, 0)
        horizontal_toward_vertical = (0, 0, 1) if "bottom" in label else (0, 0, -1)
        model.relationship(f"frame.corner.{label}", "assembly", [vertical, horizontal],
                           properties={"topology": "L", "centerlinePoint": point,
                                       "participantAnchors": [
                                           _end_anchor(vertical, vertical_end, lengths[vertical], point,
                                                       _manufacturing_face(axes[vertical], vertical_toward_horizontal),
                                                       allowances[vertical][vertical_end]),
                                           _end_anchor(horizontal, horizontal_end, lengths[horizontal], point,
                                                       _manufacturing_face(axes[horizontal], horizontal_toward_vertical),
                                                       allowances[horizontal][horizontal_end]),
                                       ]})
    for side, host, branch_end, x, normal, wall_x in (
            ("left", "frame.left.0001", "start", left, (1, 0, 0), frame.width),
            ("right", "frame.right.0001", "end", right, (-1, 0, 0), width - frame.width)):
        point = _point(x, middle_height if side == "left" else middle_right_height)
        branch = "frame.middle.0001"
        face = _manufacturing_face(axes[host], normal)
        contact_z = point[2] + (wall_x - x) * axes[branch][2] / axes[branch][0]
        host_anchor = _side_anchor(host, point[2] - bottom, point, face, normal,
                                   _point(wall_x, contact_z))
        branch_anchor = _end_anchor(branch, branch_end, lengths[branch], point,
                                    stock_allowance=allowances[branch][branch_end])
        branch_anchor["anchor"]["contactInset"] = math.dist(
            point, host_anchor["contactPoint"])
        model.relationship(f"frame.junction.middle-{side}", "assembly", [host, branch],
                           properties={"topology": "T", "centerlinePoint": point,
                                       "participantAnchors": [
                                           host_anchor, branch_anchor,
                                       ]})

    model.output("display.default", "display", item_keys)
    model.output("export.manufacturing", "export", item_keys)
    model.table("parts", "框架原管", columns=[
        {"key": "partNumber", "displayName": "编号", "valueType": "string"},
        {"key": "name", "displayName": "构件", "valueType": "string"},
        {"key": "profile", "displayName": "管型", "valueType": "string"},
        {"key": "material", "displayName": "材质", "valueType": "string"},
        {"key": "length", "displayName": "原管长度", "valueType": "number", "unit": "mm"},
        {"key": "quantity", "displayName": "数量", "valueType": "integer"},
    ], rows=rows)
    return to_resource_model(shared_geometry.finish_geometry_request(model, context))


def display(parameter_values):
    """Generate display data from the values owned by the product instance."""
    return to_display_model(_generate_resource_document(parameter_values, display_context(__file__)))




def manufacturing(parameter_values):
    """Return manufacturing declarations from the values owned by the host."""
    with manufacturing_declaration():
        return to_manufacturing_model(
            _generate_resource_document(parameter_values, manufacturing_context(__file__)))
