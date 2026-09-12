"""依据Corus厂家手册第8页的单球头名义轮廓：30度斜边、球头与根部独立相切圆弧、端角r1，不是扁钢加半圆。默认120×6，c=17、r=5；r1为可编辑制造参数。实际轮廓可完整覆盖。"""
import copy
import json
import math
from .geometry import polygon, box, envelope, swap, positive

def shape(p):
    b,t,c,r,r1=[p[k] for k in ("width","wallThickness","bulbProjection","bulbRadius","edgeRadius")]
    positive(b,t,c,r)
    if 2*r1>=t: raise ValueError("端角重叠：2r1必须小于腹板厚")
    # Intersection of the top tangent and 30-degree sloping tangent.
    # Filleting this vertex moves the extreme x back to t+c.
    tip=t+c+r*(1/math.tan(math.pi/12)-1)
    d=(tip-t)*math.tan(math.pi/6)
    positive(b-d)
    pts=[[0,0],[t,0],[t,b-d],[tip,b],[0,b]]
    return [polygon(pts,[r1,r1,r,r,r1])]

def build(parameters):
    p=dict(parameters)
    actual=p["materialBoundary"].strip()
    if p["geometrySource"] != "idealizedFallback":
        if not actual or not p["sourceRevision"].strip():raise ValueError("实际轮廓须提供完整材料边界及来源版本")
        loops=json.loads(actual)
        if not isinstance(loops,list) or not loops:raise ValueError("材料边界须为非空轮廓数组")
    else:
        if actual:raise ValueError("已提供完整轮廓，请明确选择其来源")
        loops=shape(p)
    if p["mirrorX"]:
        loops=reflect_x(loops)
    w,h=envelope(loops)
    return {"kind":"bulb-flat","width":w,"depth":h,"wallThickness":p["wallThickness"],"cornerRadius":p.get("cornerRadius",0),"contours":loops,"geometrySource":p["geometrySource"],"sourceRevision":p["sourceRevision"],"specification":"球扁钢"+f" {w:g} × {h:g} mm"}

def reflect_x(loops):
    result=copy.deepcopy(loops)
    for c in result:
        if c["kind"]=="polygon":c["points"]=[[-x,y] for x,y in c["points"]]
        elif c["kind"]=="path":
            for e in c["segments"]:
                for key in ("start","middle","end","center"):
                    if key in e:e[key]=[-e[key][0],e[key][1]]
                for key in ("controlPoints","poles"):
                    if key in e:e[key]=[[-x,y] for x,y in e[key]]
                if e["kind"]=="ellipseArc":
                    e["rotation"]=math.pi-e.get("rotation",0)
                    e["startAngle"],e["endAngle"]=-e["startAngle"],-e["endAngle"]
    return result
