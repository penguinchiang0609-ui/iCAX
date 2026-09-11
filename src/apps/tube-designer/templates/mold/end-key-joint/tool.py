"""A rectangular, through-thickness tongue/slot joint, not an arc end cap.

Both use the same nominal width/depth. To mate a male start with a female end,
place the female outer end at the male shoulder (male tip + nominal depth).
The two retained volumes are complementary at zero clearance. Only the female
slot grows when assembly clearances are requested.
"""
import math


def generate(p, context):
    lo, hi = context["bounds"]["min"], context["bounds"]["max"]
    place = context["placement"]
    length = hi[0] - lo[0]
    if place["datum"] != "long":
        raise ValueError("插舌/插槽配合口使用原端长点定位，不支持猜测非平面端的中心或短点")
    if not math.isfinite(place["trim"]) or place["trim"] < 0 or place["trim"] >= length:
        raise ValueError("端部修剪量须小于母材长度")
    rotation = math.radians(place["rotation"])
    half_width = (abs(math.cos(rotation)) * (hi[1]-lo[1]) + abs(math.sin(rotation)) * (hi[2]-lo[2])) / 2
    female = p["gender"] == "female"
    width = p["width"] + (2*p["sideClearance"] if female else 0)
    depth = p["depth"] + (p["axialClearance"] if female else 0)
    offset = place.get("offset", 0)
    if abs(offset) + width/2 >= half_width - 0.001:
        raise ValueError("插舌/插槽须位于截面横向范围内，并在两侧保留肩部")
    if place["trim"] + depth >= length - 0.001:
        raise ValueError("配合口深度与修剪量之和须小于母材长度")
    reach = 4 * (sum(hi[i]-lo[i] for i in range(3)) + 10)
    nodes = []

    def box(key, x0, x1, y0, y1):
        nodes.append({"key":key+"-profile","operator":"profile2d","arguments":{
            "placement":{"origin":[(x0+x1)/2,(y0+y1)/2,-reach],"xAxis":[1,0,0],"yAxis":[0,1,0]},
            "contours":[{"kind":"roundedRectangle","width":x1-x0,"height":y1-y0,"radius":0}]}})
        nodes.append({"key":key,"operator":"extrude","inputs":[key+"-profile"],"arguments":{"vector":[0,0,2*reach]}})

    box("outer-slab", -reach, 0 if female else depth, -reach, reach)
    box("joint-band", 0, depth, offset-width/2, offset+width/2)
    nodes.append({"key":"tool","operator":"boolean","inputs":["outer-slab","joint-band"],
        "arguments":{"operation":"union" if female else "subtract"}})
    return {"mode":"solid","coordinateSpace":"end-local","outputKey":"tool","datumCenter":None,
        "requireEndContact":True,"requireRetainedEndMaterial":True,
        "model":{"schema":"icax.neutral-model","schemaVersion":1,
            "template":{"id":"end-key-joint","version":"1.0.0","packageDigest":"self-contained"},"geometry":nodes}}
