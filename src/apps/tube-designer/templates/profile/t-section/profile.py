"""P23 T 型钢：族内模型独立实现。"""
import importlib
import json
from .geometry import envelope, swap
MODELS = {"t-split":"model_0","t-welded":"model_1"}
PARAMETERS = {"t-split":{"width":"width","depth":"depth","wallThickness":"wallThickness","flangeThickness":"model0FlangeThickness","webOffset":"model0WebOffset","rootRadius":"model0RootRadius","toeRadius":"model0ToeRadius"},"t-welded":{"width":"width","depth":"depth","wallThickness":"wallThickness","flangeThickness":"model1FlangeThickness","webOffset":"model1WebOffset","weldLeg":"model1WeldLeg"}}

def selected(p):
    name=p.get("sectionModel","t-split")
    if name not in MODELS: raise ValueError("不支持的轮廓子模型")
    module=importlib.import_module("." + MODELS[name], __package__)
    values={key:p[value] for key,value in PARAMETERS[name].items()}
    return name,module,values

def build(p):
    name,module,values=selected(p)
    actual=p.get("materialBoundary","").strip()
    if p["geometrySource"] != "idealizedFallback" and not actual: raise ValueError("所选来源必须提供完整材料边界")
    if actual and p["geometrySource"] == "idealizedFallback": raise ValueError("请为实际轮廓指定来源")
    if actual:
        if not p.get("sourceRevision","").strip(): raise ValueError("实际轮廓必须填写来源及版本")
        curves=json.loads(actual)
        if not isinstance(curves,list) or not curves: raise ValueError("材料边界必须为非空轮廓数组")
        w,h=envelope(curves)
        result={"width":w,"depth":h,"wallThickness":0,"cornerRadius":0,"contours":curves}
    else:
        result=module.build(values)
    result["manufacturingRoute"] = {"t-split":"HotRolled","t-welded":"Welded"}[name]
    result.update({"kind":"t-section","_familyParameters":dict(p),"specification":"T 型钢"+" "+str(result["width"])+" × "+str(result["depth"])+" mm","geometrySource":p.get("geometrySource","idealizedFallback"),"sourceRevision":p.get("sourceRevision",""),"sectionModel":name})
    return result
