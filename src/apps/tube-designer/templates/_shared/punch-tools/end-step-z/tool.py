"""One rectangular Z/step interface in an axial side view.

One side ends at local X=0 and the other at X=depth, joined at Y=splitOffset.
This explicitly defined single-step lap is not every industry's 'Z cut'.
"""
import math


def generate(p, context):
    lo, hi = context["bounds"]["min"], context["bounds"]["max"]
    place = context["placement"]
    length = hi[0]-lo[0]
    if place["datum"] != "long":
        raise ValueError("单台阶 Z 搭接口使用原端长点定位，不支持中心或短点基准")
    if not math.isfinite(place["trim"]) or place["trim"] < 0 or place["trim"]+p["depth"] >= length-0.001:
        raise ValueError("阶差与修剪量之和须小于母材长度")
    rotation = math.radians(place["rotation"])
    half_width = (abs(math.cos(rotation))*(hi[1]-lo[1])+abs(math.sin(rotation))*(hi[2]-lo[2]))/2
    if abs(p["splitOffset"]) >= half_width-0.001:
        raise ValueError("阶梯分界须位于截面横向范围内部")
    reach = 4*(sum(hi[i]-lo[i] for i in range(3))+10)
    nodes = []

    def box(key, x0, x1, y0, y1):
        nodes.append({"key":key+"-profile","operator":"profile2d","arguments":{
            "placement":{"origin":[(x0+x1)/2,(y0+y1)/2,-reach],"xAxis":[1,0,0],"yAxis":[0,1,0]},
            "contours":[{"kind":"roundedRectangle","width":x1-x0,"height":y1-y0,"radius":0}]}})
        nodes.append({"key":key,"operator":"extrude","inputs":[key+"-profile"],"arguments":{"vector":[0,0,2*reach]}})

    box("outer-slab", -reach, 0, -reach, reach)
    box("step", 0, p["depth"], p["splitOffset"] if p["hand"]=="positive" else -reach,
        reach if p["hand"]=="positive" else p["splitOffset"])
    nodes.append({"key":"tool","operator":"boolean","inputs":["outer-slab","step"],"arguments":{"operation":"union"}})
    return {"mode":"solid","coordinateSpace":"end-local","outputKey":"tool","datumCenter":None,
        "requireEndContact":True,"requireRetainedEndMaterial":True,
        "model":{"schema":"icax.neutral-model","schemaVersion":1,
            "template":{"id":"end-step-z","version":"1.0.0","packageDigest":"self-contained"},"geometry":nodes}}
