"""Fixed tube louvers. X=width, Y=depth, Z=height.
phi: axis direction in XZ; theta: wide section side rolled from Y toward
the in-plane array normal. Finished geometry is identical in preview/CAM.
"""
from copy import deepcopy
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import sys
from icax_template_sdk import NeutralModel


def _shared(filename):
    path = Path(__file__).resolve().parents[2] / "_shared" / filename
    name = "icax_louver_" + path.stem + hashlib.sha256(path.read_bytes()).hexdigest()[:16]
    if name not in sys.modules:
        spec = importlib.util.spec_from_file_location(name, path)
        module = importlib.util.module_from_spec(spec)
        sys.modules[name] = module
        spec.loader.exec_module(module)
    return sys.modules[name]


def dot(a, b):
    return sum(x*y for x, y in zip(a, b))


def clip_polygon(points, normal, limit):
    """Convex polygon intersected with dot(p,normal)<=limit."""
    result = []
    for a, b in zip(points, points[1:] + points[:1]):
        da, db = dot(a, normal)-limit, dot(b, normal)-limit
        if da <= 1e-9:
            result.append(a)
        if (da < -1e-9 and db > 1e-9) or (da > 1e-9 and db < -1e-9):
            t = da/(da-db)
            result.append([a[j]+t*(b[j]-a[j]) for j in range(2)])
    return result


def rectangle(bounds):
    l, r, b, t = bounds
    return [[l,b], [r,b], [r,t], [l,t]]


def path(points):
    return {"kind": "path", "closed": True, "segments": [
        {"kind": "line", "start": list(points[i]), "end": list(points[(i + 1) % len(points)])}
        for i in range(len(points))
    ]}


def prism(model, key, polygon, depth):
    face = model.geometry(key+".profile", "profile2d", arguments={
        "placement": {"origin": [0,-depth/2,0], "xAxis": [1,0,0], "yAxis": [0,0,1]},
        "contours": [path(polygon)]})
    return model.geometry(key+".volume", "extrude", inputs=[face], arguments={"vector": [0,depth,0]})


def boolean(model, key, target, tools, operation):
    if not tools:
        return target
    return model.geometry(key, "boolean", inputs=[target,*tools],
                          arguments={"operation": operation, "target": target, "tools": tools})


