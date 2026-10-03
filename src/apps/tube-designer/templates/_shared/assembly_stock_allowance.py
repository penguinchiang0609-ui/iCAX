"""Uncut stock around product assembly nodes, independent of joint recipes."""
from functools import lru_cache
import importlib.util
import json
import math
from pathlib import Path
import sys


def _dot(first, second):
    return sum(a * b for a, b in zip(first, second))


@lru_cache(maxsize=1)
def _section_geometry():
    path = Path(__file__).with_name("section_geometry.py")
    name = "icax_assembly_stock_section_geometry"
    if name not in sys.modules:
        spec = importlib.util.spec_from_file_location(name, path)
        module = importlib.util.module_from_spec(spec)
        sys.modules[name] = module
        spec.loader.exec_module(module)
    return sys.modules[name]


@lru_cache(maxsize=1)
def _recognition_geometry():
    path = Path(__file__).with_name("profile_recognition_geometry.py")
    name = "icax_assembly_stock_profile_recognition_geometry"
    if name not in sys.modules:
        spec = importlib.util.spec_from_file_location(name, path)
        module = importlib.util.module_from_spec(spec)
        sys.modules[name] = module
        spec.loader.exec_module(module)
    return sys.modules[name]


@lru_cache(maxsize=512)
def _contour_projection(serialized_contours, across_x, across_y):
    geometry = _section_geometry()
    contours = json.loads(serialized_contours)
    section = geometry.from_profile({"contours": contours})
    magnitude = math.hypot(across_x, across_y)
    if magnitude <= 1e-9:
        raise ValueError("相邻管材截面与节点构件轴向没有有效投影")
    angle = math.degrees(math.atan2(across_y, across_x))
    rotated = geometry.local_section(section, angle)
    projected = geometry.bounds(rotated["contours"][0])
    # from_profile centres the contour at its bounding box. Restore that
    # translation so the support interval remains relative to the real datum.
    recognition = _recognition_geometry()
    raw = recognition.bounds(recognition.contour(contours[0], 0.001))
    raw_center = ((raw[0] + raw[2]) / 2, (raw[1] + raw[3]) / 2)
    offset = across_x * raw_center[0] + across_y * raw_center[1]
    result = (magnitude * projected["min"][0] + offset,
              magnitude * projected["max"][0] + offset)
    if not all(math.isfinite(value) for value in result) or result[1] <= result[0]:
        raise ValueError("相邻管材真实外截面投影无效")
    return result


def _support_interval(profile_arguments, member_axis, node_point):
    """Neighbour outer contour projected about a node, in product coordinates."""
    placement = profile_arguments["placement"]
    direction = tuple(float(value) for value in member_axis)
    norm = math.sqrt(_dot(direction, direction))
    if not math.isfinite(norm) or norm <= 1e-9:
        raise ValueError("节点构件轴向无效")
    direction = tuple(value / norm for value in direction)
    x = _dot(placement["xAxis"], direction)
    y = _dot(placement["yAxis"], direction)
    contours = profile_arguments["contours"]
    minimum, maximum = _contour_projection(
        json.dumps(contours, sort_keys=True, separators=(",", ":")),
        round(x, 12), round(y, 12))
    origin = placement["origin"]
    if len(origin) != 3 or len(node_point) != 3:
        raise ValueError("相邻管材截面原点和节点须为三维坐标")
    offset = _dot([float(origin[i]) - float(node_point[i]) for i in range(3)], direction)
    return minimum + offset, maximum + offset


def opposing_half_extent(profile_arguments, member_axis, *, node_point):
    """Conservative stock allowance covering both sides of a neighbour."""
    minimum, maximum = _support_interval(profile_arguments, member_axis, node_point)
    result = max(abs(minimum), abs(maximum))
    if not math.isfinite(result) or result <= 0:
        raise ValueError("相邻管材真实外截面投影无效")
    return result


def opposing_end_stock(profile_arguments, member_axis, *, node_point, end):
    """Stock allowance and near-side inset for an L member's end.

    A member leaving its start node meets the positive profile face; a member
    approaching its end node meets the negative one. The two distances can
    differ when the section datum is asymmetric.
    """
    if end not in ("start", "end"):
        raise ValueError("节点端必须为 start 或 end")
    minimum, maximum = _support_interval(profile_arguments, member_axis, node_point)
    if minimum > 1e-7 or maximum < -1e-7:
        raise ValueError("节点中心线不在相邻管材外轮廓投影内")
    allowance = max(abs(minimum), abs(maximum))
    inset = maximum if end == "start" else -minimum
    if not math.isfinite(allowance) or not math.isfinite(inset) or allowance <= 0 or inset < 0:
        raise ValueError("相邻管材真实外截面投影无效")
    return {"stockAllowance": allowance, "contactInset": inset}


def stock_span(start, end, start_allowance=0.0, end_allowance=0.0):
    """Keep logical endpoints while extending a square-ended source blank."""
    logical_start = tuple(float(value) for value in start)
    logical_end = tuple(float(value) for value in end)
    length = math.dist(logical_start, logical_end)
    if length <= 1e-9:
        raise ValueError("节点原管的逻辑轴长无效")
    allowances = (float(start_allowance), float(end_allowance))
    if any(not math.isfinite(value) or value < 0 for value in allowances):
        raise ValueError("节点原管端部余量无效")
    axis = tuple((logical_end[i] - logical_start[i]) / length for i in range(3))
    physical_start = [logical_start[i] - axis[i] * allowances[0] for i in range(3)]
    physical_end = [logical_end[i] + axis[i] * allowances[1] for i in range(3)]
    return {"start": physical_start, "end": physical_end,
            "axis": list(axis), "axisLength": length,
            "length": length + sum(allowances),
            "stockInterval": {"startStation": -allowances[0],
                              "endStation": length + allowances[1]},
            "allowances": {"start": allowances[0], "end": allowances[1]}}
