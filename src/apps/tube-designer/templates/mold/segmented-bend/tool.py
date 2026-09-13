"""Discrete bend, with explicit radius datum and compensated slot clearance."""
import math


def analyze(parameters, section, context):
    try:
        data = section_geometry.closed_shell_metrics(section, context.get("placement", {}).get("rotation", 0))
        return {"applicable": True, "reason": "", "data": data,
                "derivedParameters": {"wallThickness": data["wallThickness"]}}
    except ValueError as error:
        return {"applicable": False, "reason": str(error), "data": {}}


def generate(p, context):
    data = context["analysis"]
    lo, hi = data["outside"]["min"], data["outside"]["max"]
    root = data["inside"]["min"][1] + p["rootClearance"]
    height = hi[1]-root
    half_height = (hi[1]-lo[1])/2
    radius = p["bendRadius"] + (half_height if p["radiusDatum"] == "inner" else 0)
    if height <= 0 or radius <= half_height:
        raise ValueError("槽根超出顶部或中心线半径不大于截面半高")
    beta = math.radians(p["angle"])
    if not 0 < beta < math.pi:
        raise ValueError("总折弯角须介于 0 与 180°")
    count = p["segmentCount"]
    if p["countMode"] == "tolerance":
        error = p["maximumChordError"]
        if error <= 0:
            raise ValueError("自动分段的最大弓高误差必须大于 0")
        # asin form avoids catastrophic cancellation at very small error/R.
        step = 4*math.asin(math.sqrt(min(1, error/(2*radius))))
        if step <= 0:
            raise ValueError("弓高误差过小")
        count = max(2, math.ceil(beta/step))
    if isinstance(count, bool) or int(count) != count or not 2 <= count <= 64:
        raise ValueError("分段槽数必须为 2 至 64；当前误差要求可能过小")
    count = int(count)
    delta = beta/count
    pitch = 2*radius*math.sin(delta/2)
    chord_error = 2*radius*math.sin(delta/4)**2
    # K is an optional process allowance, not a formed-radius solution.
    allowance = p["kFactor"]*data["wallThickness"]*delta if p["bendCompensation"] else 0
    opening = 2*height*math.tan(delta/2)+allowance
    if pitch-opening < p["minimumLand"]:
        raise ValueError("相邻槽的剩余间隔不足，请增大半径、槽数或减小补偿")
    nodes, keys = [], []
    for i in range(count):
        station = (i-(count-1)/2)*pitch
        top = hi[1]+1
        half_width = (top-root)*math.tan(delta/2)+allowance/2
        points = [[-half_width,top], [half_width,top]]
        if allowance > 0:
            points.extend([[allowance/2,root],[-allowance/2,root]])
        else:
            points.append([0,root])
        key = "slot-"+str(i)
        keys.append(key)
        nodes.extend([
            {"key": key+"-profile", "operator": "profile2d", "arguments": {
                "placement": {"origin": [station,hi[0]+1,0], "xAxis": [1,0,0], "yAxis": [0,0,1]},
                "contours": [{"kind": "path", "segments": [
                    {"kind": "line", "start": a, "end": b} for a,b in zip(points,points[1:]+points[:1])]}]}},
            {"key": key, "operator": "extrude", "inputs": [key+"-profile"],
             "arguments": {"vector": [0,-(hi[0]-lo[0]+2),0]}}])
    nodes.append({"key": "slots", "operator": "compound", "inputs": keys, "arguments": {}})
    return {"mode": "solid", "coordinateSpace": "part-local", "outputKey": "slots",
        "calculation": {"bendAngle": p["angle"], "finalIncludedAngle": 180-p["angle"],
            "radiusDatum": p["radiusDatum"], "centerlineRadius": radius,
            "segmentCount": count, "singleNotchAngle": math.degrees(delta),
            "chordPitch": pitch, "chordError": chord_error, "singleNotchOpening": opening,
            "remainingLand": pitch-opening, "singleNotchAllowance": allowance,
            "patternLength": (count-1)*pitch+opening,
            "rootReference": "inner", "formingValidation": "not-performed"},
        "model": {"schema": "icax.neutral-model", "schemaVersion": 1,
            "template": {"id": "segmented-bend", "version": "1.0.0", "packageDigest": "self-contained"},
            "geometry": nodes}}
