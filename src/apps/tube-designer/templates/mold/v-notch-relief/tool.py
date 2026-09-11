import math


def generate(p, context):
    lo, hi = context["bounds"]["min"], context["bounds"]["max"]; feature = context["feature"]
    station, reference = feature.get("station", 0), feature.get("reference", "start")
    if isinstance(station, bool) or not isinstance(station, (int, float)) or not math.isfinite(station) or reference not in ("start", "end", "center"):
        raise ValueError("V 槽定位无效")
    x = lo[0] + station if reference == "start" else hi[0] - station if reference == "end" else (lo[0] + hi[0]) / 2 + station
    placement = context.get("placement", {})
    rotation = math.radians(placement.get("rotation", 0)); sr, cr = math.sin(rotation), math.cos(rotation)
    h = (hi[1] - lo[1]) * abs(sr) / 2 + (hi[2] - lo[2]) * abs(cr) / 2; span = (hi[1] - lo[1]) * abs(cr) + (hi[2] - lo[2]) * abs(sr) + 2
    if p["bridge"] >= 2 * h: raise ValueError("底部保留厚度须小于该方向截面高度")
    bottom, top = -h + p["bridge"], h + 1; depth = top - bottom; half = math.radians(p["angle"]) / 2
    points = [[-depth * math.tan(half), top], [0, bottom], [depth * math.tan(half), top]]
    contour = {"kind": "path", "segments": [{"kind": "line", "start": a, "end": b} for a, b in zip(points, points[1:] + points[:1])]}
    origin = [x, (lo[1] + hi[1]) / 2 + cr * span / 2, (lo[2] + hi[2]) / 2 - sr * span / 2]; nodes = []

    def prism(key, shape, lift=0):
        o = [origin[0], origin[1] + sr * lift, origin[2] + cr * lift]
        nodes.append({"key": key + "-profile", "operator": "profile2d", "arguments": {"placement": {"origin": o, "xAxis": [1, 0, 0], "yAxis": [0, sr, cr]}, "contours": [shape]}})
        nodes.append({"key": key, "operator": "extrude", "inputs": [key + "-profile"], "arguments": {"vector": [0, -cr * span, sr * span]}})
    prism("notch", contour); cutters = ["notch"]
    radius = p["holeDiameter"] / 2
    circle = {"kind": "path", "segments": [{"kind": "arc", "start": [-radius, 0], "middle": [0, -radius], "end": [radius, 0]}, {"kind": "arc", "start": [radius, 0], "middle": [0, radius], "end": [-radius, 0]}]}
    prism("relief", circle, bottom + p["holeLift"]); cutters.append("relief")
    if p["bottomCut"]:
        half_width = p["bottomCutWidth"] / 2; cut = [[-half_width, -h - 1], [half_width, -h - 1], [half_width, bottom + 1], [-half_width, bottom + 1]]
        prism("bottom-cut", {"kind": "path", "segments": [{"kind": "line", "start": a, "end": b} for a, b in zip(cut, cut[1:] + cut[:1])]}); cutters.append("bottom-cut")
    nodes.append({"key": "tool", "operator": "boolean", "inputs": cutters, "arguments": {"operation": "union"}})
    return {"mode": "solid", "coordinateSpace": "part", "outputKey": "tool", "model": {"schema": "icax.neutral-model", "schemaVersion": 1, "template": {"id": "v-notch-relief", "version": "1.0.0", "packageDigest": "self-contained"}, "geometry": nodes}}
