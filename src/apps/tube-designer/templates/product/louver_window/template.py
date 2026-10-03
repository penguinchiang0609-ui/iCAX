"""Fixed tube louvers. X=width, Y=depth, Z=height.
phi: axis direction in XZ; theta: wide section side rolled from Y toward
the in-plane array normal. Display places shared stock; CAM owns exact end cuts.
"""
from copy import deepcopy
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import sys
from icax_template_sdk import NeutralModel, to_resource_model, display_context, to_display_model
from icax_template_sdk import manufacturing_context, manufacturing_declaration, to_manufacturing_model


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


def _external_rectangular_mirror_center(contours):
    center = None
    for contour in contours:
        segments = contour.get("segments", [])
        if (contour.get("kind") != "path" or contour.get("closed") is not True
                or len(segments) != 4 or any(
                    segment.get("kind") != "line"
                    or ((segment["start"][0] == segment["end"][0])
                        == (segment["start"][1] == segment["end"][1]))
                    for segment in segments)):
            raise ValueError("百叶窗提供截面的横向原管暂只支持镜像对称矩形轮廓")
        points = {tuple(point) for segment in segments
                  for point in (segment["start"], segment["end"])}
        xs, ys = sorted({point[0] for point in points}), sorted({point[1] for point in points})
        if len(xs) != 2 or len(ys) != 2 or points != {
                (x, y) for x in xs for y in ys}:
            raise ValueError("百叶窗提供截面的横向原管暂只支持镜像对称矩形轮廓")
        current = (ys[0] + ys[1]) / 2
        if center is not None and abs(center - current) > 1.0e-8:
            raise ValueError("百叶窗提供截面的内外轮廓没有共同镜像基准")
        center = current
    return center


def _external_section_geometry(start, end, span, arguments, profile, swap_axes):
    placement = arguments["placement"]
    length = math.dist(start, end)
    axis = [(end[i] - start[i]) / length for i in range(3)]
    x_axis, y_axis = placement["xAxis"], placement["yAxis"]
    canonical = profile.contours()
    emitted = arguments["contours"]
    if emitted != canonical:
        if emitted != [swap_axes(contour) for contour in canonical]:
            raise ValueError("百叶窗原管截面与提供的管型轮廓不一致")
        x_axis, y_axis = y_axis, x_axis
    cross = [x_axis[1] * y_axis[2] - x_axis[2] * y_axis[1],
             x_axis[2] * y_axis[0] - x_axis[0] * y_axis[2],
             x_axis[0] * y_axis[1] - x_axis[1] * y_axis[0]]
    alignment = dot(cross, axis)
    mirror_offset = 0.0
    old_y_axis = y_axis
    if alignment < -1.0 + 1.0e-8:
        if profile.properties().get("geometrySource") == "providedBoundary":
            mirror_offset = 2 * _external_rectangular_mirror_center(canonical)
        y_axis = [-value for value in y_axis]
    elif alignment < 1.0 - 1.0e-8:
        raise ValueError("百叶窗原管截面与构件轴向不一致")
    origin = [placement["origin"][i] + mirror_offset * old_y_axis[i]
              - axis[i] * span["stockInterval"]["startStation"]
              for i in range(3)]
    delta = [start[i] - origin[i] for i in range(3)]
    top = [-axis[2], 0.0, axis[0]]
    return {"originAtStart": origin, "xAxis": x_axis, "yAxis": y_axis,
            "centerlineUV": [dot(delta, x_axis), dot(delta, y_axis)],
            "faceNormals": {"top": top, "bottom": [-value for value in top],
                            "right": [0.0, 1.0, 0.0], "left": [0.0, -1.0, 0.0]}}


