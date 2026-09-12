"""P02 方矩管：族内模型独立实现。"""
import importlib
import json
from .geometry import envelope, swap
MODELS = {"rect":"model_0"}
PARAMETERS = {"rect":{"width":"width","depth":"depth","wallThickness":"wallThickness","innerWidth":"innerWidth","innerDepth":"innerDepth","innerOffsetX":"innerOffsetX","innerOffsetY":"innerOffsetY","cornerRadius":"cornerRadius","outerRadii":"outerRadii","innerRadii":"innerRadii","outerCorners":"outerCorners","innerCorners":"innerCorners"}}

def selected(p):
    name=p.get("sectionModel","rect")
    if name not in MODELS: raise ValueError("不支持的轮廓子模型")
    module=importlib.import_module("." + MODELS[name], __package__)
    values={key:p[value] for key,value in PARAMETERS[name].items()}
    return name,module,values

def build(p):
    name,module,values=selected(p)
    actual=p.get("materialBoundary","").strip()
    if actual:
        if not p.get("sourceRevision","").strip(): raise ValueError("实际轮廓必须填写来源及版本")
        curves=json.loads(actual)
        if not isinstance(curves,list) or not curves: raise ValueError("材料边界必须为非空轮廓数组")
        w,h=envelope(curves)
        result={"width":w,"depth":h,"wallThickness":0,"cornerRadius":0,"contours":curves}
    else:
        result=module.build(values)
    result["manufacturingRoute"] = {"rect":"Unspecified"}[name]
    result.update({"kind":"rect","_familyParameters":dict(p),"specification":"方矩管"+" "+str(result["width"])+" × "+str(result["depth"])+" mm","geometrySource":"providedBoundary" if actual else "idealizedFallback","sourceRevision":p.get("sourceRevision",""),"sectionModel":name})
    return result
