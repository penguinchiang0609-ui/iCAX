"""P14 角钢：族内模型独立实现。"""
import importlib
import json
from .geometry import envelope, swap
MODELS = {"angle-cold-unequal":"model_0","angle-hot-unequal-thickness":"model_1","angle-precision":"model_2","angle-welded":"model_3"}
PARAMETERS = {"angle-cold-unequal":{"width":"width","depth":"depth","wallThickness":"wallThickness","bendRadius":"model0BendRadius"},"angle-hot-unequal-thickness":{"width":"width","depth":"depth","wallThickness":"wallThickness","flangeThickness":"model1FlangeThickness","rootRadius":"model1RootRadius","heelRadius":"model1HeelRadius","toeRadius":"model1ToeRadius"},"angle-precision":{"width":"width","depth":"depth","wallThickness":"wallThickness","flangeThickness":"model2FlangeThickness"},"angle-welded":{"width":"width","depth":"depth","wallThickness":"wallThickness","flangeThickness":"model3FlangeThickness","weldLeg":"model3WeldLeg"}}

def selected(p):
    name={"ColdBent":"angle-cold-unequal","HotRolled":"angle-hot-unequal-thickness","Precision":"angle-precision","Welded":"angle-welded"}[p["manufacturingRoute"]]
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
    result["manufacturingRoute"] = {"angle-cold-unequal":"ColdBent","angle-hot-unequal-thickness":"HotRolled","angle-precision":"Precision","angle-welded":"Welded"}[name]
    result.update({"kind":"angle","_familyParameters":dict(p),"specification":"角钢"+" "+str(result["width"])+" × "+str(result["depth"])+" mm","geometrySource":p.get("geometrySource","idealizedFallback"),"sourceRevision":p.get("sourceRevision",""),"sectionModel":name})
    return result
