"""Local, repeatable structural machining functions.

Inputs describe one stock and its local section, keep regions or mating solids.
These functions do not inspect a product family, route, item list or product
parameters.  Geometry is neutral construction data; the shared executor owns
atomic application, instance identity and operation provenance.
"""
from copy import deepcopy
import hashlib
import importlib.util
import math
from pathlib import Path
import sys

EPS = 1.0e-7
SCHEMA = "icax.assembly-process-result"
DEPENDENCIES = ["tube_profile_catalog.py", "profile_package_runtime.py"]


def _profile_rules():
    path=Path(__file__).with_name("tube_profile_catalog.py")
    name="icax_structural_profile_"+hashlib.sha256(path.read_bytes()).hexdigest()[:16]
    if name not in sys.modules:
        spec=importlib.util.spec_from_file_location(name,path)
        module=importlib.util.module_from_spec(spec);sys.modules[name]=module;spec.loader.exec_module(module)
    return sys.modules[name]


def number(value, name, positive=False):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError(name + " must be finite")
    value = float(value)
    if positive and value <= EPS:
        raise ValueError(name + " must be positive")
    return value


def parameters_only(parameters, allowed, required=()):
    if not isinstance(parameters,dict) or set(parameters)-set(allowed) or not set(required).issubset(parameters):
        raise ValueError("加工参数缺失或包含未声明字段")


def vector(value, name):
    if not isinstance(value, (list, tuple)) or len(value) != 3:
        raise ValueError(name + " must contain three coordinates")
    return [number(v, name) for v in value]


def path(points):
    if not isinstance(points, (list, tuple)) or len(points) < 3:
        raise ValueError("region requires at least three polygon points")
    points = [[number(p[0], "polygon"), number(p[1], "polygon")] for p in points
              if isinstance(p, (list, tuple)) and len(p) == 2]
    if len(points) < 3 or abs(sum(a[0]*b[1]-b[0]*a[1] for a,b in zip(points, points[1:]+points[:1]))) <= EPS:
        raise ValueError("region polygon is empty")
    return {"kind":"path", "closed":True, "segments":[
        {"kind":"line", "start":a, "end":b} for a,b in zip(points,points[1:]+points[:1])]}


def circle(radius):
    radius = number(radius, "radius", True); q = radius/math.sqrt(2)
    return {"kind":"path", "closed":True, "segments":[
        {"kind":"arc","start":[radius,0],"middle":[q,q],"end":[0,radius]},
        {"kind":"arc","start":[0,radius],"middle":[-q,q],"end":[-radius,0]},
        {"kind":"arc","start":[-radius,0],"middle":[-q,-q],"end":[0,-radius]},
        {"kind":"arc","start":[0,-radius],"middle":[q,-q],"end":[radius,0]}]}


def exact_section_contours(contours):
    """Represent observed ellipse arcs as exact rational conics before sweeping.

    This preserves the real section while keeping the swept surface and its
    boundary curves in one parameter frame through cuts and resource replay.
    """
    contours=deepcopy(contours)
    for contour in contours:
        converted=[]
        for segment in contour.get("segments",[]):
            if segment.get("kind")!="ellipseArc":
                converted.append(segment);continue
            major=number(segment["majorRadius"],"ellipse major radius",True)
            minor=number(segment["minorRadius"],"ellipse minor radius",True)
            center=segment["center"]
            if not isinstance(center,(list,tuple)) or len(center)!=2:raise ValueError("ellipse center requires two coordinates")
            center=[number(value,"ellipse center") for value in center]
            start=number(segment["startAngle"],"ellipse start angle");end=number(segment["endAngle"],"ellipse end angle")
            rotation=number(segment.get("rotation",0),"ellipse rotation")
            if minor>major or end<=start or end-start>2*math.pi+EPS:raise ValueError("invalid ellipse arc")
            count=max(1,math.ceil((end-start)/(math.pi/2)))
            def point(angle,factor=1):
                x=major*math.cos(angle)*factor;y=minor*math.sin(angle)*factor
                return [center[0]+x*math.cos(rotation)-y*math.sin(rotation),
                        center[1]+x*math.sin(rotation)+y*math.cos(rotation)]
            pieces=[]
            for index in range(count):
                a=start+(end-start)*index/count;b=start+(end-start)*(index+1)/count
                weight=math.cos((b-a)/2)
                pieces.append({"kind":"nurbs","degree":2,"controlPoints":[point(a),point((a+b)/2,1/weight),point(b)],
                               "weights":[1,weight,1],"knots":[0,1],"multiplicities":[3,3],"periodic":False})
            if segment.get("reversed"):
                pieces.reverse()
                for piece in pieces:piece["reversed"]=True
            converted.extend(pieces)
        contour["segments"]=converted
    return contours


