"""P04 多边形管：族内模型独立实现。"""
import importlib
import json
from .geometry import envelope, swap
MODELS = {"hexagonal-tube":"model_0","triangle-equilateral":"model_1","triangle-scalene":"model_2","trapezoid-right":"model_3","trapezoid-isosceles":"model_4","parallelogram-tube":"model_5","pentagonal-tube":"model_6","octagonal-tube":"model_7"}
PARAMETERS = {"hexagonal-tube":{"width":"width","innerSize":"model0InnerSize","innerPhase":"model0InnerPhase","outerRadii":"model0OuterRadii","innerRadii":"model0InnerRadii"},"triangle-equilateral":{"width":"width","innerSize":"model1InnerSize","innerPhase":"model1InnerPhase","outerRadii":"model1OuterRadii","innerRadii":"model1InnerRadii"},"triangle-scalene":{"width":"width","depth":"depth","apexOffset":"model2ApexOffset","wallThickness":"wallThickness"},"trapezoid-right":{"width":"width","depth":"depth","topWidth":"model3TopWidth","wallThickness":"wallThickness"},"trapezoid-isosceles":{"width":"width","depth":"depth","topWidth":"model4TopWidth","wallThickness":"wallThickness"},"parallelogram-tube":{"width":"width","depth":"depth","shear":"model5Shear","wallThickness":"wallThickness"},"pentagonal-tube":{"width":"width","innerSize":"model6InnerSize","innerPhase":"model6InnerPhase","outerRadii":"model6OuterRadii","innerRadii":"model6InnerRadii"},"octagonal-tube":{"width":"width","innerSize":"model7InnerSize","innerPhase":"model7InnerPhase","outerRadii":"model7OuterRadii","innerRadii":"model7InnerRadii"}}

def selected(p):
    name=p.get("sectionModel","hexagonal-tube")
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
    result["manufacturingRoute"] = {"hexagonal-tube":"Unspecified","triangle-equilateral":"Unspecified","triangle-scalene":"Unspecified","trapezoid-right":"Unspecified","trapezoid-isosceles":"Unspecified","parallelogram-tube":"Unspecified","pentagonal-tube":"Unspecified","octagonal-tube":"Unspecified"}[name]
    result.update({"kind":"polygon","_familyParameters":dict(p),"specification":"多边形管"+" "+str(result["width"])+" × "+str(result["depth"])+" mm","geometrySource":"providedBoundary" if actual else "idealizedFallback","sourceRevision":p.get("sourceRevision",""),"sectionModel":name})
    return result
