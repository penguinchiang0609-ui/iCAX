"""Template-owned inverse entry points with host-owned reconstruction checks."""
import copy
import importlib.util
import math
from pathlib import Path

spec=importlib.util.spec_from_file_location("icax_recognition_geometry",Path(__file__).with_name("profile_recognition_geometry.py"))
geometry=importlib.util.module_from_spec(spec)
spec.loader.exec_module(geometry)


def _linear_solve(matrix,vector):
    n=len(vector);rows=[row[:]+[value] for row,value in zip(matrix,vector)]
    for i in range(n):
        pivot=max(range(i,n),key=lambda k:abs(rows[k][i]))
        if abs(rows[pivot][i])<1e-20:return None
        rows[i],rows[pivot]=rows[pivot],rows[i]
        scale=rows[i][i];rows[i]=[v/scale for v in rows[i]]
        for j in range(n):
            if i!=j:
                scale=rows[j][i];rows[j]=[a-scale*b for a,b in zip(rows[j],rows[i])]
    return [row[-1] for row in rows]


def solve(runtime,package,target,seeds,tolerance):
    """Bounded numerical refinement, with seeds/discrete choices owned by scripts.

    This finds representatives, not a proof of a unique inverse of Python code.
    All candidates still pass the independent forward reconstruction below.
    """
    descriptor=package["descriptor"]
    definitions={p["key"]:p for p in descriptor["parameters"]}
    keys=[k for k,p in definitions.items() if p["valueType"]=="number"]
    def evaluate(values):
        normalized=runtime._normalize_parameters(descriptor,values)
        _,contours=runtime.evaluate_section(descriptor,package["scriptSource"],normalized,
                                            package["packageDigest"],package.get("resources"))
        return geometry.normalize({"contours":contours},tolerance)
    def error(values):
        try:return geometry.residual(target,evaluate(values))
        except (ValueError,ArithmeticError):return None
    found=[]
    for seed in seeds:
        current=copy.deepcopy(seed);r=error(current)
        if r is None:continue
        for iteration in range(32):
            if max(map(abs,r),default=0)<=tolerance/4:break
            columns=[]
            for key in keys:
                step=max(abs(float(current[key]))*1e-5,1e-5)
                changed={**current,key:current[key]+step};rr=error(changed)
                if rr is None or len(rr)!=len(r):
                    step=-step;rr=error({**current,key:current[key]+step})
                columns.append([(b-a)/step for a,b in zip(r,rr)] if rr is not None and len(rr)==len(r) else [0.0]*len(r))
            matrix=[[sum(x*y for x,y in zip(a,b))+(1e-8 if i==j else 0)
                     for j,b in enumerate(columns)] for i,a in enumerate(columns)]
            rhs=[-sum(x*y for x,y in zip(c,r)) for c in columns]
            delta=_linear_solve(matrix,rhs)
            if delta is None:break
            improved=False
            for factor in (1,0.5,0.25,0.125,0.0625,0.015625):
                trial={**current,**{k:current[k]+factor*d for k,d in zip(keys,delta)}}
                rr=error(trial)
                if rr is not None and sum(v*v for v in rr)<sum(v*v for v in r):
                    current,r=trial,rr;improved=True;break
            if not improved:break
        if max(map(abs,r),default=0)<=tolerance/2:
            found.append({"parameters":current,"parameterUniqueness":"not-proven"})
    return found


def recognize(runtime,package,section,tolerance):
    if isinstance(tolerance,bool) or not isinstance(tolerance,(float,int)) or not math.isfinite(tolerance) or not 0<tolerance<=1:
        raise ValueError("识别容差须为 (0, 1] mm 的有限数值")
    descriptor=package["descriptor"]
    result={"profileId":descriptor["id"],"packageDigest":package["packageDigest"],
            "status":"no-match","candidates":[],"exhaustive":False}
    fixed=descriptor["profileForm"]=="fixed"
    if not fixed and descriptor.get("recognition") != {"schemaVersion":1,"entryPoint":"recognize.py"}:
        return {**result,"status":"unsupported","reason":"模板未声明逆向入口"}
    target=geometry.normalize(section,tolerance)
    # Each pose is sent to the same inverse script. No profile type knowledge
    # lives in this scheduler, and template code cannot mark itself verified.
    seen=set()
    for local,pose in geometry.frames(target):
        context={"tolerance":tolerance,"geometry":geometry,
                 "descriptor":copy.deepcopy(descriptor),"defaultParameters":copy.deepcopy(package["defaultParameters"])}
        context["solve"]=lambda seeds:solve(runtime,package,local,seeds,tolerance)
        if fixed:
            proposed=[{"parameters":{},"parameterUniqueness":"unique"}]
        else:
            with runtime._script_context(package["scriptSource"],package["packageDigest"],
                                         package.get("resources"),descriptor,entry="recognize") as namespace:
                proposed=namespace["recognize"](copy.deepcopy(local),context)
        if not isinstance(proposed,list) or len(proposed)>64:
            raise ValueError("recognize 必须返回不超过 64 个候选组成的数组")
        for proposal in proposed:
            if not isinstance(proposal,dict) or not isinstance(proposal.get("parameters"),dict):
                raise ValueError("逆向候选必须明确返回 parameters 对象")
            # Never fill missing parameters silently from descriptor defaults.
            expected={p["key"] for p in descriptor["parameters"]}
            if set(proposal["parameters"])!=expected:
                raise ValueError("逆向候选必须显式提供全部重建参数；不可识别项须另行标明")
            parameters=runtime._normalize_parameters(descriptor,proposal["parameters"])
            rebuilt=runtime._evaluate(descriptor,package["scriptSource"],parameters,
                package["packageDigest"],package["sourceFileName"],package.get("resources"))
            passed,error=geometry.verify(target,geometry.normalize(rebuilt,tolerance),pose,tolerance)
            if not passed:continue
            unknown=proposal.get("unresolvedParameters",[])
            if not isinstance(unknown,list) or any(k not in expected for k in unknown):
                raise ValueError("unresolvedParameters 必须列出声明过的参数名")
            identity=repr((sorted(parameters.items()),round(pose["rotation"],9)))
            if identity in seen:continue
            seen.add(identity)
            result["candidates"].append({"parameters":{k:v for k,v in parameters.items() if k not in unknown},
                "representativeParameters":parameters,"unresolvedParameters":unknown,
                "parameterUniqueness":proposal.get("parameterUniqueness","not-proven"),
                "poseUniqueness":"not-proven",
                "transform":pose,"verified":True,"maximumBoundaryError":error,
                "verification":"analytic-boundaries","profile":rebuilt})
        if len(result["candidates"])>=64:break
    if result["candidates"]:result["status"]="matched"
    return result
