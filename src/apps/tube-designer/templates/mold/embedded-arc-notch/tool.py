"""V cutter minus a root-tangent cylinder: an embedded circular tongue.

The circle centre is (R, root), not a rounded V root or a release hole.
Its second intersection with the right V flank is
(R*(1-cos(beta)), root+R*sin(beta)). No plastic forming is claimed.
"""
import math


def analyze(parameters, section, context):
    try:
        data = section_geometry.closed_shell_metrics(section, context.get("placement", {}).get("rotation", 0))
        tol = data["tolerance"]
        for loop in data["contours"]:
            lines = [e for e in loop["edges"] if e["kind"] == "line"]
            if (len(lines) != 4 or
                sum(abs(e["start"][0]-e["end"][0]) <= tol for e in lines) != 2 or
                sum(abs(e["start"][1]-e["end"][1]) <= tol for e in lines) != 2):
                raise ValueError("嵌入圆弧需要两对正交平直壁的闭口截面")
            for edge in loop["edges"]:
                if edge["kind"] != "line" and (edge["kind"] != "circleArc" or
                    abs(abs(edge["last"]-edge["first"])-math.pi/2)*edge["radius"] > tol):
                    raise ValueError("截面角部必须为四分之一圆弧")
        return {"applicable": True, "reason": "", "data": data,
                "derivedParameters": {"wallThickness": data["wallThickness"]}}
    except ValueError as error:
        return {"applicable": False, "reason": str(error), "data": {}}


def generate(p, context):
    data = context["analysis"]
    lo, hi = data["outside"]["min"], data["outside"]["max"]
    root = data["inside"]["min"][1] + p["rootClearance"]
    height = hi[1]-root
    beta = math.radians(p["angle"])
    # This is the local retained-circle radius of the notch.  It is not the
    # finished assembly's bend/centreline radius.  Keeping a distinct key
    # prevents assembly templates from binding two unrelated dimensions just
    # because both used to be named bendRadius.
    radius = p["arcRadius"]
    if height <= 0 or radius <= 0 or not 0 < beta < math.pi:
        raise ValueError("角度、圆弧半径或槽根高度无效")
    # Keep the complete embedded arc below the top wall. For obtuse angles
    # its highest point is R, not the flank intersection R*sin(beta).
    arc_height = radius*(math.sin(beta) if beta <= math.pi/2 else 1)
    if arc_height >= height-data["tolerance"]:
        raise ValueError("嵌入圆弧超出可切高度，请减小半径或槽根距离")
    top = hi[1]
    half_width = (top-root)*math.tan(beta/2)
    mirror = 1 if p["rightArc"] else -1
    nodes = []

    def prism(key, segments):
        for edge in segments:
            for name in ("start", "middle", "end"):
                if name in edge:
                    edge[name][0] *= mirror
        nodes.extend([
            {"key": key+"-profile", "operator": "profile2d", "arguments": {
                "placement": {"origin": [0, hi[0], 0], "xAxis": [1,0,0], "yAxis": [0,0,1]},
                "contours": [{"kind": "path", "segments": segments}]}},
            {"key": key, "operator": "extrude", "inputs": [key+"-profile"],
             "arguments": {"vector": [0, -(hi[0]-lo[0]), 0]}}])

    points = [[-half_width, top], [half_width, top], [0, root]]
    male_size = 0.0
    fit_clearance = 0.0
    insertion_depth = 0.0
    pre_deflection = 0.0
    if p.get("maleFemale", False):
        male_size = p.get("maleFemaleSize", 0) or data["wallThickness"]
        fit_clearance = p.get("fitClearance", 0)
        insertion_depth = p.get("insertionDepth", 0) or male_size
        pre_deflection = p.get("preDeflection", 0)
        if male_size < data["wallThickness"]:
            raise ValueError("公母尺寸不能小于主管实际壁厚")
        if fit_clearance < 0 or insertion_depth <= 0 or pre_deflection < 0:
            raise ValueError("配合间隙、插入深度或预压量无效")
        transition = hi[1]-insertion_depth
        # Use the established V-notch top step, entirely above the retained
        # arc. Do not trim the circular tongue or silently reduce its radius.
        if transition <= root+arc_height+data["tolerance"]:
            raise ValueError("公母台阶与嵌入圆弧重叠，请减小插入深度或圆弧半径")
        run = (transition-root)*math.tan(beta/2)
        # The receiving side is wider by twice the single-side clearance.
        # Pre-deflection is a forming-path instruction and deliberately does
        # not distort this nominal two-dimensional cutting boundary.
        female_size = male_size + 2*fit_clearance
        points = [[-run,transition],[-run,top],[run+female_size,top],
                  [run+female_size,transition],[run,transition],[0,root]]
    prism("v-base", [{"kind": "line", "start": list(a), "end": list(b)}
                     for a,b in zip(points, points[1:]+points[:1])])
    prism("retained-circle", [
        {"kind": "arc", "start": [0,root], "middle": [radius,root+radius], "end": [2*radius,root]},
        {"kind": "arc", "start": [2*radius,root], "middle": [radius,root-radius], "end": [0,root]}])
    nodes.append({"key": "notch", "operator": "boolean", "inputs": ["v-base", "retained-circle"],
                  "arguments": {"operation": "subtract"}})
    return {"mode": "solid", "coordinateSpace": "part-local", "outputKey": "notch",
            "calculation": {"bendAngle": p["angle"], "finalIncludedAngle": 180-p["angle"],
                "arcConstraint": "RootTangentCircleIntersectFlank", "arcRadius": radius,
                "circleCenter": [mirror*radius,root],
                "flankIntersection": [mirror*radius*(1-math.cos(beta)),root+radius*math.sin(beta)],
                "maleSize": male_size, "femaleSize": male_size+2*fit_clearance if male_size else 0,
                "fitClearance": fit_clearance, "insertionDepth": insertion_depth,
                "preDeflection": pre_deflection,
                "assemblyPath": "pre-deflect-then-insert" if pre_deflection > 0 else "bend-then-insert",
                "rootReference": "inner", "formingValidation": "not-performed"},
            "model": {"schema": "icax.neutral-model", "schemaVersion": 1,
            "template": {"id": "embedded-arc-notch", "version": "2.2.0", "packageDigest": "self-contained"},
                "geometry": nodes}}