def layout(p):
    def number(key, low=0, high=10000, integer=False):
        v = p[key]
        if isinstance(v, bool) or not isinstance(v,(float,int)) or not math.isfinite(v) or not low <= v <= high:
            raise ValueError(key+" 超出有效范围")
        if integer and v != int(v):
            raise ValueError(key+" 必须是整数")
        return int(v) if integer else float(v)
    def choice(key, values):
        if p[key] not in values:
            raise ValueError(key+" 选项无效")
        return p[key]
    width, height = number("width",100), number("height",100)
    catalog = _shared("tube_profile_catalog.py")
    frame, blade = catalog.load_profile(p,"frame"), catalog.load_profile(p,"blade")
    if frame.profile_id != "rect" or blade.profile_id != "rect":
        raise ValueError("当前固定百叶采用矩形管截面")
    face = frame.width
    if min(width,height) <= 2*face:
        raise ValueError("边框内净尺寸不足")
    phi = math.radians(number("bladeDirectionAngle",-180,180))
    theta = math.radians(number("bladeRollAngle",-90,90))
    d, n = (math.cos(phi),math.sin(phi)), (-math.sin(phi),math.cos(phi))
    s, c = math.sin(theta),math.cos(theta)
    # Exact support function of the rounded-rectangle outer contour.
    radius = blade.radius
    hp = (blade.width-2*radius)*abs(s)+(blade.depth-2*radius)*abs(c)+2*radius
    dp = (blade.width-2*radius)*abs(c)+(blade.depth-2*radius)*abs(s)+2*radius
    xaxis, yaxis = (n[0]*s,c,n[1]*s), (n[0]*c,-s,n[1]*c)
    joint = choice("frameJoint",("side_wrap","horizontal_wrap","miter"))
    connection = choice("bladeConnection",("face_weld","slot_insert","through_insert"))
    strategy = choice("supportMode",("split","through"))
    if connection == "through_insert":
        strategy = "through"  # Retained split draft must not create overlapping stock.
    columns, rows = number("middlePostCount",0,10,True), number("middleBeamCount",0,10,True)
    gap = number("endClearance",0,10) if connection == "face_weld" else 0
    insert = number("insertDepth",0.1,200) if connection == "slot_insert" else 0
    protrusion = number("throughExtension",0,200) if connection == "through_insert" else 0
    clearance = number("slotClearance",0,5) if connection != "face_weld" or (strategy=="through" and columns+rows) else 0
    if connection == "slot_insert" and not frame.wall+clearance+0.01 < insert < face-frame.wall-clearance-0.01:
        raise ValueError("插入深度必须穿过内侧壁，且不能触及外侧壁")
    if connection == "slot_insert" and strategy == "split" and columns+rows and 2*(insert+clearance+0.01) >= face:
        raise ValueError("中间支撑两侧插入段会相交，请减小插深或改为整根穿过支撑")
    if connection != "face_weld" or (strategy=="through" and columns+rows):
        if dp+2*clearance >= frame.depth-2*max(frame.radius,frame.wall):
            raise ValueError("叶片及安装间隙超出边框侧面净深度，请增大边框深度")
    margin1, margin2 = number("startOffset",0,2000), number("endOffset",0,2000)
    mode = choice("arrayMode",("pitch","gap","overlap","count"))
    equal = p["equalEdgeMargin"]
    if not isinstance(equal,bool):
        raise ValueError("equalEdgeMargin 必须是布尔值")
    xs = [face/2]+[face+(width-2*face)*i/(columns+1) for i in range(1,columns+1)]+[width-face/2]
    zs = [face/2]+[face+(height-2*face)*i/(rows+1) for i in range(1,rows+1)]+[height-face/2]
    cells = [(xs[i]+face/2,xs[i+1]-face/2,zs[j]+face/2,zs[j+1]-face/2)
             for i in range(columns+1) for j in range(rows+1)]
    if any(min(r-l,t-b)<=0 for l,r,b,t in cells):
        raise ValueError("分格数量过多，中间支撑没有净空间")
    receivers = []
    def receiver(key, start, end, polygon=None):
        receivers.append(dict(key=key,start=start,end=end,polygon=polygon))
    if joint == "horizontal_wrap":
        ends = [("left",(face/2,0,face),(face/2,0,height-face)),
                ("right",(width-face/2,0,face),(width-face/2,0,height-face)),
                ("bottom",(0,0,face/2),(width,0,face/2)),
                ("top",(0,0,height-face/2),(width,0,height-face/2))]
    else:
        ends = [("left",(face/2,0,0),(face/2,0,height)),
                ("right",(width-face/2,0,0),(width-face/2,0,height)),
                ("bottom",(0 if joint=="miter" else face,0,face/2),(width if joint=="miter" else width-face,0,face/2)),
                ("top",(0 if joint=="miter" else face,0,height-face/2),(width if joint=="miter" else width-face,0,height-face/2))]
    miters = {"left":[[0,0],[face,face],[face,height-face],[0,height]],
              "right":[[width,0],[width,height],[width-face,height-face],[width-face,face]],
              "bottom":[[0,0],[width,0],[width-face,face],[face,face]],
              "top":[[0,height],[face,height-face],[width-face,height-face],[width,height]]}
    for name,start,end in ends:
        receiver("frame."+name,start,end,miters[name] if joint=="miter" else None)
    for i,x in enumerate(xs[1:-1],1):
        receiver(f"post.{i}",(x,0,face),(x,0,height-face))
    for j,z in enumerate(zs[1:-1],1):
        for i in range(columns+1):
            receiver(f"beam.{j}.{i}",(xs[i]+face/2,0,z),(xs[i+1]-face/2,0,z))
    if strategy == "through":
        cells = [(face,width-face,face,height-face)]
    blades, arrays = [], []
    for cell_index,(l,r,b,t) in enumerate(cells):
        ci, cj = divmod(cell_index, rows+1)
        owners = (("frame.left","frame.right","frame.bottom","frame.top") if strategy=="through" else
                  ("frame.left" if ci==0 else f"post.{ci}",
                   "frame.right" if ci==columns else f"post.{ci+1}",
                   "frame.bottom" if cj==0 else f"beam.{cj}.{ci}",
                   "frame.top" if cj==rows else f"beam.{cj+1}.{ci}"))
        corners = rectangle((l,r,b,t))
        lo = min(dot(v,n) for v in corners)+margin1+hp/2
        hi = max(dot(v,n) for v in corners)-margin2-hp/2
        if hi < lo-1e-7:
            raise ValueError("叶片投影和边距超出分格可布置空间")
        if mode == "count":
            count = number("bladeCount",1,200,True)
            pitch = (hi-lo)/(count-1) if count>1 else 0
        else:
            pitch = (number("bladePitch",0.01,2000) if mode=="pitch" else
                     hp+number("bladeGap",0,2000) if mode=="gap" else hp-number("bladeOverlap",0,2000))
            if pitch <= 0:
                raise ValueError("搭接量必须小于叶片投影宽度")
            count = int(math.floor((hi-lo)/pitch+1e-9))+1
        if count>200 or len(blades)+count>500:
            raise ValueError("叶片数量过多，请增加间距或减少分格")
        # Minkowski sum of two identical rounded rectangles. Visual overlap
        # alone is NOT a collision; test the actual section in its local axes.
        dx, dy = pitch*abs(s), pitch*abs(c)
        collide = (math.hypot(max(dx-(blade.width-2*radius),0),
                             max(dy-(blade.depth-2*radius),0)) < 2*radius-1e-7
                   if radius else dx < blade.width-1e-7 and dy < blade.depth-1e-7)
        if count>1 and collide:
            raise ValueError("相邻叶片截面包络干涉，请增大间距或调整滚转角")
        offset = ((hi-lo)-(count-1)*pitch)/2 if equal or count==1 else 0
        arrays.append(dict(cell=cell_index+1,count=count,pitch=pitch,projectedGap=pitch-hp if count>1 else None))
        for i in range(count):
            position = lo+offset+i*pitch
            # Depth is normal to the receiving frame wall, not along the blade.
            extra = -gap if connection=="face_weld" else insert if connection=="slot_insert" else face+protrusion
            bounds = (l-extra,r+extra,b-extra,t+extra)
            polygon = clip_polygon(rectangle(bounds),n,position+hp/2)
            polygon = clip_polygon(polygon,(-n[0],-n[1]),-position+hp/2)
            if len(polygon)<3:
                raise ValueError("斜向叶片裁剪后无有效截面")
            amin,amax = min(dot(v,d) for v in polygon),max(dot(v,d) for v in polygon)
            start = (d[0]*amin+n[0]*position,0,d[1]*amin+n[1]*position)
            end = (d[0]*amax+n[0]*position,0,d[1]*amax+n[1]*position)
            planes = [([1,0,0],bounds[1]), ([-1,0,0],-bounds[0]),
                      ([0,0,1],bounds[3]), ([0,0,-1],-bounds[2])]
            active = [(normal,limit) for normal,limit in planes
                      if any(abs(dot((v[0],0,v[1]),normal)-limit)<1e-6 for v in polygon)]
            blades.append(dict(key=f"blade.{len(blades)+1}", start=start,end=end,length=amax-amin,
                               bounds=bounds,polygon=polygon,position=position,cell=cell_index+1,planes=active,
                               receivers=[(owners[1],owners[0],owners[3],owners[2])[planes.index(v)] for v in active]))
    return dict(frame=frame,blade=blade,receivers=receivers,blades=blades,arrays=arrays,
                xaxis=xaxis,yaxis=yaxis,d=d,n=n,hp=hp,dp=dp,depth=max(frame.depth,dp),
                clearance=clearance,connection=connection,strategy=strategy)