def rounded_rectangle(width, height, radius):
    width=number(width,"width",True);height=number(height,"height",True)
    radius=number(radius,"radius")
    if radius < 0: raise ValueError("radius must be non-negative")
    radius=min(radius,width/2-EPS,height/2-EPS)
    x,y=width/2,height/2
    if radius <= EPS: return path([[-x,-y],[x,-y],[x,y],[-x,y]])
    q=radius/math.sqrt(2)
    return {"kind":"path","closed":True,"segments":[
        {"kind":"line","start":[-x+radius,-y],"end":[x-radius,-y]},
        {"kind":"arc","start":[x-radius,-y],"middle":[x-radius+q,-y+radius-q],"end":[x,-y+radius]},
        {"kind":"line","start":[x,-y+radius],"end":[x,y-radius]},
        {"kind":"arc","start":[x,y-radius],"middle":[x-radius+q,y-radius+q],"end":[x-radius,y]},
        {"kind":"line","start":[x-radius,y],"end":[-x+radius,y]},
        {"kind":"arc","start":[-x+radius,y],"middle":[-x+radius-q,y-radius+q],"end":[-x,y-radius]},
        {"kind":"line","start":[-x,y-radius],"end":[-x,-y+radius]},
        {"kind":"arc","start":[-x,-y+radius],"middle":[-x+radius-q,-y+radius-q],"end":[-x+radius,-y]}]}


def input_geometry(process_input):
    if process_input.get("schema") != "icax.assembly-process-input" or process_input.get("schemaVersion") != 1:
        raise ValueError("unsupported process input")
    if not isinstance(process_input.get("parts",{}).get("stock"), dict):
        raise ValueError("stock role is required")
    geometry=process_input.get("geometry")
    if not isinstance(geometry,dict): raise ValueError("local geometry is required")
    return geometry


def joint_plane(point, inward, mate_direction, extent):
    """Actual two-member bisector; no nominal template angle or role names."""
    point=vector(point,"joint point");inward=vector(inward,"stock direction")
    def unit(a):
        length=math.sqrt(sum(v*v for v in a))
        if length<=EPS:raise ValueError("节点方向退化")
        return [v/length for v in a]
    def cross(a,b):return [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]]
    inward=unit(inward)
    n=unit([inward[i]-unit(vector(mate_direction,"mate direction"))[i] for i in range(3)]) if mate_direction is not None else inward
    u=unit(cross(n,[0,0,1] if abs(n[2])<.9 else [0,1,0]));w=cross(n,u)
    extent=number(extent,"half-space extent",True)
    return {"placement":{"origin":point,"xAxis":u,"yAxis":w},
            "polygon":[[-extent,-extent],[extent,-extent],[extent,extent],[-extent,extent]],
            "vector":[v*extent for v in n]},n


def joint_blank_extension(planes, section_x, section_y, direction, width, depth):
    x=vector(section_x,"section xAxis");y=vector(section_y,"section yAxis");d=vector(direction,"stock direction")
    width=number(width,"section width",True);depth=number(depth,"section depth",True)
    extra=0
    for n in planes:
        denominator=abs(sum(a*b for a,b in zip(n,d)))
        if denominator<=EPS:raise ValueError("端面与母材轴平行")
        extra+=(width*abs(sum(a*b for a,b in zip(n,x)))+depth*abs(sum(a*b for a,b in zip(n,y))))/(2*denominator)
    return extra


def result(parameters, geometry, operations):
    keys={node["key"] for node in geometry}
    references={key for node in geometry for key in node.get("inputs",[]) if key not in keys}
    references.update(key for op in operations for key in op.get("tools",[op.get("tool")]) if key is not None and key not in keys)
    return {"schema":SCHEMA,"schemaVersion":1,"applicable":True,"reason":"",
            "parameters":deepcopy(parameters),"geometry":geometry,"operations":operations,
            "externalGeometry":sorted(references)}


