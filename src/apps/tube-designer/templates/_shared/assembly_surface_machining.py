"""Reusable local cutting functions for an extrusion or a planar stock.

The inputs describe one machining site in its actual local frame.  The
functions do not know a product, panel layout, catalogue, or SDK model, and
return neutral cutter declarations for the shared geometry runtime to replay.
"""
from copy import deepcopy
import math


_HOST_STOCK_FIELDS = {"id", "label", "profileRef", "parameters", "length", "matrix",
                      "anchor", "section", "memberEntityId", "itemKey", "nodeFrame", "productAnchor"}


def _stock_fields(stock, required):
    """Accept host-owned identity and exact section snapshots, never a product."""
    return set(required) <= set(stock) and not set(stock) - set(required) - _HOST_STOCK_FIELDS


def _number(value, label, minimum=None, maximum=None):
    if (type(value) not in (int, float) or not math.isfinite(value) or
            minimum is not None and value < minimum or
            maximum is not None and value > maximum):
        raise ValueError(label + " 数值无效")
    return float(value)


def _vector(value, label, dimensions=3):
    if not isinstance(value, (list, tuple)) or len(value) != dimensions:
        raise ValueError(label + " 坐标无效")
    return [_number(component, label) for component in value]


def _dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def _point(frame, point, depth=0.0):
    return [frame["origin"][i] + frame["xAxis"][i] * point[0] +
            frame["yAxis"][i] * point[1] + frame["depthAxis"][i] * depth
            for i in range(3)]


def _frame(value):
    if not isinstance(value, dict) or set(value) != {
            "origin", "xAxis", "yAxis", "depthAxis"}:
        raise ValueError("加工坐标系须声明原点、两个面内轴和深度轴")
    result = {key: _vector(value[key], "加工坐标系 " + key) for key in value}
    axes = [result[key] for key in ("xAxis", "yAxis", "depthAxis")]
    if any(abs(math.hypot(*axis) - 1) > 1.e-7 for axis in axes):
        raise ValueError("加工坐标系方向须为单位向量")
    if any(abs(_dot(axes[a], axes[b])) > 1.e-7
           for a, b in ((0, 1), (0, 2), (1, 2))):
        raise ValueError("加工坐标系方向须正交")
    return result


def _boundary(value):
    if not isinstance(value, (list, tuple)) or len(value) != 4:
        raise ValueError("加工边界无效")
    result = [_number(v, "加工边界") for v in value]
    if result[1] - result[0] <= 1.e-8 or result[3] - result[2] <= 1.e-8:
        raise ValueError("加工边界没有有效面积")
    return result


def _polygon(value):
    if not isinstance(value, (list, tuple)) or not 3 <= len(value) <= 4096:
        raise ValueError("加工多边形无效")
    result = [_vector(point, "加工多边形", 2) for point in value]
    area = sum(a[0] * b[1] - b[0] * a[1]
               for a, b in zip(result, result[1:] + result[:1]))
    if abs(area) <= 1.e-8:
        raise ValueError("加工多边形没有有效面积")
    return result


def _rect(box):
    l, r, b, t = box
    return [[l, b], [r, b], [r, t], [l, t]]


def _path(points):
    return {"kind": "path", "closed": True, "segments": [
        {"kind": "line", "start": list(points[i]),
         "end": list(points[(i + 1) % len(points)])}
        for i in range(len(points))]}


def _circle(radius):
    radius = _number(radius, "加工圆半径", 1.e-8)
    k = radius / math.sqrt(2.0)
    return {"kind": "path", "closed": True, "segments": [
        {"kind": "arc", "start": [radius, 0], "middle": [k, k], "end": [0, radius]},
        {"kind": "arc", "start": [0, radius], "middle": [-k, k], "end": [-radius, 0]},
        {"kind": "arc", "start": [-radius, 0], "middle": [-k, -k], "end": [0, -radius]},
        {"kind": "arc", "start": [0, -radius], "middle": [k, -k], "end": [radius, 0]}]}