def _generate_external_geometry(parameters, context):
    """Expose uncut stock and the axis-aligned L/T product topology."""
    p = parameters
    if p["bladeConnection"] != "face_weld":
        raise ValueError("独立装配工艺仅支持顶接叶片；插槽与穿透需沿用原加工规则")
    if p["bladeDirectionAngle"] not in (0, 90, -90, 180, -180):
        raise ValueError("斜向叶片需要端部裁切；独立装配工艺仅支持水平或竖直叶片")
    columns, rows = p["middlePostCount"], p["middleBeamCount"]
    if (columns or rows) and p["supportMode"] != "split":
        raise ValueError("叶片穿过中间支撑需要开槽；独立装配工艺仅支持分格分段")

    # Built-in frame joints and weld-end clearance are retained as drafts. The
    # node templates, rather than these fields, will determine finished cuts.
    effective = dict(p, frameJoint="side_wrap", endClearance=0)
    state = layout(effective)
    shared = _shared("shared_tube_geometry.py")
    frames = _shared("security_window_frame_geometry.py")
    swap_axes = _shared("tube_profile_catalog.py")._swap_contour_axes
    model = NeutralModel(template_id="louver-window", template_version="2.1.0",
                         package_digest=str(context.get("template", {}).get("packageDigest", "")),
                         parameters=deepcopy(p))
    geometry = shared.SharedTubeGeometry(model)
    stock_rules = _shared("assembly_stock_allowance.py")
    frame, blade = state["frame"], state["blade"]
    face, width, height = frame.width, float(p["width"]), float(p["height"])
    xs = [face/2] + [face+(width-2*face)*i/(columns+1)
                     for i in range(1, columns+1)] + [width-face/2]
    zs = [face/2] + [face+(height-2*face)*j/(rows+1)
                     for j in range(1, rows+1)] + [height-face/2]
    members, keys, groups = {}, [], {}
    stock_spans = {}
    horizontal_section = frames._profile_arguments((0, 0, 0), (1, 0, 0), frame)
    vertical_section = frames._profile_arguments((0, 0, 0), (0, 0, 1), frame)
    vertical_allowance = stock_rules.opposing_half_extent(horizontal_section, (0, 0, 1), node_point=(0, 0, 0))
    horizontal_allowance = stock_rules.opposing_half_extent(vertical_section, (1, 0, 0), node_point=(0, 0, 0))

    def member(key, name, start, end, profile, material, *, blade_member=False, cell=None):
        length = math.dist(start, end)
        if length <= 0:
            raise ValueError(key+" 原管长度不足")
        axis = tuple((end[i]-start[i])/length for i in range(3))
        allowance = horizontal_allowance if abs(axis[0]) > abs(axis[2]) else vertical_allowance
        span = stock_rules.stock_span(start, end, allowance, allowance)
        stock_spans[key] = span
        if blade_member:
            profile_arguments = {
                "placement": {"origin": span["start"], "xAxis": list(state["xaxis"]),
                              "yAxis": list(state["yaxis"])},
                "contours": profile.contours()}
            solid = geometry.emit_tube(key, profile_arguments=profile_arguments,
                extrude_arguments={"vector": [span["end"][i]-span["start"][i]
                                              for i in range(3)]})
        else:
            part = frames.Part(key, name, tuple(span["start"]), tuple(span["end"]), profile, "frame")
            profile_arguments = frames._profile_arguments(part.start, part.end, profile)
            solid = frames._emit_tube_geometry(model, part, geometry)[0]
        section_frame = _external_section_geometry(
            start, end, span, profile_arguments, profile, swap_axes)
        props = {"length": span["length"], "quantity": 1,
                 "partNumber": p["productCode"]+"-"+key,
                 "manufacturing.partKind": "tube", "manufacturing.sourcing": "made",
                 "manufacturing.materialGrade": material,
                 "manufacturing.categoryKey": "louver."+key.split(".")[0],
                 "tubeDesigner.profile": profile.properties(),
                 "tubeDesigner.manufacturingAxis": list(axis),
                 "tubeDesigner.manufacturingStartToEnd": list(axis),
                 "tubeDesigner.endProcess": {"startCut": "square", "endCut": "square",
                                             "lengthBasis": "blank_axial_extent"},
                 "tubeDesigner.assemblyPlanning": {"stockState": "uncut", "ready": False},
                 "assemblyFrame.member": {"start": list(start), "end": list(end),
                                          "stockState": "uncut", "axisLength": length,
                                          "sectionFrame": section_frame,
                                          "stockInterval": span["stockInterval"]},
                 "louver.part": {"role": key.split(".")[0], "start": list(start),
                                 "end": list(end), **({"cell": cell} if cell is not None else {})}}
        keys.append(model.item(key, name, representations={"display": solid, "export": solid},
                               properties=props))
        members[key] = {"start": start, "end": end, "axis": axis,
                        "length": length, "profile": profile}
        signature = json.dumps([profile.properties(), material, round(span["length"], 6), name],
                               sort_keys=True)
        if signature not in groups:
            groups[signature] = {"key": key, "itemKey": key,
                                 "values": {"name": name, "quantity": 0,
                                            "length": span["length"], "material": material},
                                 "members": []}
        groups[signature]["values"]["quantity"] += 1
        groups[signature]["members"].append(key)

    for key, start, end in (
            ("frame.left", (xs[0], 0, zs[0]), (xs[0], 0, zs[-1])),
            ("frame.right", (xs[-1], 0, zs[0]), (xs[-1], 0, zs[-1])),
            ("frame.bottom", (xs[0], 0, zs[0]), (xs[-1], 0, zs[0])),
            ("frame.top", (xs[0], 0, zs[-1]), (xs[-1], 0, zs[-1]))):
        member(key, "边框管", start, end, frame, p["frameMaterial"])
    for i in range(1, columns+1):
        member(f"post.{i}", "中间立柱", (xs[i], 0, zs[0]), (xs[i], 0, zs[-1]),
               frame, p["frameMaterial"])
    for j in range(1, rows+1):
        for i in range(columns+1):
            member(f"beam.{j}.{i}", "中间横梁", (xs[i], 0, zs[j]),
                   (xs[i+1], 0, zs[j]), frame, p["frameMaterial"])

    horizontal = p["bladeDirectionAngle"] in (0, 180, -180)
    forward = p["bladeDirectionAngle"] in (0, 90)
    blade_hosts = {}
    for b in state["blades"]:
        ci, cj = divmod(b["cell"]-1, rows+1)
        left = "frame.left" if ci == 0 else f"post.{ci}"
        right = "frame.right" if ci == columns else f"post.{ci+1}"
        bottom = "frame.bottom" if cj == 0 else f"beam.{cj}.{ci}"
        top = "frame.top" if cj == rows else f"beam.{cj+1}.{ci}"
        first, last = (left, right) if horizontal else (bottom, top)
        if set(b["receivers"]) != {first, last}:
            raise ValueError("叶片触及第三侧支撑，当前不能拆成独立 T 节点")
        if horizontal:
            ordinate = (b["start"][2]+b["end"][2])/2
            start = (xs[ci] if forward else xs[ci+1], 0, ordinate)
            end = (xs[ci+1] if forward else xs[ci], 0, ordinate)
        else:
            ordinate = (b["start"][0]+b["end"][0])/2
            start = (ordinate, 0, zs[cj] if forward else zs[cj+1])
            end = (ordinate, 0, zs[cj+1] if forward else zs[cj])
        member(b["key"], "百叶管", start, end, blade, p["bladeMaterial"],
               blade_member=True, cell=b["cell"])
        blade_hosts[b["key"]] = (first, last) if forward else (last, first)

    def manufacturing_face(info, normal):
        axis = info["axis"]
        local_z = (-axis[2], 0, axis[0])
        across_y = normal[1]
        across_z = dot(normal, local_z)
        if abs(across_y) > 0.99:
            return "right" if across_y > 0 else "left"
        if abs(across_z) > 0.99:
            return "top" if across_z > 0 else "bottom"
        raise ValueError("装配节点侧面法向与构件轴线不垂直")

    def end_anchor(key, end, point, normal=None):
        info = members[key]
        if math.dist(info[end], point) > 1e-6:
            raise ValueError(key+" 端点与连接点不一致")
        anchor = {"kind": "end", "end": end,
                  "stockAllowance": stock_spans[key]["allowances"][end]}
        if normal is not None:
            anchor["approachFace"] = manufacturing_face(info, normal)
            anchor["contactInset"] = stock_spans[key]["allowances"][end]
        return {"itemKey": key, "kind": "end", "end": end, "anchor": anchor,
                "localAxialStation": 0 if end == "start" else info["length"],
                "centerlinePoint": list(point)}

    def side_anchor(key, point, normal):
        info = members[key]
        delta = [point[i]-info["start"][i] for i in range(3)]
        station = dot(delta, info["axis"])
        closest = [info["start"][i]+station*info["axis"][i] for i in range(3)]
        if not 1e-6 < station < info["length"]-1e-6 or math.dist(closest, point) > 1e-6:
            raise ValueError(key+" 侧面连接点不在构件中段")
        face_name = manufacturing_face(info, normal)
        return {"itemKey": key, "kind": "side", "face": face_name,
                "anchor": {"kind": "side", "face": face_name,
                           "reference": "start", "station": station},
                "localAxialStation": station, "centerlinePoint": list(point),
                "faceNormal": list(normal),
                "contactPoint": [point[i]+normal[i]*info["profile"].width/2
                                 for i in range(3)]}

    corners = (
        ("bottom-left", "frame.left", "start", (1, 0, 0),
         "frame.bottom", "start", (0, 0, 1)),
        ("bottom-right", "frame.right", "start", (-1, 0, 0),
         "frame.bottom", "end", (0, 0, 1)),
        ("top-left", "frame.left", "end", (1, 0, 0),
         "frame.top", "start", (0, 0, -1)),
        ("top-right", "frame.right", "end", (-1, 0, 0),
         "frame.top", "end", (0, 0, -1)))
    for label, vertical, v_end, v_normal, horizontal_key, h_end, h_normal in corners:
        point = members[vertical][v_end]
        model.relationship("frame.corner."+label, "assembly", [vertical, horizontal_key],
                           properties={"topology": "L", "centerlinePoint": list(point),
                                       "participantAnchors": [
                                           end_anchor(vertical, v_end, point, v_normal),
                                           end_anchor(horizontal_key, h_end, point, h_normal)]})

    def tee(key, host, branch, branch_end):
        info = members[branch]
        point = info[branch_end]
        normal = info["axis"] if branch_end == "start" else tuple(-v for v in info["axis"])
        host_anchor = side_anchor(host, point, normal)
        branch_anchor = end_anchor(branch, branch_end, point)
        branch_anchor["anchor"]["contactInset"] = math.dist(
            point, host_anchor["contactPoint"])
        model.relationship(key, "assembly", [host, branch],
                           properties={"topology": "T", "centerlinePoint": list(point),
                                       "participantAnchors": [host_anchor, branch_anchor]})

    for i in range(1, columns+1):
        tee(f"post.{i}.bottom", "frame.bottom", f"post.{i}", "start")
        tee(f"post.{i}.top", "frame.top", f"post.{i}", "end")
    for j in range(1, rows+1):
        for i in range(columns+1):
            branch = f"beam.{j}.{i}"
            tee(branch+".left", "frame.left" if i == 0 else f"post.{i}", branch, "start")
            tee(branch+".right", "frame.right" if i == columns else f"post.{i+1}", branch, "end")
    for key, (first, last) in blade_hosts.items():
        tee(key+".start", first, key, "start")
        tee(key+".end", last, key, "end")

    model.output("display.default", "display", keys)
    model.output("export.manufacturing", "export", keys)
    model.table("parts", "框架与叶片原管（待节点工艺）", columns=[
        {"key": "name", "displayName": "名称", "valueType": "string"},
        {"key": "quantity", "displayName": "数量", "valueType": "integer"},
        {"key": "length", "displayName": "原管中心线长度", "valueType": "number", "unit": "mm"},
        {"key": "material", "displayName": "材质", "valueType": "string"}],
        rows=[{k: v for k, v in row.items() if k != "members"} for row in groups.values()])
    model.diagnostic("warning", "louver.assembly-unassigned",
                     "仅显示成品连接拓扑和方头原管；各 L/T 节点仍需选择工艺并复核整窗下料。")
    document = shared.finish_geometry_request(model, context)
    document.setdefault("extensions", {})["louver"] = {
        "arrays": state["arrays"], "projectedWidth": state["hp"],
        "projectedDepth": state["dp"], "overallDepth": state["depth"],
        "partGroups": [row["members"] for row in groups.values()],
        "assemblyPlanningMode": "external_templates", "assemblyCuttingReady": False,
        "insertionDimension": "unassigned"}
    return document