def generate(parameters, context):
    state, p = layout(parameters), parameters
    shared, frames = _shared("shared_tube_geometry.py"), _shared("security_window_frame_geometry.py")
    model = NeutralModel(template_id="louver-window",template_version="2.0.0",
                         package_digest=str(context.get("template",{}).get("packageDigest","")),parameters=deepcopy(p))
    geometry = shared.SharedTubeGeometry(model)
    keys, groups = [], {}
    frame, blade = state["frame"],state["blade"]
    def emit(key,start,end,profile,xaxis,yaxis,outer=False,clearance=0):
        contours = profile.contours(clearance=clearance)
        return geometry.emit_tube(key,profile_arguments={
            "placement":{"origin":list(start),"xAxis":list(xaxis),"yAxis":list(yaxis)},
            "contours":contours[:1] if outer else contours},
            extrude_arguments={"vector":[end[j]-start[j] for j in range(3)]})
    def add(key,name,solid,profile,start,end,material,features,signature):
        length = math.dist(start,end)
        props = {"length":length,"quantity":1,"partNumber":p["productCode"]+"-"+key,
                 "manufacturing.partKind":"tube","manufacturing.materialGrade":material,
                 "manufacturing.sourcing":"made","manufacturing.categoryKey":"louver."+key.split(".")[0],
                 "tubeDesigner.profile":profile.properties(),
                 "tubeDesigner.endProcess":{"startCut":"plane","endCut":"plane",
                    "connection":state["connection"],"lengthBasis":"blank_axial_extent",
                    "cutSource":"finished_geometry","profileHoleCount":len(features.get("slotFeatures",[]))},
                 "louver.part":{"role":key.split(".")[0],"start":list(start),"end":list(end),**features}}
        keys.append(model.item(key,name,representations={"display":solid,"export":solid},properties=props))
        signature = json.dumps([profile.properties(),material,round(length,6),signature],sort_keys=True)
        if signature not in groups:
            groups[signature] = {"key":key,"itemKey":key,"values":{"name":name,"quantity":0,"length":length,"material":material},"members":[]}
        groups[signature]["values"]["quantity"] += 1
        groups[signature]["members"].append(key)
    cutters = []
    for b in state["blades"]:
        key,start,end = b["key"],b["start"],b["end"]
        raw = emit(key,start,end,blade,state["xaxis"],state["yaxis"])
        keep = prism(model,key+".keep",rectangle(b["bounds"]),state["depth"]+20)
        solid = boolean(model,key+".finished",raw,[keep],"intersect")
        if state["connection"]!="face_weld" or state["strategy"]=="through":
            cl = state["clearance"]
            d = state["d"]
            a = (start[0]-d[0]*(cl+0.01),0,start[2]-d[1]*(cl+0.01))
            z = (end[0]+d[0]*(cl+0.01),0,end[2]+d[1]*(cl+0.01))
            tool = emit(key+".slot",a,z,blade,state["xaxis"],state["yaxis"],True,cl)
            bounds = b["bounds"]
            expanded = (bounds[0]-cl-0.01,bounds[1]+cl+0.01,bounds[2]-cl-0.01,bounds[3]+cl+0.01)
            cutkeep = prism(model,key+".slot.limit",rectangle(expanded),state["depth"]+20)
            tool = boolean(model,key+".slot.finished",tool,[cutkeep],"intersect")
            cutters.append((b,tool))
        local_planes = [{"normal":[dot(normal,state["xaxis"]),dot(normal,state["yaxis"]),
                                    dot(normal,(state["d"][0],0,state["d"][1]))],
                         "offset":limit-dot(normal,start)} for normal,limit in b["planes"]]
        features = {"cutPlanes":local_planes,"sectionXAxis":list(state["xaxis"]),
                    "sectionYAxis":list(state["yaxis"]),"cell":b["cell"],
                    "endReceivers":b["receivers"],
                    "directionAngle":p["bladeDirectionAngle"],"rollAngle":p["bladeRollAngle"]}
        rounded = [{"normal":[round(x,6) for x in v["normal"]],"offset":round(v["offset"],6)} for v in local_planes]
        add(key,"百叶管",solid,blade,start,end,p["bladeMaterial"],features,rounded)
    for r in state["receivers"]:
        key,start,end = r["key"],r["start"],r["end"]
        part = frames.Part(key,key,start,end,frame,"frame")
        raw = frames._emit_tube_geometry(model,part,geometry)[0]
        if r["polygon"]:
            keep = prism(model,key+".miter",r["polygon"],state["depth"]+20)
            raw = boolean(model,key+".miter.finished",raw,[keep],"intersect")
        selected, features = [], []
        vertical = abs(end[2]-start[2])>abs(end[0]-start[0])
        rb = ((start[0]-frame.width/2,start[0]+frame.width/2,start[2],end[2]) if vertical else
              (start[0],end[0],start[2]-frame.width/2,start[2]+frame.width/2))
        for b,tool in cutters:
            if state["connection"]=="face_weld" and key.startswith("frame."):
                continue
            polygon = rectangle(rb)
            cl = state["clearance"]+0.01
            for normal,limit in [((1,0),b["bounds"][1]+cl),((-1,0),-b["bounds"][0]+cl),
                                 ((0,1),b["bounds"][3]+cl),((0,-1),-b["bounds"][2]+cl),
                                 (state["n"],b["position"]+state["hp"]/2+2*cl),
                                 ((-state["n"][0],-state["n"][1]),-b["position"]+state["hp"]/2+2*cl)]:
                polygon = clip_polygon(polygon,normal,limit)
            if len(polygon)<3:
                continue
            if key.startswith(("post.","beam.")) and state["strategy"]=="through":
                along = (0,1) if vertical else (1,0)
                if abs(dot(along,state["d"])) > 1-1e-9:
                    raise ValueError("叶片与中间支撑平行重叠，请改变阵列边距或采用分格分段")
            selected.append(tool)
            features.append({"blade":b["key"],"tool":tool,"clearance":state["clearance"],
                             "position":[b["start"][j]-start[j] for j in range(3)]})
        solid = boolean(model,key+".slots",raw,selected,"subtract")
        ax, ay, az = ((1,0,0),(0,1,0),(0,0,1)) if vertical else ((0,1,0),(0,0,1),(1,0,0))
        trim = r["polygon"] or rectangle(rb)
        # Convex trim polygon has CCW winding; outward normals define keep<=0.
        cuts = []
        for a,b in zip(trim,trim[1:]+trim[:1]):
            normal = (b[1]-a[1],0,a[0]-b[0])
            norm = math.hypot(*normal)
            normal = tuple(v/norm for v in normal)
            cuts.append({"normal":[dot(normal,ax),dot(normal,ay),dot(normal,az)],
                         "offset":dot(normal,(a[0]-start[0],-start[1],a[1]-start[2]))})
        # Conservative receiver grouping: distinct slot patterns never merge.
        add(key,"边框管" if key.startswith("frame") else "中间支撑",solid,frame,start,end,
            p["frameMaterial"],{"slotFeatures":features,"miter":bool(r["polygon"]),
                "sectionXAxis":list(ax),"sectionYAxis":list(ay),"cutPlanes":cuts},
            [key if features else "uncut-slots",cuts])
        for index,feature in enumerate(features):
            model.relationship(f"joint.{key}.{index}","insert",[key,feature["blade"]],
                               properties={"clearance":state["clearance"],"geometry":"actual-outer-envelope"})
    if state["connection"]=="face_weld":
        for b in state["blades"]:
            for index,receiver in enumerate(dict.fromkeys(b["receivers"])):
                model.relationship(f"weld.{b['key']}.{index}","weld",[b["key"],receiver])
    model.output("display.default","display",keys)
    model.output("export.manufacturing","export",keys)
    model.table("parts","加工零件归并",columns=[
        {"key":"name","displayName":"名称","valueType":"string"},
        {"key":"quantity","displayName":"数量","valueType":"integer"},
        {"key":"length","displayName":"毛坯长度","valueType":"number","unit":"mm"},
        {"key":"material","displayName":"材质","valueType":"string"}],
        rows=[{k:v for k,v in row.items() if k!="members"} for row in groups.values()])
    document = shared.finish_geometry_request(model,context)
    document.setdefault("extensions",{})["louver"] = {
        "arrays":state["arrays"],"projectedWidth":state["hp"],"projectedDepth":state["dp"],
        "overallDepth":state["depth"],"partGroups":[row["members"] for row in groups.values()],
        "insertionDimension":"normal-to-receiver-wall"}
    return document