def _clip(points, box):
    """Clip the local convex motif to its actual allowed boundary."""
    for normal, limit in [((1, 0), box[1]), ((-1, 0), -box[0]),
                          ((0, 1), box[3]), ((0, -1), -box[2])]:
        result = []
        for a, b in zip(points, points[1:] + points[:1]):
            da = _dot(a, normal) - limit
            db = _dot(b, normal) - limit
            if da <= 1.e-8:
                result.append(a)
            if (da < -1.e-8 and db > 1.e-8) or (da > 1.e-8 and db < -1.e-8):
                f = da / (da - db)
                result.append([a[i] + f * (b[i] - a[i]) for i in range(2)])
        points = result
    return points


def _overlap(a, b):
    return min(a[1], b[1]) - max(a[0], b[0]) > 1.e-7 and min(a[3], b[3]) - max(a[2], b[2]) > 1.e-7


class _Graph:
    def __init__(self):
        self.geometry = []

    def node(self, key, operator, inputs=None, arguments=None):
        node = {"key": key, "operator": operator, "arguments": arguments or {}}
        if inputs:
            node["inputs"] = list(inputs)
        self.geometry.append(node)
        return key

    def prism(self, key, contours, frame, offset, depth):
        profile = self.node(key + ".profile", "profile2d", arguments={
            "placement": {"origin": _point(frame, [0, 0], offset),
                          "xAxis": list(frame["xAxis"]), "yAxis": list(frame["yAxis"])},
            "contours": deepcopy(contours)})
        return self.node(key + ".solid", "extrude", [profile], {
            "vector": [component * depth for component in frame["depthAxis"]]})

    def boolean(self, key, target, tools, operation):
        if not tools:
            return target
        return self.node(key, "boolean", [target, *tools], {
            "operation": operation, "target": target, "tools": list(tools)})


def _input(process_input):
    if not isinstance(process_input, dict) or set(process_input) != {
            "schema", "schemaVersion", "parts", "geometry"}:
        raise ValueError("局部加工输入字段无效")
    if process_input["schema"] != "icax.assembly-process-input" or process_input["schemaVersion"] != 1:
        raise ValueError("局部加工输入版本无效")
    parts = process_input["parts"]
    if not isinstance(parts, dict) or set(parts) != {"stock"} or not isinstance(parts["stock"], dict):
        raise ValueError("局部加工必须提供实际母材")
    geometry = process_input["geometry"]
    if not isinstance(geometry, dict):
        raise ValueError("局部加工几何数据无效")
    return parts["stock"], geometry


def _result(function_id, process_input, parameters, build):
    result = {"schema": "icax.assembly-process-result", "schemaVersion": 1,
              "templateId": function_id, "applicable": False, "reason": "",
              "parameters": deepcopy(parameters), "geometry": [], "operations": [],
              "materialRequirements": [], "forming": [], "checks": [],
              "contactRequirements": [], "assemblySteps": [], "bom": []}
    try:
        if not isinstance(parameters, dict):
            raise ValueError("局部加工参数须为对象")
        stock, geometry = _input(process_input)
        graph = _Graph()
        root, operation = build(graph, stock, geometry, parameters)
        result.update(applicable=True, geometry=graph.geometry,
                      operations=[{"id": "stock:local-cut", "kind": "neutral-csg",
                                   "role": "stock", "operation": operation, "tool": root}])
    except (ValueError, KeyError, TypeError, OverflowError) as error:
        result.update(reason=str(error) or "局部加工必要数据无效", geometry=[], operations=[])
    return result


