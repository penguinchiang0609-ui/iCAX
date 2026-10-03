import copy
import importlib.util
import math
from pathlib import Path
import sys


def _clearanced_outer_contour(contour, clearance):
    if isinstance(clearance, bool) or not isinstance(clearance, (int, float)) or not math.isfinite(clearance) or clearance < 0:
        raise ValueError("支管开口单边间隙须为非负有限数值")
    if clearance == 0:
        return contour
    # Use the same analytic contour offset as the tube-profile library. A
    # boundary without an exact supported offset fails instead of falling back
    # to a visually enlarged but unmachinable approximation.
    script = Path(__file__).resolve().parents[2] / "_shared" / "tube_profile_catalog.py"
    module_name = "icax_branch_profile_offset"
    module = sys.modules.get(module_name)
    if module is None or not getattr(module, "__file__", None) or Path(module.__file__).resolve() != script:
        spec = importlib.util.spec_from_file_location(module_name, script)
        module = importlib.util.module_from_spec(spec)
        sys.modules[module_name] = module
        try:
            spec.loader.exec_module(module)
        except Exception:
            sys.modules.pop(module_name, None)
            raise
    return module._offset_outer_contour(contour, clearance)


def generate(p, context):
    feature=context["feature"]
    profile=feature.get("section",{}).get("profile",{})
    contours=profile.get("contours")
    if not isinstance(contours,list) or not contours:
        raise ValueError("请从管型库选择支管截面，或导入本地 DXF")
    # The opening follows the filled outer envelope, never an inner void.
    # A verified supplied boundary identifies that envelope by containment;
    # profile contour array order is not a geometric contract.
    section_frame=profile.get("sectionFrame")
    outer_index=section_frame.get("outerContourIndex") if isinstance(section_frame,dict) else 0
    if isinstance(outer_index,bool) or not isinstance(outer_index,int) or not 0<=outer_index<len(contours):
        raise ValueError("提供截面的外轮廓索引无效")
    contours=[_clearanced_outer_contour(copy.deepcopy(contours[outer_index]),
                                         context.get("placement", {}).get("clearance", 0))]
    lo,hi=context["bounds"]["min"],context["bounds"]["max"]
    reference=feature.get("reference","start")
    station=feature.get("station",0)
    if not isinstance(station,(int,float)) or not math.isfinite(station):
        raise ValueError("支管位置无效")
    if reference not in ("start","end","center"):
        raise ValueError("支管定位基准无效")
    x=lo[0]+station if reference=="start" else hi[0]-station if reference=="end" else (lo[0]+hi[0])/2+station
    place = context.get("placement", {})
    a,b,r=map(math.radians,(place.get("angle",90),place.get("azimuth",0),place.get("roll",0)))
    sa,ca,sb,cb=math.sin(a),math.cos(a),math.sin(b),math.cos(b)
    axis=[ca,sa*sb,sa*cb]
    u,v=[sa,-ca*sb,-ca*cb],[0,cb,-sb]
    u,v=([u[i]*math.cos(r)+v[i]*math.sin(r) for i in range(3)],
         [-u[i]*math.sin(r)+v[i]*math.cos(r) for i in range(3)])
    center=[x,(lo[1]+hi[1])/2+place.get("offsetY",0),(lo[2]+hi[2])/2+place.get("offsetZ",0)]
    if section_frame is not None:
        if (not isinstance(section_frame,dict)
                or section_frame.get("verification")!="committed-source-brep-replay"):
            raise ValueError("提供截面的装配坐标尚未通过真实原管验证")
        def vector(key,count):
            values=section_frame.get(key)
            if (not isinstance(values,list) or len(values)!=count
                    or any(isinstance(value,bool) or not isinstance(value,(int,float))
                           or not math.isfinite(value) for value in values)):
                raise ValueError("提供截面的制造坐标无效: "+key)
            return values
        actual_u=vector("xAxisToolPart",3)
        actual_v=vector("yAxisToolPart",3)
        datum=vector("centerlineUV",2)
        center=vector("nodeToolPart",3)
        actual_axis=[actual_u[1]*actual_v[2]-actual_u[2]*actual_v[1],
                     actual_u[2]*actual_v[0]-actual_u[0]*actual_v[2],
                     actual_u[0]*actual_v[1]-actual_u[1]*actual_v[0]]
        if (abs(math.hypot(*actual_u)-1)>1e-6
                or abs(math.hypot(*actual_v)-1)>1e-6
                or abs(sum(left*right for left,right in zip(actual_u,actual_v)))>1e-6
                or abs(sum(left*right for left,right in zip(actual_axis,axis)))<1-1e-6
                or abs(r)>1e-8 or abs(center[0]-x)>0.05):
            raise ValueError("提供截面的真实方向与装配刀具方向不一致")
        # The contour coordinates are measured from their own section datum.
        # The product node denotes the branch centreline, not contour (0,0).
        u,v=actual_u,actual_v
    direction = place.get("direction", "through")
    length = place.get("length", 120)
    if direction not in ("through", "symmetric", "positive", "negative"):
        raise ValueError("支管拉伸范围无效")
    if not isinstance(length, (int, float)) or not math.isfinite(length) or length <= 0:
        raise ValueError("支管拉伸长度无效")
    if direction=="through":
        start=sum(min((lo[i]-center[i])*axis[i],(hi[i]-center[i])*axis[i]) for i in range(3))
        end=sum(max((lo[i]-center[i])*axis[i],(hi[i]-center[i])*axis[i]) for i in range(3))
        # Keep the visible cutter proportional to the stock along its own axis.
        # Both ends extend by half the projected stock span; this is the actual
        # tool solid used by preview and final cutting, not a display-only scale.
        extension=(end-start)/2
        start-=extension
        end+=extension
        length=end-start
    else:
        start={"symmetric":-length/2,"positive":0,"negative":-length}[direction]
    origin=[center[i]+axis[i]*start
            -(u[i]*datum[0]+v[i]*datum[1] if section_frame is not None else 0)
            for i in range(3)]
    nodes=[{"key":"section","operator":"profile2d","arguments":{"placement":{"origin":origin,"xAxis":u,"yAxis":v},"contours":contours}},
           {"key":"tool","operator":"extrude","inputs":["section"],"arguments":{"vector":[n*length for n in axis]}}]
    return {"mode":"solid","coordinateSpace":"part","outputKey":"tool","model":{
        "schema":"icax.neutral-model","schemaVersion":1,
        "template":{"id":"branch-profile","version":"1.0.1","packageDigest":"self-contained"},"geometry":nodes}}
