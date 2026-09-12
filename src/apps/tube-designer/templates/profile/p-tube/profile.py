"""明确的单安装翼 P-Tube 参数模型，参考厂家公开结构，不复制9706规格。台阶闭口等其他P型用有图纸依据的完整材料边界，不按字母猜测。预制孔不属于恒截面母材。"""
import copy
import json
import math
from .geometry import polygon, box, envelope, swap, positive

def shape(p):
    w,h,t,f,s=[p[k] for k in ("width","depth","wallThickness","flangeLength","flangeThickness")]
    positive(w-2*t,h-2*t,t,f,s)
    if s>t: raise ValueError("安装翼厚不得侵入内孔")
    left,right=-w/2,w/2
    pts=[[left,-f],[left+s,-f],[left+s,0],[right,0],[right,h],[left,h]]
    return [polygon(pts,[0,0,0,p["cornerRadius"],p["cornerRadius"],p["cornerRadius"]]),box(w-2*t,h-2*t,0,h/2,[p["innerRadius"]]*4)]

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
    return {"kind":"p-tube","width":w,"depth":h,"wallThickness":p["wallThickness"],"cornerRadius":p.get("cornerRadius",0),"contours":loops,"geometrySource":p["geometrySource"],"sourceRevision":p["sourceRevision"],"specification":"P型管"+f" {w:g} × {h:g} mm"}

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
