"""上下翼缘宽度、厚度和腹板偏心独立。参数化名义截面，非标准规格；真实来料使用完整边界。"""
import copy
import json
import math
from .geometry import polygon, box, envelope, swap, positive

def shape(p):
    w,wb,h,t,f,b,x=[p[k] for k in ("width","lowerWidth","depth","wallThickness","flangeThickness","lowerThickness","webOffset")]
    positive(min(w,wb)-t-2*abs(x),h-f-b,t,f,b)
    pts=[[-wb/2,-h/2],[wb/2,-h/2],[wb/2,-h/2+b],[x+t/2,-h/2+b],[x+t/2,h/2-f],[w/2,h/2-f],[w/2,h/2],[-w/2,h/2],[-w/2,h/2-f],[x-t/2,h/2-f],[x-t/2,-h/2+b],[-wb/2,-h/2+b]]
    return [polygon(pts,[p["rootRadius"] if i in (3,4,9,10) else p["toeRadius"] if i in (2,5,8,11) else 0 for i in range(12)])]

def build(parameters):
    p=dict(parameters)
    actual=p["materialBoundary"].strip()
    if p["geometrySource"] != "idealizedFallback":
        if not actual or not p["sourceRevision"].strip():raise ValueError("实际轮廓须提供完整材料边界及来源版本")
        loops=json.loads(actual)
        if not isinstance(loops,list) or not loops:raise ValueError("材料边界须为非空轮廓数组")
    else:
        # Supplied-boundary draft is retained but inactive in idealized mode.
        loops=shape(p)
    if p["mirrorX"]:
        loops=reflect_x(loops)
    w,h=envelope(loops)
    return {"kind":"unequal-i","width":w,"depth":h,"wallThickness":p["wallThickness"],"cornerRadius":p.get("cornerRadius",0),"contours":loops,"geometrySource":p["geometrySource"],"sourceRevision":p["sourceRevision"],"specification":"不等翼工字钢"+f" {w:g} × {h:g} mm"}

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