def extrusion_end_clip(process_input, parameters):
    """Retain the declared local end envelope of one extrusion."""
    def build(graph, stock, geometry, values):
        if values or set(geometry) != {"frame", "boundary"} or not _stock_fields(stock, {"depth"}):
            raise ValueError("端切仅接受实际深度、局部坐标系和保留边界")
        depth = _number(stock["depth"], "母材深度", 1.e-8)
        frame = _frame(geometry["frame"])
        points = _polygon(geometry["boundary"])
        # Clearance belongs to the cutter definition, never to the product's
        # nominal dimensions; retain the full physical extrusion depth.
        tool = graph.prism("end-envelope", [_path(points)], frame, -depth / 2 - 10, depth + 20)
        return tool, "intersect"
    return _result("extrusion-end-clip", process_input, parameters, build)


def through_depth_aperture(process_input, parameters):
    """Drill or slot one measured station across an actual extrusion face."""
    def build(graph, stock, geometry, values):
        if (not _stock_fields(stock, {"length", "width", "depth"}) or
                set(geometry) != {"frame", "station", "across"} or
                set(values) != {"kind", "width", "height"}):
            raise ValueError("贯穿孔槽的局部输入字段无效")
        length = _number(stock["length"], "母材长度", 1.e-8)
        face = _number(stock["width"], "母材装配面宽", 1.e-8)
        depth = _number(stock["depth"], "母材深度", 1.e-8)
        frame = _frame(geometry["frame"])
        station = _number(geometry["station"], "加工站位")
        across = _number(geometry["across"], "跨截面位置")
        width = _number(values["width"], "加工孔宽", 1.e-8)
        height = _number(values["height"], "加工孔高", 1.e-8)
        if values["kind"] not in ("roundThroughDepth", "rectThroughDepth"):
            raise ValueError("包含未支持的加工规则，不能忽略后继续拆单")
        if values["kind"] == "roundThroughDepth" and abs(width - height) > 1.e-8:
            raise ValueError("圆孔宽高必须等于实际直径")
        if (station - height / 2 <= 0 or station + height / 2 >= length or
                across - width / 2 <= 0 or across + width / 2 >= face):
            raise ValueError("加工孔槽超出料件边界")
        contours = [_circle(width / 2) if values["kind"] == "roundThroughDepth" else
                    _path(_rect([-width / 2, width / 2, -height / 2, height / 2]))]
        hole_frame = deepcopy(frame)
        hole_frame["origin"] = _point(frame, [across, station])
        tool = graph.prism("aperture", contours, hole_frame, -depth / 2 - 1, depth + 2)
        return tool, "subtract"
    return _result("through-depth-aperture", process_input, parameters, build)