class Tools:
    def __init__(self): self.geometry=[];self.joint_requirements=[]

    def solid(self, key, recipe, *, record_joint=False):
        """Construct a local section prism or observe an intrinsic component."""
        if "jointPlane" in recipe:
            g=recipe["jointPlane"]
            recipe,normal=joint_plane(g["point"],g["inward"],g.get("mateDirection"),g["extent"])
            if record_joint:
                self.joint_requirements.append({"role":"stock","kind":"joint-plane",
                    "point":deepcopy(g["point"]),"inwardNormal":normal})
        if "union" in recipe:
            return self.union(key+".union",[self.solid(key+"."+str(i),part) for i,part in enumerate(recipe["union"])])
        if "section" in recipe:
            root=self.solid(key+".section",recipe["section"])
            for index,region in enumerate(recipe.get("keepRegions",[])):
                tool=self.solid(key+".limit."+str(index),region);next_key=key+".limited."+str(index)
                self.geometry.append({"key":next_key,"operator":"boolean","inputs":[root,tool],
                                      "arguments":{"operation":"intersect","target":root,"tools":[tool]}})
                root=next_key
            return root
        if "center" in recipe:
            center=vector(recipe["center"],"region center");x=vector(recipe["xAxis"],"region xAxis");y=vector(recipe["yAxis"],"region yAxis")
            width=number(recipe["width"],"region width",True);height=number(recipe["height"],"region height",True)
            depth=number(recipe["thickness"],"region thickness",True)
            normal=[x[1]*y[2]-x[2]*y[1],x[2]*y[0]-x[0]*y[2],x[0]*y[1]-x[1]*y[0]]
            recipe={"placement":{"origin":[center[i]-normal[i]*depth/2 for i in range(3)],"xAxis":x,"yAxis":y},
                    "polygon":[[-width/2,-height/2],[width/2,-height/2],[width/2,height/2],[-width/2,height/2]],
                    "vector":[v*depth for v in normal]}
        if "intrinsicGeometryKey" in recipe:
            value=recipe["intrinsicGeometryKey"]
            if not isinstance(value,str) or not value: raise ValueError("intrinsic geometry observer is invalid")
            return value
        placement=recipe.get("placement",{})
        placement={"origin":vector(placement.get("origin"),"origin"),
                   "xAxis":vector(placement.get("xAxis"),"xAxis"),
                   "yAxis":vector(placement.get("yAxis"),"yAxis")}
        x,y=placement["xAxis"],placement["yAxis"]
        if abs(sum(v*v for v in x)-1)>1e-5 or abs(sum(v*v for v in y)-1)>1e-5 or abs(sum(a*b for a,b in zip(x,y)))>1e-5:
            raise ValueError("section axes must be orthonormal")
        contours=deepcopy(recipe.get("contours")) if "contours" in recipe else [path(recipe.get("polygon"))]
        clearance=number(recipe.get("clearance",0),"section clearance")
        if clearance<0:raise ValueError("section clearance must be non-negative")
        if clearance:
            source=recipe.get("profileEnvelope")
            catalog=_profile_rules()
            if source and source.get("kind")=="system":
                _,module=catalog._load_package(source["profileId"])
                contours=module.contours(deepcopy(source["profileData"]),clearance=clearance)[:len(contours)]
            else:
                contours[0]=catalog._offset_outer_contour(contours[0],clearance)
        if not isinstance(contours,list) or not contours: raise ValueError("section contours are required")
        contours=exact_section_contours(contours)
        extrusion=vector(recipe.get("vector"),"extrusion")
        if sum(v*v for v in extrusion)<=EPS*EPS: raise ValueError("extrusion must be nonzero")
        if recipe.get("geometryKind")=="stock-section":
            # Use the stock's exact local section frame, matching the original
            # straight stock declaration. Building diagonal sections directly
            # at distant world coordinates can create coincident sliver faces.
            def unit(a):
                norm=math.sqrt(sum(v*v for v in a));return [v/norm for v in a]
            x=unit(x);z=unit([x[1]*y[2]-x[2]*y[1],x[2]*y[0]-x[0]*y[2],x[0]*y[1]-x[1]*y[0]])
            y=[z[1]*x[2]-z[2]*x[1],z[2]*x[0]-z[0]*x[2],z[0]*x[1]-z[1]*x[0]]
            self.geometry.extend([
                {"key":key+".profile","operator":"profile2d","arguments":{"placement":{"origin":[0,0,0],"xAxis":[1,0,0],"yAxis":[0,1,0]},"contours":contours}},
                {"key":key+".extrude","operator":"extrude","inputs":[key+".profile"],"arguments":{"vector":[sum(a*b for a,b in zip(extrusion,axis)) for axis in (x,y,z)]}},
                {"key":key+".solid","operator":"transform","inputs":[key+".extrude"],"arguments":{"placement":{"origin":placement["origin"],"xAxis":x,"yAxis":y,"zAxis":z}}}])
            return key+".solid"
        self.geometry.extend([
            {"key":key+".profile","operator":"profile2d","arguments":{"placement":placement,"contours":contours}},
            {"key":key+".solid","operator":"extrude","inputs":[key+".profile"],"arguments":{"vector":extrusion}}])
        return key+".solid"

    def union(self,key,roots):
        if len(roots)==1:return roots[0]
        if not roots:raise ValueError("union requires a solid")
        self.geometry.append({"key":key,"operator":"boolean","inputs":roots,"arguments":{"operation":"union"}})
        return key