def _generate_document(parameters, context):
    planning_mode = parameters.get("assemblyPlanningMode", "builtin_rules")
    if planning_mode not in ("builtin_rules", "external_templates"):
        raise ValueError("装配工艺来源不受支持")
    if planning_mode == "external_templates":
        return _generate_external_geometry(parameters, context)
    state, p = layout(parameters), parameters
    shared, frames = _shared("shared_tube_geometry.py"), _shared("security_window_frame_geometry.py")
    model = NeutralModel(template_id="louver-window",template_version="2.1.0",
                         package_digest=str(context.get("template",{}).get("packageDigest","")),parameters=deepcopy(p))
    geometry = shared.SharedTubeGeometry(model)
    manufacturing = shared.request_geometry_purpose(context) != "display"
    keys, groups = [], {}
    frame, blade = state["frame"],state["blade"]
    process_runtime = _shared("assembly_geometry_process_runtime.py")
    def process(target, key, function_id, start, end, profile, local_geometry):
        return process_runtime.invoke(model,target,function_id,
            {"schema":"icax.assembly-process-input","schemaVersion":1,
             "parts":{"stock":{"start":list(start),"end":list(end),"length":math.dist(start,end),
                               "section":profile.properties()}},"geometry":local_geometry},
            {},key,key.split(".process.")[0])
    def region(polygon,depth):
        return {"placement":{"origin":[0,-depth/2,0],"xAxis":[1,0,0],"yAxis":[0,0,1]},
                "polygon":polygon,"vector":[0,depth,0]}
    def emit(key,start,end,profile,xaxis,yaxis):
        contours = profile.contours()
        return geometry.emit_tube(key,profile_arguments={
            "placement":{"origin":list(start),"xAxis":list(xaxis),"yAxis":list(yaxis)},
            "contours":contours},
            extrude_arguments={"vector":[end[j]-start[j] for j in range(3)]})
    def add(key,name,solid,profile,start,end,material,features,signature,display_solid=None):
        length = math.dist(start,end)
        section_arguments = ({"placement": {"origin": list(start),
                              "xAxis": list(state["xaxis"]), "yAxis": list(state["yaxis"])},
                              "contours": profile.contours()} if key.startswith("blade.")
                             else frames._profile_arguments(start, end, profile))
        section_frame = _external_section_geometry(
            start, end, {"stockInterval": {"startStation": 0}}, section_arguments,
            profile, _shared("tube_profile_catalog.py")._swap_contour_axes)
        profile_component = profile.properties()
        if section_arguments["contours"] != profile.contours():
            # Keep the original section resource and explicitly declare its
            # coordinate mapping into this member's actual section plane.
            profile_component["sectionCoordinateMap"] = [[0, 1], [1, 0]]
            source_frame = section_arguments["placement"]
            section_frame.update(originAtStart=list(source_frame["origin"]),
                                 xAxis=list(source_frame["xAxis"]), yAxis=list(source_frame["yAxis"]),
                                 centerlineUV=[0, 0])
        props = {"length":length,"quantity":1,"partNumber":p["productCode"]+"-"+key,
                 "manufacturing.partKind":"tube","manufacturing.materialGrade":material,
                 "manufacturing.sourcing":"made","manufacturing.categoryKey":"louver."+key.split(".")[0],
                 "tubeDesigner.profile":profile_component,
                 "assemblyFrame.member": {"start": list(start), "end": list(end),
                                          "axisLength": length, "sectionFrame": section_frame},
                 "tubeDesigner.endProcess":{"startCut":"plane","endCut":"plane",
                    "connection":state["connection"],"lengthBasis":"blank_axial_extent",
                    "cutSource":"finished_geometry","profileHoleCount":len(features.get("slotFeatures",[]))},
                 "louver.part":{"role":key.split(".")[0],"start":list(start),"end":list(end),**features}}
        keys.append(model.item(key,name,representations={"display":display_solid or solid,"export":solid},properties=props))
        signature = json.dumps([profile.properties(),material,round(length,6),signature],sort_keys=True)
        if signature not in groups:
            groups[signature] = {"key":key,"itemKey":key,"values":{"name":name,"quantity":0,"length":length,"material":material},"members":[]}
        groups[signature]["values"]["quantity"] += 1
        groups[signature]["members"].append(key)
    cutters = []
    for b in state["blades"]:
        key,start,end = b["key"],b["start"],b["end"]
        raw = emit(key,start,end,blade,state["xaxis"],state["yaxis"])
        solid = raw
        if manufacturing:
            extent=b["length"]+blade.width+blade.depth+20
            solid=process(raw,key+".process.trim","structural-planar-trim",start,end,blade,
                {"boundsPolygon":rectangle((-extent+start[0],extent+start[0],-extent+start[2],extent+start[2])),
                 "planes":[{"normal":[normal[0],normal[2]],"limit":limit} for normal,limit in b["planes"]],
                 "placement":{"origin":[0,-(state["depth"]+20)/2,0],"xAxis":[1,0,0],"yAxis":[0,0,1]},
                 "vector":[0,state["depth"]+20,0]})
        if manufacturing and (state["connection"]!="face_weld" or state["strategy"]=="through"):
            cl = state["clearance"]
            d = state["d"]
            a = (start[0]-d[0]*(cl+0.01),0,start[2]-d[1]*(cl+0.01))
            z = (end[0]+d[0]*(cl+0.01),0,end[2]+d[1]*(cl+0.01))
            section = {"geometryKind":"stock-section","placement":{"origin":list(a),"xAxis":list(state["xaxis"]),"yAxis":list(state["yaxis"])},
                       "contours":blade.contours()[:1],"vector":[z[j]-a[j] for j in range(3)],"clearance":cl,
                       "profileEnvelope":{"kind":"system" if blade._fixed_contours is None else "frozen",
                                          "profileId":blade.profile_id,"profileData":blade._profile_data}}
            bounds = b["bounds"]
            expanded = (bounds[0]-cl-0.01,bounds[1]+cl+0.01,bounds[2]-cl-0.01,bounds[3]+cl+0.01)
            cutters.append((b,{"section":section,"limits":[region(rectangle(expanded),state["depth"]+20)]}))
        local_planes = [{"normal":[dot(normal,state["xaxis"]),dot(normal,state["yaxis"]),
                                    dot(normal,(state["d"][0],0,state["d"][1]))],
                         "offset":limit-dot(normal,start)} for normal,limit in b["planes"]]
        features = {"cutPlanes":local_planes,"sectionXAxis":list(state["xaxis"]),
                    "sectionYAxis":list(state["yaxis"]),"cell":b["cell"],
                    "endReceivers":b["receivers"],
                    "directionAngle":p["bladeDirectionAngle"],"rollAngle":p["bladeRollAngle"]}
        rounded = [{"normal":[round(x,6) for x in v["normal"]],"offset":round(v["offset"],6)} for v in local_planes]
        add(key,"百叶管",solid,blade,start,end,p["bladeMaterial"],features,rounded,raw)
    for r in state["receivers"]:
        key,start,end = r["key"],r["start"],r["end"]
        part = frames.Part(key,key,start,end,frame,"frame")
        raw = frames._emit_tube_geometry(model,part,geometry)[0]
        display_solid=raw
        if r["polygon"] and manufacturing:
            raw = process(raw,key+".process.miter","structural-stock-fit",start,end,frame,
                          {"keepRegions":[region(r["polygon"],state["depth"]+20)]})
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
            features.append({"blade":b["key"],"tool":b["key"]+".slot","clearance":state["clearance"],
                             "position":[b["start"][j]-start[j] for j in range(3)]})
        solid = raw
        for index,pocket in enumerate(selected):
            solid = process(solid,key+".process.slot."+str(index),"structural-profile-pocket",start,end,frame,pocket)
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
            [key if features else "uncut-slots",cuts],display_solid)
        for index,feature in enumerate(features):
            model.relationship(f"joint.{key}.{index}","insert",[key,feature["blade"]],
                               properties={"clearance":state["clearance"],"geometry":"actual-outer-envelope"})
    if state["connection"]=="face_weld":
        for b in state["blades"]:
            for index,receiver in enumerate(dict.fromkeys(b["receivers"])):
                model.relationship(f"weld.{b['key']}.{index}","weld",[b["key"],receiver])
    model.output("display.default","display",keys)
    model.output("export.manufacturing","export",keys)
    if shared.request_geometry_purpose(context) != "manufacturing":
        model.diagnostic("info","louver.stock-preview","装配显示采用原管近似，斜切及插接处可能重叠；拼角、端切和槽孔以拆单加工模型为准。")
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