def surface_feature_cut(process_input, parameters):
    """Cut one local surface motif with boundaries and protected material.

    The same function can be called repeatedly on either side of the same
    stock.  A reversed inward axis selects the other actual stock surface.
    """
    def build(graph, stock, geometry, values):
        if (not _stock_fields(stock, {"thickness"}) or
                set(geometry) != {"frame", "primitive", "boundary", "protectedRegions"} or
                set(values) != {"depth", "through", "grooveWidth", "tool", "vAngle"}):
            raise ValueError("表面加工的局部输入字段无效")
        thickness = _number(stock["thickness"], "母材厚度", 1.e-8)
        depth = _number(values["depth"], "加工深度", 1.e-8, thickness)
        if type(values["through"]) is not bool or values["tool"] not in ("flat", "v"):
            raise ValueError("表面加工方式无效")
        through = values["through"]
        frame = _frame(geometry["frame"])
        boundary = _boundary(geometry["boundary"])
        protected = geometry["protectedRegions"]
        if not isinstance(protected, list) or len(protected) > 100:
            raise ValueError("材料保护区域无效")
        protected = [_boundary(region) for region in protected]
        motif = geometry["primitive"]
        if not isinstance(motif, dict):
            raise ValueError("表面加工图元无效")
        actual_depth = thickness + .04 if through else depth + .02
        kind = motif.get("kind")
        if kind == "polygon":
            if set(motif) != {"kind", "points"} or values["tool"] != "flat":
                raise ValueError("区域加工图元字段无效")
            points = _clip(_polygon(motif["points"]), boundary)
            points = _polygon(points)
            mb = [min(v[0] for v in points), max(v[0] for v in points),
                  min(v[1] for v in points), max(v[1] for v in points)]
            tool = graph.prism("surface", [_path(points)], frame, -.02, actual_depth)
        elif kind == "ring":
            if set(motif) != {"kind", "center", "radius"} or values["tool"] != "flat":
                raise ValueError("圆环加工图元字段无效")
            center = _vector(motif["center"], "加工圆环中心", 2)
            radius = _number(motif["radius"], "加工圆环半径", 1.e-8)
            groove = _number(values["grooveWidth"], "槽宽", 1.e-8, 2 * radius - 1.e-8)
            ring_frame = deepcopy(frame)
            ring_frame["origin"] = _point(frame, center)
            tool = graph.prism("surface", [_circle(radius + groove / 2),
                                            _circle(radius - groove / 2)], ring_frame, -.02, actual_depth)
            r = radius + groove / 2
            mb = [center[0] - r, center[0] + r, center[1] - r, center[1] + r]
        elif kind == "line":
            if set(motif) != {"kind", "a", "b"}:
                raise ValueError("线槽加工图元字段无效")
            a, b = (_vector(motif[name], "线槽端点", 2) for name in ("a", "b"))
            length = math.dist(a, b)
            if length < .01:
                raise ValueError("线槽长度不足")
            direction = [(b[i] - a[i]) / length for i in range(2)]
            normal = [-direction[1], direction[0]]
            if values["tool"] == "v":
                if through:
                    raise ValueError("V 形线槽须明确保留底部材料")
                angle = _number(values["vAngle"], "V 形槽夹角", 30, 120)
                groove = 2 * depth * math.tan(math.radians(angle / 2))
                half = (depth + .02) * math.tan(math.radians(angle / 2))
                section = graph.node("surface.section", "profile2d", arguments={
                    "placement": {"origin": _point(frame, a),
                                  "xAxis": [frame["xAxis"][i] * normal[0] + frame["yAxis"][i] * normal[1] for i in range(3)],
                                  "yAxis": list(frame["depthAxis"])},
                    "contours": [_path([[-half, -.02], [half, -.02], [0, depth]])]})
                tool = graph.node("surface.solid", "extrude", [section], {
                    "vector": [frame["xAxis"][i] * (b[0] - a[0]) + frame["yAxis"][i] * (b[1] - a[1]) for i in range(3)]})
            else:
                groove = _number(values["grooveWidth"], "槽宽", 1.e-8)
                points = [[q[i] + normal[i] * offset for i in range(2)]
                          for q, offset in [(a, -groove / 2), (b, -groove / 2), (b, groove / 2), (a, groove / 2)]]
                tool = graph.prism("surface", [_path(points)], frame, -.02, actual_depth)
            mb = [min(a[0], b[0]) - groove / 2, max(a[0], b[0]) + groove / 2,
                  min(a[1], b[1]) - groove / 2, max(a[1], b[1]) + groove / 2]
        else:
            raise ValueError("未支持的表面加工图元")
        if not _overlap(mb, boundary):
            raise ValueError("表面加工图元不在实际加工边界内")
        keep = graph.prism("boundary", [_path(_rect(boundary))], frame, -.1, thickness + .2)
        tool = graph.boolean("surface.clipped", tool, [keep], "intersect")
        if not through and protected:
            reserved = [graph.prism("protect." + str(index), [_path(_rect(region))], frame, -.1, thickness + .2)
                        for index, region in enumerate(protected) if _overlap(mb, region)]
            tool = graph.boolean("surface.safe", tool, reserved, "subtract")
        return tool, "subtract"
    return _result("surface-feature-cut", process_input, parameters, build)


FUNCTIONS = {"extrusion-end-clip": extrusion_end_clip,
             "through-depth-aperture": through_depth_aperture,
             "surface-feature-cut": surface_feature_cut}
