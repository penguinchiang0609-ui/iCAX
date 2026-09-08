import math


def generate(p,context):
    lo,hi=context["bounds"]["min"],context["bounds"]["max"]
    f=context["feature"];station=f.get("station",0);ref=f.get("reference","start")
    if isinstance(station,bool) or not isinstance(station,(int,float)) or not math.isfinite(station) or ref not in ("start","end","center"):
        raise ValueError("V 槽定位无效")
    x=lo[0]+station if ref=="start" else hi[0]-station if ref=="end" else (lo[0]+hi[0])/2+station
    r=math.radians(p["rotation"]);sr,cr=math.sin(r),math.cos(r)
    h=(hi[1]-lo[1])*abs(sr)/2+(hi[2]-lo[2])*abs(cr)/2
    span=(hi[1]-lo[1])*abs(cr)+(hi[2]-lo[2])*abs(sr)+2
    if p["bridge"]>=2*h:raise ValueError("底部保留厚度须小于该方向截面高度")
    bottom=-h+p["bridge"];top=h+1;depth=top-bottom
    style=p.get("style","sharp_v")
    if style not in ("sharp_v","asymmetric_v","rounded_v","left_arc","right_arc","flat_v","relief_v"):
        raise ValueError("V 槽类型无效")
    left=math.radians(p["leftAngle"]) if style=="asymmetric_v" else math.radians(p["angle"])/2
    right=math.radians(p["rightAngle"]) if style=="asymmetric_v" else left
    # Existing sharp-V recipes retain their original explicit root dimensions.
    # New styles have independent defaults, so selecting one always produces
    # its advertised geometry without inheriting a hidden field from another.
    legacy=style in ("sharp_v","asymmetric_v")
    radius=p["rootRadius"] if legacy else p["curveRadius"] if style in ("rounded_v","left_arc","right_arc") else 0
    root_width=p["rootWidth"] if legacy else p["flatWidth"] if style=="flat_v" else 0
    if radius and root_width:raise ValueError("槽底圆角和平口宽度不能同时使用")
    origin=[x,(lo[1]+hi[1])/2+cr*span/2,(lo[2]+hi[2])/2-sr*span/2]
    nodes=[]
    def prism(key,contour,profile_lift=0):
        profile_origin=[origin[0],origin[1]+sr*profile_lift,origin[2]+cr*profile_lift]
        nodes.append({"key":key+"-profile","operator":"profile2d","arguments":{"placement":{
            "origin":profile_origin,"xAxis":[1,0,0],"yAxis":[0,sr,cr]},"contours":[contour]}})
        nodes.append({"key":key,"operator":"extrude","inputs":[key+"-profile"],"arguments":{"vector":[0,-cr*span,sr*span]}})
    if radius:
        left_y=bottom+radius*(1-math.sin(left));left_x=-radius*math.cos(left)
        right_y=bottom+radius*(1-math.sin(right));right_x=radius*math.cos(right)
        if max(left_y,right_y)>=top:raise ValueError("槽底圆弧超过可切除高度，请减小圆弧半径")
        left_top=[left_x-(top-left_y)*math.tan(left),top]
        right_top=[right_x+(top-right_y)*math.tan(right),top]
        # All curved variants are analytic circular arcs. A single-sided arc
        # is a root-to-wall transition, not a claimed bend-allowance solution.
        if style=="left_arc":
            middle_angle=(-math.pi+left-math.pi/2)/2
            right_top=[depth*math.tan(right),top]
            arc={"kind":"arc","start":[left_x,left_y],
                 "middle":[radius*math.cos(middle_angle),bottom+radius+radius*math.sin(middle_angle)],"end":[0,bottom]}
            ends=([left_x,left_y],[0,bottom])
        elif style=="right_arc":
            middle_angle=(-math.pi/2-right)/2
            left_top=[-depth*math.tan(left),top]
            arc={"kind":"arc","start":[0,bottom],
                 "middle":[radius*math.cos(middle_angle),bottom+radius+radius*math.sin(middle_angle)],"end":[right_x,right_y]}
            ends=([0,bottom],[right_x,right_y])
        else:
            arc={"kind":"arc","start":[left_x,left_y],"middle":[0,bottom],"end":[right_x,right_y]}
            ends=([left_x,left_y],[right_x,right_y])
        contour={"kind":"path","segments":[
            {"kind":"line","start":left_top,"end":ends[0]},arc,
            {"kind":"line","start":ends[1],"end":right_top},
            {"kind":"line","start":right_top,"end":left_top}]}
    else:
        w=root_width/2;points=[[-w-depth*math.tan(left),top],[-w,bottom]]
        if w:points.append([w,bottom])
        points.extend([[w+depth*math.tan(right),top]])
        contour={"kind":"path","segments":[{"kind":"line","start":a,"end":b} for a,b in zip(points,points[1:]+points[:1])]}
    prism("notch",contour);cutters=["notch"]
    relief_diameter=p["holeDiameter"] if style=="relief_v" else p["reliefDiameter"]
    relief_lift=p["holeLift"] if style=="relief_v" else p["reliefLift"]
    if relief_diameter:
        radius=relief_diameter/2
        cy=bottom+relief_lift
        # A cutter may cross the retained edge: during editing it is only a
        # tool, and cutting through is also an intentional modelling action.
        # Two exact semicircular edges retain the analytic circle through the
        # persisted BRep path, without a periodic edge's ambiguous seam. The
        # circle center belongs to placement, not the imported-DXF `center`.
        circle={"kind":"path","segments":[
            {"kind":"arc","start":[-radius,0],"middle":[0,-radius],"end":[radius,0]},
            {"kind":"arc","start":[radius,0],"middle":[0,radius],"end":[-radius,0]}]}
        prism("relief",circle,profile_lift=cy)
        cutters.append("relief")
    if p.get("bottomCut",False):
        half_width=p["bottomCutWidth"]/2
        points=[[-half_width,-h-1],[half_width,-h-1],[half_width,bottom+1],[-half_width,bottom+1]]
        prism("bottom-cut",{"kind":"path","segments":[{"kind":"line","start":a,"end":b} for a,b in zip(points,points[1:]+points[:1])]})
        cutters.append("bottom-cut")
    output="notch"
    if len(cutters)>1:
        nodes.append({"key":"tool","operator":"boolean","inputs":cutters,"arguments":{"operation":"union"}});output="tool"
    return {"mode":"solid","coordinateSpace":"part","outputKey":output,"model":{
        "schema":"icax.neutral-model","schemaVersion":1,
        "template":{"id":"v-notch","version":"1.1.0","packageDigest":"self-contained"},"geometry":nodes}}
