import math

def _rect_path(width, height):
    p=[[-width/2,-height/2],[width/2,-height/2],[width/2,height/2],[-width/2,height/2]]
    return {"kind":"path","closed":True,"segments":[{"kind":"line","start":p[i],"end":p[(i+1)%4]} for i in range(4)]}

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
              _rect_path(2*reach, 2*reach*norm),
              [-sign*reach,0,0])
    slope = math.tan(math.radians(p["angle"]))
    local = section_geometry.local_section(section_geometry.from_profile(context["targetSection"]), place["rotation"])
    box = section_geometry.bounds(next(loop for loop in local["contours"] if not loop["inner"]))
    low, high = sorted((slope*box["min"][0], slope*box["max"][0]))
    # Long/short points belong to the actual contour, not its rotated box.
    datum = anchor
    if place["datum"] == "long": datum -= low if start else high
    elif place["datum"] == "short": datum -= high if start else low
    far_point = datum + (high if start else low)
    if sign * (far_point - (lo[0] if start else hi[0])) >= hi[0]-lo[0]:
        raise ValueError("斜切角度与修剪量会切穿零件另一端，请减小角度或修剪量")
    slab("tool", datum, slope)
    return {"mode": "solid", "coordinateSpace": "part", "outputKey": "tool", "datumCenter": datum,
            "model": {"schema": "icax.neutral-model", "schemaVersion": 1,
                      "template": {"id": "punch-end", "version": "1.0.0", "packageDigest": "self-contained"},
                      "geometry": nodes}}
