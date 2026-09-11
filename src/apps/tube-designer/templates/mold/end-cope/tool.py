import math

def generate(p, context):
    bounds = context["bounds"]
    lo, hi = bounds["min"], bounds["max"]
    place = context["placement"]
    start = context["end"] == "start"
    sign = 1 if start else -1
    trim = place["trim"]
    if not math.isfinite(trim) or trim < 0 or trim >= hi[0] - lo[0]:
        raise ValueError("端部修剪量须小于零件长度")
    if place["datum"] not in ("long", "center", "short"):
        raise ValueError("端面尺寸基准无效")
    rotation = math.radians(place["rotation"])
    cr, sr = math.cos(rotation), math.sin(rotation)
    yc, zc = (lo[1]+hi[1])/2, (lo[2]+hi[2])/2
    anchor = (lo[0] if start else hi[0]) + sign * trim
    reach = 4 * (sum(hi[i]-lo[i] for i in range(3)) + 10)
    nodes = []
    def prism(key, origin, u, v, contour, vector):
        nodes.append({"key": key+"-profile", "operator": "profile2d", "arguments": {
            "placement": {"origin": origin, "xAxis": u, "yAxis": v}, "contours": [contour]}})
        nodes.append({"key": key, "operator": "extrude", "inputs": [key+"-profile"], "arguments": {"vector": vector}})
    def slab(key, c, slope=0):
        norm = math.sqrt(1+slope*slope)
        prism(key, [c,yc,zc], [0,-sr,cr], [slope/norm,cr/norm,sr/norm],
              {"kind": "roundedRectangle", "width": 2*reach, "height": 2*reach*norm, "radius": 0},
              [-sign*reach,0,0])
    def boolean(key, operation, left, right):
        nodes.append({"key": key, "operator": "boolean", "inputs": [left,right], "arguments": {"operation": operation}})
    angle = math.radians(place.get("angle", 90))
    ca, sa = math.cos(angle), math.sin(angle)
    radius = p["diameter"]/2 + p["clearance"]
    if radius <= 0:
        raise ValueError("间隙不能使圆柱半径小于零")
    axis = [ca,sa*cr,sa*sr]
    tangent = [0,-sr,cr]
    cross = [sa,-ca*cr,-ca*sr]
    center = anchor
    # Concave tool subtracts the cylinder from the original extrusion.
    offset = place.get("offset", 0)
    origin = [center-axis[0]*reach,yc-offset*sr-axis[1]*reach,zc+offset*cr-axis[2]*reach]
    prism("cylinder", origin, tangent, cross, {"kind":"circle","radius":radius}, [a*2*reach for a in axis])
    slab("end-slab", anchor)
    boolean("tool", "union", "end-slab", "cylinder")
    datum = None
    return {"mode": "solid", "coordinateSpace": "part", "outputKey": "tool", "datumCenter": datum,
            "model": {"schema": "icax.neutral-model", "schemaVersion": 1,
                      "template": {"id": "punch-end", "version": "1.0.0", "packageDigest": "self-contained"},
                      "geometry": nodes}}
