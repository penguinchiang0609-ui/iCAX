"""Direct template-owned inverse dispatch. Never evaluates a forward model."""
import copy
import importlib.util
import math
from pathlib import Path

def _module(name):
    spec=importlib.util.spec_from_file_location(name,Path(__file__).with_name(name+".py"))
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    return module
geometry=_module("profile_recognition_geometry")
queries=_module("profile_direct_fitting")

def recognize(runtime,package,section,tolerance):
    if isinstance(tolerance,bool) or not isinstance(tolerance,(int,float)) or not math.isfinite(tolerance) or not 0<tolerance<=1:
        raise ValueError("识别容差须为 (0, 1] mm 的有限数值")
    descriptor=package["descriptor"]
    result={"profileId":descriptor["id"],"packageDigest":package["packageDigest"],
            "status":"no-match","candidates":[],"exhaustive":False}
    if descriptor.get("recognition",{}).get("entryPoint")!="fitting.py":
        return {**result,"status":"unsupported","reason":"模板未提供直接反解入口"}
    loops=geometry.normalize(section,tolerance)
    context={"tolerance":tolerance,"geometry":queries,"curves":geometry}
    with runtime._script_context(package["scriptSource"],package["packageDigest"],
                                 package.get("resources"),descriptor,entry="fitting") as namespace:
        if namespace.get("IMPLEMENTED",True) is False:
            return {**result,"status":"unsupported","reason":"此模板的直接反解尚未实现"}
        fitted=namespace["fitting"](copy.deepcopy(loops),context)
    if fitted is False:return result
    if not isinstance(fitted,dict) or not isinstance(fitted.get("parameters"),dict):
        raise ValueError("fitting 必须返回 False 或参数与姿态对象")
    definitions={p["key"]:p for p in descriptor["parameters"]}
    parameters=fitted["parameters"]
    if set(parameters)-set(definitions):raise ValueError("反解返回了未声明的参数")
    # Validate only the values actually inferred; never fill descriptor defaults.
    runtime._normalize_parameters({**descriptor,"parameters":[definitions[k] for k in parameters]},parameters)
    angle=fitted.get("rotationDegrees")
    if isinstance(angle,bool) or not isinstance(angle,(int,float)) or not math.isfinite(angle):
        raise ValueError("反解必须返回有限的 rotationDegrees")
    translation=geometry.point(fitted.get("translation",[0,0]))
    unknown=sorted(set(definitions)-set(parameters))
    candidate={**fitted,"rotationDegrees":angle%360,"unresolvedParameters":unknown,
               "transform":{"rotation":math.radians(angle%360),"translation":translation},
               "verification":"direct-geometric-constraints","parameterUniqueness":fitted.get("parameterUniqueness","not-proven")}
    return {**result,"status":"matched","candidates":[candidate]}