def _generate_resource_document(parameters, context):
    return to_resource_model(_generate_document(parameters, context))


def display(parameter_values):
    """Generate display data from the values owned by the product instance."""
    effective = dict(parameter_values, assemblyPlanningMode="builtin_rules")
    document = _generate_resource_document(effective, display_context(__file__))
    for item in document["items"]:
        profile = item.get("properties", {}).get("tubeDesigner.profile")
        if profile is None:
            continue
        arguments = {"placement": {"origin": [0, 0, 0], "xAxis": [1, 0, 0], "yAxis": [0, 1, 0]},
                     "contours": deepcopy(profile["contours"])}
        section = next((node["key"] for node in document["resources"]
                        if node["operator"] == "profile2d" and node["arguments"] == arguments), None)
        if section is None:
            section = "louver.section." + hashlib.sha256(
                json.dumps(arguments, sort_keys=True, separators=(",", ":")).encode()).hexdigest()[:16]
            document["resources"].append({"key": section, "operator": "profile2d", "inputs": [],
                                          "arguments": arguments})
        profile["sectionResource"] = section
    result = to_display_model(document)
    # The part component carries design facts. End planes, slots and receiving
    # members are declared by manufacturing, not published in the shared base.
    design_fields = {"role", "start", "end", "sectionXAxis", "sectionYAxis",
                     "cell", "directionAngle", "rollAngle"}
    for item in result["items"]:
        component = item.get("properties", {}).get("louver.part")
        if component is not None:
            item["properties"]["louver.part"] = {
                key: value for key, value in component.items() if key in design_fields}
    return result




def manufacturing(parameter_values):
    """Return manufacturing declarations from the values owned by the host."""
    with manufacturing_declaration():
        document = _generate_resource_document(parameter_values, manufacturing_context(__file__))
        result = to_manufacturing_model(document)
        if parameter_values.get("assemblyPlanningMode", "builtin_rules") == "external_templates":
            for item in document["items"]:
                if "assemblyFrame.member" not in item.get("properties", {}):
                    continue
                key = item["key"]
                connection_keys = [connection["key"] for connection in result["connections"]
                    if any(anchor.get("itemKey") == key and anchor.get("anchor", {}).get("kind") == "end"
                           for anchor in connection.get("properties", {}).get("participantAnchors", []))]
                result["processes"].append({"key": key + ".prepare", "kind": "assembly-process", "definition": {
                    "templateId": "profile-stock-preparation",
                    "processInput": {"schema": "icax.assembly-process-input", "schemaVersion": 2,
                        "parts": {"stock": {"scope": "manufacturing", "itemKey": key, "state": "initial"}},
                        "geometry": {"connectionKeys": connection_keys, "status": "await-external", "ready": False}},
                    "parameters": {}, "processDrafts": {}, "targets": {"stock": key}, "dependencies": []}})
        return result