def stock_fit(process_input, parameters):
    """Keep explicit local regions, then fit actual mating section envelopes."""
    parameters_only(parameters,())
    g=input_geometry(process_input); t=Tools(); ops=[]
    regions=g.get("keepRegions",[]);receivers=g.get("receivers",[])
    if not isinstance(regions,list) or not isinstance(receivers,list):raise ValueError("local region lists are required")
    for index,region in enumerate(regions):
        # Only a direct keep plane trims this stock's end. Planes nested in
        # receiver observations describe the receiver, not this member.
        root=t.solid("keep."+str(index),region,record_joint=True)
        ops.append({"id":"keep."+str(index),"kind":"neutral-csg","role":"stock","operation":"intersect","tool":root})
    if receivers:
        roots=[t.solid("receiver."+str(index),recipe) for index,recipe in enumerate(receivers)]
        if g.get("unionReceivers"):roots=[t.union("receiver.union",roots)]
        op={"id":"fit","kind":"neutral-csg","role":"stock","operation":"subtract","tools":roots}
        if "keepConnectedTo" in g:op["arguments"]={"keepConnectedTo":vector(g["keepConnectedTo"],"witness")}
        ops.append(op)
    produced=result(parameters,t.geometry,ops)
    if t.joint_requirements:
        produced["materialRequirements"]=t.joint_requirements
    return produced


def profile_pocket(process_input, parameters):
    """Clip a mating section to a local window before cutting a receiving wall."""
    parameters_only(parameters,())
    g=input_geometry(process_input);t=Tools()
    section=t.solid("section",g["section"])
    for index,region in enumerate(g.get("limits",[])):
        root=t.solid("limit."+str(index),region);key="limited."+str(index)
        t.geometry.append({"key":key,"operator":"boolean","inputs":[section,root],
                           "arguments":{"operation":"intersect","target":section,"tools":[root]}})
        section=key
    return result(parameters,t.geometry,[{"id":"pocket","kind":"neutral-csg","role":"stock","operation":"subtract","tool":section}])


def planar_trim(process_input, parameters):
    """Keep the material inside explicit local target end half-planes."""
    parameters_only(parameters,())
    g=input_geometry(process_input);points=deepcopy(g["boundsPolygon"])
    path(points)
    for plane in g["planes"]:
        n=plane["normal"]
        if not isinstance(n,(list,tuple)) or len(n)!=2:raise ValueError("plane requires a two dimensional normal")
        n=[number(v,"plane normal") for v in n];limit=number(plane["limit"],"plane limit")
        if sum(v*v for v in n)<=EPS*EPS:raise ValueError("plane normal is zero")
        clipped=[]
        for a,b in zip(points,points[1:]+points[:1]):
            da=sum(x*y for x,y in zip(a,n))-limit;db=sum(x*y for x,y in zip(b,n))-limit
            if da<=1e-9:clipped.append(a)
            if (da < -1e-9 and db > 1e-9) or (da > 1e-9 and db < -1e-9):
                q=da/(da-db);clipped.append([a[i]+q*(b[i]-a[i]) for i in range(2)])
        points=clipped
    local=deepcopy(process_input);local["geometry"]={"keepRegions":[{
        "placement":g["placement"],"polygon":points,"vector":g["vector"]}]}
    return stock_fit(local,parameters)


def apertures(process_input, parameters):
    parameters_only(parameters,())
    return _apertures(input_geometry(process_input),parameters)


