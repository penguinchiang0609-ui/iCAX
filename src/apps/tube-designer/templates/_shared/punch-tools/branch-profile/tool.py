import copy
import math


def generate(p, context):
    feature=context["feature"]
    profile=feature.get("section",{}).get("profile",{})
    contours=profile.get("contours")
    if not isinstance(contours,list) or not contours:
        raise ValueError("请从管型库选择支管截面，或导入本地 DXF")
    # A branch intersection always cuts the filled outside envelope. Keeping
    # the inner void as a selectable region can leave a detached plug in the
    # main tube, so this operation intentionally uses only the outer contour.
    contours=copy.deepcopy(contours[:1])
    lo,hi=context["bounds"]["min"],context["bounds"]["max"]
    reference=feature.get("reference","start")
    station=feature.get("station",0)
    if not isinstance(station,(int,float)) or not math.isfinite(station):
        raise ValueError("支管位置无效")
    if reference not in ("start","end","center"):
        raise ValueError("支管定位基准无效")
    x=lo[0]+station if reference=="start" else hi[0]-station if reference=="end" else (lo[0]+hi[0])/2+station
    a,b,r=map(math.radians,(p["angle"],p["azimuth"],p["roll"]))
    sa,ca,sb,cb=math.sin(a),math.cos(a),math.sin(b),math.cos(b)
    axis=[ca,sa*sb,sa*cb]
    u,v=[sa,-ca*sb,-ca*cb],[0,cb,-sb]
    u,v=([u[i]*math.cos(r)+v[i]*math.sin(r) for i in range(3)],
         [-u[i]*math.sin(r)+v[i]*math.cos(r) for i in range(3)])
    center=[x,(lo[1]+hi[1])/2+p["offsetY"],(lo[2]+hi[2])/2+p["offsetZ"]]
    length=p["length"]
    if p["direction"]=="through":
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
        start={"symmetric":-length/2,"positive":0,"negative":-length}[p["direction"]]
    origin=[center[i]+axis[i]*start for i in range(3)]
    nodes=[{"key":"section","operator":"profile2d","arguments":{"placement":{"origin":origin,"xAxis":u,"yAxis":v},"contours":contours}},
           {"key":"tool","operator":"extrude","inputs":["section"],"arguments":{"vector":[n*length for n in axis]}}]
    return {"mode":"solid","coordinateSpace":"part","outputKey":"tool","model":{
        "schema":"icax.neutral-model","schemaVersion":1,
        "template":{"id":"branch-profile","version":"1.0.0","packageDigest":"self-contained"},"geometry":nodes}}
