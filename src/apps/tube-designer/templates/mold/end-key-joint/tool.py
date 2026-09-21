"""A through-thickness tongue/slot joint with a semicircular insertion end.

Both use the same nominal width and straight depth. To mate a male start with a
female end, place the female outer end at the male shoulder, one total insertion
depth (straight depth + half the nominal width) from the male outer tip.
The two retained volumes are complementary at zero clearance. Only the female
slot grows when assembly clearances are requested. Equal side and axial
clearances form an exact uniform offset of the nominal waist-shaped profile.
"""
import math

def _rect_path(width, height):
    p=[[-width/2,-height/2],[width/2,-height/2],[width/2,height/2],[-width/2,height/2]]
    return {"kind":"path","closed":True,"segments":[{"kind":"line","start":p[i],"end":p[(i+1)%4]} for i in range(4)]}


def _waist_end_path(straight_depth, width, rounded_at_start=False):
    radius = width / 2
    total_depth = straight_depth + radius
    if rounded_at_start:
        start = [total_depth, -radius]
        segments = []
        if straight_depth > 1e-9:
            segments.append({"kind":"line", "start":start, "end":[radius,-radius]})
        segments.append({"kind":"arc", "start":[radius,-radius], "middle":[0,0],
            "end":[radius,radius]})
        if straight_depth > 1e-9:
            segments.append({"kind":"line", "start":[radius,radius], "end":[total_depth,radius]})
        segments.append({"kind":"line", "start":[total_depth,radius], "end":start})
        return {"kind":"path", "closed":True, "segments":segments}

    start = [straight_depth, -radius]
    segments = []
    if straight_depth > 1e-9:
        segments.append({"kind":"line", "start":[0,-radius], "end":start})
    segments.append({"kind":"arc", "start":start, "middle":[straight_depth+radius,0],
        "end":[straight_depth,radius]})
    if straight_depth > 1e-9:
        segments.append({"kind":"line", "start":[straight_depth,radius], "end":[0,radius]})
    segments.append({"kind":"line", "start":[0,radius], "end":[0,-radius]})
    return {"kind":"path", "closed":True, "segments":segments}


def generate(p, context):
    lo, hi = context["bounds"]["min"], context["bounds"]["max"]
    place = context["placement"]
    length = hi[0] - lo[0]
    if not math.isfinite(place["trim"]) or place["trim"] < 0 or place["trim"] >= length:
        raise ValueError("端部修剪量须小于母材长度")
    local = section_geometry.local_section(section_geometry.from_profile(context["targetSection"]), place["rotation"])
    box_bounds = section_geometry.bounds(next(loop for loop in local["contours"] if not loop["inner"]))
    female = p["gender"] == "female"
    side_clearance = p["sideClearance"] if female else 0
    axial_clearance = p["axialClearance"] if female else 0
    width = p["width"] + 2*side_clearance
    radius = width / 2
    nominal_total_depth = p["straightDepth"] + p["width"] / 2
    total_depth = nominal_total_depth + axial_clearance
    straight_depth = total_depth - radius
    if straight_depth < -1e-9:
        raise ValueError("母口每侧间隙不得大于直线段深度与根部间隙之和")
    straight_depth = max(0, straight_depth)
    offset = place.get("offset", 0)
    if offset-width/2 <= box_bounds["min"][0]+0.001 or offset+width/2 >= box_bounds["max"][0]-0.001:
        raise ValueError("插舌/插槽须位于截面横向范围内，并在两侧保留肩部")
    if place["trim"] + total_depth >= length - 0.001:
        raise ValueError("腰形配合口总深度与修剪量之和须小于母材长度")
    reach = 4 * (sum(hi[i]-lo[i] for i in range(3)) + 10)
    nodes = []

    def box(key, x0, x1, y0, y1):
        nodes.append({"key":key+"-profile","operator":"profile2d","arguments":{
            "placement":{"origin":[(x0+x1)/2,(y0+y1)/2,-reach],"xAxis":[1,0,0],"yAxis":[0,1,0]},
            "contours":[_rect_path(x1-x0,y1-y0)]}})
        nodes.append({"key":key,"operator":"extrude","inputs":[key+"-profile"],"arguments":{"vector":[0,0,2*reach]}})

    box("outer-slab", -reach, 0 if female else total_depth, -reach, reach)
    nodes.append({"key":"joint-band-profile","operator":"profile2d","arguments":{
        "placement":{"origin":[0,offset,-reach],"xAxis":[1,0,0],"yAxis":[0,1,0]},
        "contours":[_waist_end_path(straight_depth,width,rounded_at_start=not female)]}})
    nodes.append({"key":"joint-band","operator":"extrude","inputs":["joint-band-profile"],
        "arguments":{"vector":[0,0,2*reach]}})
    nodes.append({"key":"tool","operator":"boolean","inputs":["outer-slab","joint-band"],
        "arguments":{"operation":"union" if female else "subtract"}})
    return {"mode":"solid","coordinateSpace":"end-local","outputKey":"tool","datumCenter":None,
        "requireEndContact":True,"requireRetainedEndMaterial":True,
        "model":{"schema":"icax.neutral-model","schemaVersion":1,
            "template":{"id":"end-key-joint","version":"1.0.0","packageDigest":"self-contained"},"geometry":nodes}}