def _apertures(g,parameters):
    t=Tools();roots=[]
    holes=g.get("holes")
    if not isinstance(holes,list) or not holes:raise ValueError("holes are required")
    for index,h in enumerate(holes):
        contour=(circle(number(h["diameter"],"diameter",True)/2) if "diameter" in h else
                 rounded_rectangle(h["width"],h["height"],h.get("radius",0)))
        roots.append(t.solid("hole."+str(index),{"placement":h["placement"],"contours":[contour],"vector":h["vector"]}))
    return result(parameters,t.geometry,[{"id":"apertures","kind":"neutral-csg","role":"stock","operation":"subtract","tools":roots}])


def plate_apertures(process_input, parameters):
    """Local bolt-hole pattern with explicit plate-edge material validation."""
    parameters_only(parameters,("holes",),("holes",))
    g=input_geometry(process_input);width=number(g["width"],"plate width",True)
    height=number(g["height"],"plate height",True);thickness=number(g["thickness"],"thickness",True)
    center=vector(g["center"],"plate center");x=vector(g["xAxis"],"plate xAxis");y=vector(g["yAxis"],"plate yAxis")
    n=[x[1]*y[2]-x[2]*y[1],x[2]*y[0]-x[0]*y[2],x[0]*y[1]-x[1]*y[0]]
    holes=[]
    for h in parameters["holes"]:
        u=number(h["x"],"hole x");v=number(h["y"],"hole y");diameter=number(h["diameter"],"diameter",True)
        if abs(u)+diameter/2 >= width/2 or abs(v)+diameter/2 >= height/2:
            raise ValueError("板孔必须完整位于板件内并保留边缘材料")
        holes.append({"diameter":diameter,"placement":{"origin":[center[i]+x[i]*u+y[i]*v-n[i]*(thickness/2+1) for i in range(3)],
                      "xAxis":x,"yAxis":y},"vector":[value*(thickness+2) for value in n]})
    if not holes:return result(parameters,[],[])
    return _apertures({"holes":holes},parameters)


def male_head(process_input, parameters):
    parameters_only(parameters,("length","width","corner","size"),("length","width"))
    g=input_geometry(process_input);t=Tools()
    left=number(g["visibleLeft"],"visibleLeft");right=number(g["visibleRight"],"visibleRight")
    z=number(g["centerZ"],"centerZ");pw=number(g["profileWidth"],"profileWidth",True)
    depth=number(g["profileDepth"],"profileDepth",True)+max(4,4*number(g["wall"],"wall",True))
    length=number(parameters["length"],"length",True);width=number(parameters["width"],"width",True)
    corner=parameters.get("corner","none");size=number(parameters.get("size",0),"size")
    if right<=left or corner not in {"round","chamfer","none","square"}:raise ValueError("invalid head geometry")
    def prism(key,contour,cx=0,cz=0):
        return t.solid(key,{"placement":{"origin":[cx,-depth/2,cz],"xAxis":[1,0,0],"yAxis":[0,0,1]},"contours":[contour],"vector":[0,depth,0]})
    body=prism("body",path([[left-EPS,z-pw],[right+EPS,z-pw],[right+EPS,z+pw],[left-EPS,z+pw]]))
    def head(a,b,tip):
        cx=(a+b)/2;half=width/2;span=b-a
        if corner=="round":
            radius=min(size,span/2-EPS,half-EPS)
            if radius<=0:raise ValueError("invalid rounded head radius")
            return prism(tip,rounded_rectangle(span,width,radius),cx,z)
        if corner=="chamfer":
            leg=min(size,span-EPS,half-EPS)
            if leg<=0:raise ValueError("invalid head chamfer")
            points=([[a,z-half+leg],[a+leg,z-half],[b,z-half],[b,z+half],[a+leg,z+half],[a,z+half-leg]] if tip=="left" else
                    [[a,z-half],[b-leg,z-half],[b,z-half+leg],[b,z+half-leg],[b-leg,z+half],[a,z+half]])
        else:points=[[a,z-half],[b,z-half],[b,z+half],[a,z+half]]
        return prism(tip,path([[x-cx,y-z] for x,y in points]),cx,z)
    root=t.union("envelope",[body,head(left-length,left+EPS,"left"),head(right-EPS,right+length,"right")])
    return result(parameters,t.geometry,[{"id":"male-head","kind":"neutral-csg","role":"stock","operation":"intersect","tool":root}])


FUNCTIONS={"structural-stock-fit":stock_fit,"structural-profile-pocket":profile_pocket,
           "structural-apertures":apertures,"structural-male-head":male_head,
           "structural-plate-apertures":plate_apertures,"structural-planar-trim":planar_trim}
