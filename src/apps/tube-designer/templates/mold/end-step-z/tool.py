"""One rectangular Z/step interface in an axial side view.

One side ends at local X=0 and the other at X=depth, joined at Y=splitOffset.
This explicitly defined single-step lap is not every industry's 'Z cut'.
"""
import math

def _rect_path(width, height):
    p=[[-width/2,-height/2],[width/2,-height/2],[width/2,height/2],[-width/2,height/2]]
    return {"kind":"path","closed":True,"segments":[{"kind":"line","start":p[i],"end":p[(i+1)%4]} for i in range(4)]}


def generate(p, context):
    lo, hi = context["bounds"]["min"], context["bounds"]["max"]
    place = context["placement"]
    length = hi[0]-lo[0]
    if not math.isfinite(place["trim"]) or place["trim"] < 0 or place["trim"]+p["depth"] >= length-0.001:
        raise ValueError("阶差与修剪量之和须小于母材长度")
    local = section_geometry.local_section(section_geometry.from_profile(context["targetSection"]), place["rotation"])
    box_bounds = section_geometry.bounds(next(loop for loop in local["contours"] if not loop["inner"]))
    if not box_bounds["min"][0]+0.001 < p["splitOffset"] < box_bounds["max"][0]-0.001:
        raise ValueError("阶梯分界须位于截面横向范围内部")
    reach = 4*(sum(hi[i]-lo[i] for i in range(3))+10)
    nodes = []

    def box(key, x0, x1, y0, y1):
        nodes.append({"key":key+"-profile","operator":"profile2d","arguments":{
            "placement":{"origin":[(x0+x1)/2,(y0+y1)/2,-reach],"xAxis":[1,0,0],"yAxis":[0,1,0]},
            "contours":[_rect_path(x1-x0,y1-y0)]}})
        nodes.append({"key":key,"operator":"extrude","inputs":[key+"-profile"],"arguments":{"vector":[0,0,2*reach]}})

    box("outer-slab", -reach, 0, -reach, reach)
    box("step", 0, p["depth"], p["splitOffset"] if p["hand"]=="positive" else -reach,
        reach if p["hand"]=="positive" else p["splitOffset"])
    nodes.append({"key":"tool","operator":"boolean","inputs":["outer-slab","step"],"arguments":{"operation":"union"}})
    return {"mode":"solid","coordinateSpace":"end-local","outputKey":"tool","datumCenter":None,
        "requireEndContact":True,"requireRetainedEndMaterial":True,
        "model":{"schema":"icax.neutral-model","schemaVersion":1,
            "template":{"id":"end-step-z","version":"1.0.0","packageDigest":"self-contained"},"geometry":nodes}}
