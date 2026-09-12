"""P15 槽钢 / U-C 型钢：族内模型独立实现。"""
import importlib
import json
from .geometry import envelope, swap
MODELS = {"channel-cold-u":"model_0","channel-cold-c":"model_1","channel-cold-unequal":"model_2","channel-lipped-inward":"model_3","channel-lipped-outward":"model_4","channel-hot-tapered":"model_5","channel-hot-parallel":"model_6","channel-welded":"model_7"}
PARAMETERS = {"channel-cold-u":{"width":"width","depth":"depth","wallThickness":"wallThickness","bendRadius":"model0BendRadius"},"channel-cold-c":{"width":"width","depth":"depth","wallThickness":"wallThickness","bendRadius":"model1BendRadius"},"channel-cold-unequal":{"width":"width","depth":"depth","wallThickness":"wallThickness","bendRadius":"model2BendRadius","upperWidth":"model2UpperWidth"},"channel-lipped-inward":{"width":"width","depth":"depth","wallThickness":"wallThickness","bendRadius":"model3BendRadius","lipLength":"model3LipLength"},"channel-lipped-outward":{"width":"width","depth":"depth","wallThickness":"wallThickness","bendRadius":"model4BendRadius","lipLength":"model4LipLength"},"channel-hot-tapered":{"width":"width","depth":"depth","wallThickness":"wallThickness","flangeThickness":"model5FlangeThickness","rootRadius":"model5RootRadius","toeRadius":"model5ToeRadius","flangeSlope":"model5FlangeSlope"},"channel-hot-parallel":{"width":"width","depth":"depth","wallThickness":"wallThickness","flangeThickness":"model6FlangeThickness","rootRadius":"model6RootRadius","toeRadius":"model6ToeRadius"},"channel-welded":{"width":"width","depth":"depth","wallThickness":"wallThickness","flangeThickness":"model7FlangeThickness","lowerThickness":"model7LowerThickness","weldLeg":"model7WeldLeg"}}

def selected(p):
    name=p.get("sectionModel","channel-cold-u")
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
    result["manufacturingRoute"] = {"channel-cold-u":"ColdBent","channel-cold-c":"ColdBent","channel-cold-unequal":"ColdBent","channel-lipped-inward":"Unspecified","channel-lipped-outward":"Unspecified","channel-hot-tapered":"HotRolled","channel-hot-parallel":"HotRolled","channel-welded":"Welded"}[name]
    result.update({"kind":"channel","_familyParameters":dict(p),"specification":"槽钢 / U-C 型钢"+" "+str(result["width"])+" × "+str(result["depth"])+" mm","geometrySource":p.get("geometrySource","idealizedFallback"),"sourceRevision":p.get("sourceRevision",""),"sectionModel":name})
    return result
