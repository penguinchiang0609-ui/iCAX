"""Security-window stock decomposition, independent of face infill generation.

Reference members retain assembly bounds; ordered paths own stock coordinates,
tool rotations, apertures and the manufacturing member identity.
"""
from __future__ import annotations

from dataclasses import dataclass, replace
from types import SimpleNamespace
import math


def sub(a, b):
    return tuple(x - y for x, y in zip(a, b))


def add(a, b):
    return tuple(x + y for x, y in zip(a, b))


def scale(a, k):
    return tuple(x * k for x in a)


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def cross(a, b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])


def unit(a):
    length = math.sqrt(dot(a, a))
    if length < 1e-8:
        raise ValueError("外框路径包含退化方向")
    return scale(a, 1 / length)


def rotate(v, axis, angle):
    c, s = math.cos(angle), math.sin(angle)
    return add(add(scale(v, c), scale(cross(axis, v), s)), scale(axis, dot(axis, v)*(1-c)))


@dataclass
class Span:
    reference: object
    start: tuple
    end: tuple
    direction: tuple = ()
    y_axis: tuple = ()
    z_axis: tuple = ()
    stock_start: float = 0
    start_reserve: float = 0
    end_reserve: float = 0


@dataclass
class FramePath:
    key: str
    name: str
    spans: list
    closed: bool = False


def mode(parameters):
    value = parameters.get("frameManufacturingMode", "segment_weld")
    if value not in ("segment_weld", "plane_v_notch", "spatial_v_notch"):
        raise ValueError("外框制造方式不受支持")
    if parameters.get("faceType") in ("single", "five") and value == "spatial_v_notch":
        return "plane_v_notch"
    return value


def plan(parts, points, parameters, layout):
    selected = mode(parameters)
    if selected == "segment_weld":
        return []
    height, half = float(parameters["height"]), parts[0].profile.width / 2
    posts = [p for p in parts if p.key.startswith("outer_frame.vertical.")]
    top = sorted((p for p in parts if p.key.startswith("outer_frame.top.")), key=lambda p:p.face_index)
    bottom = sorted((p for p in parts if p.key.startswith("outer_frame.bottom.")), key=lambda p:p.face_index)
    def at(i, z):
        return (points[i][0], points[i][1], z)
    def ring(rails, z):
        return [Span(p, at(i, z), at(i+1, z)) for i, p in enumerate(rails)]
    if selected == "plane_v_notch":
        # Terminal rails butt against the independent end posts. Intermediate
        # corners use their shared pivot, rather than the old trimmed rail ends.
        paths = []
        for label, rails, z in (("上", top, height-half), ("下", bottom, half)):
            spans = ring(rails, z)
            closed = layout == "five-face"
            if closed:
                back = next(p for p in parts if p.key.startswith("outer_frame.back.")
                            and p.face_index == (4 if label == "上" else 5))
                spans.append(Span(back, at(len(points)-1,z), at(0,z)))
                first = spans[0]
                midpoint = scale(add(first.start,first.end),.5)
                spans = [Span(first.reference,midpoint,first.end),*spans[1:],
                         Span(first.reference,first.start,midpoint)]
            else:
                spans[0].start, spans[-1].end = rails[0].start, rails[-1].end
            paths.append(FramePath("outer_frame.plane." + ("top" if label == "上" else "bottom"),
                                   label + ("完整方框" if closed else "L形折弯框" if len(rails)==2 else "U形折弯框"), spans, closed))
        return paths
    spans = ring(top, height-half)
    spans.append(Span(posts[-1], at(len(points)-1, height-half), at(len(points)-1, half)))
    spans.extend(Span(p, at(i+1, half), at(i, half)) for i, p in reversed(list(enumerate(bottom))))
    spans.append(Span(posts[0], at(0, half), at(0, height-half)))
    # Close on a straight edge: four/eight genuine fold corners and one flat
    # butt seam, using the same closure convention as the established rectangle.
    first = spans[0]
    midpoint = scale(add(first.start, first.end), .5)
    spans = [Span(first.reference, midpoint, first.end), *spans[1:],
             Span(first.reference, first.start, midpoint)]
    return [FramePath("outer_frame.spatial.continuous", "空间连续外围框", spans, True)]


def _prepare(path, fg, parameters):
    profile = path.spans[0].reference.profile
    fg._validate_bending_section(profile)
    if mode(parameters) == "spatial_v_notch":
        # Spatial stock changes bending face. A non-square section would also
        # change the assembled post/rail footprint, requiring another skeleton.
        if abs(profile.width-profile.depth)>1e-6:
            raise ValueError("空间连续外框目前需要宽深相等的闭口方管；矩形管可选平面折弯")
    proxy = fg.ContinuousFrame(path.key, path.name, 0, 0, 1, 1, profile, "main",
                              fg.CornerProcess("v_groove_90", "tool_library"), parameters, "outerFrameGroove")
    _, tool_values = fg._frame_mould_binding(proxy)
    angle = tool_values.get("angle",90)
    if isinstance(angle,bool) or not isinstance(angle,(int,float)) or not math.isfinite(angle) or abs(angle-90)>1e-7:
        raise ValueError("外框连续折弯需要90°槽口模具")
    bridge = tool_values.get("leaveBottom",tool_values.get("bridge",1))
    if isinstance(bridge,bool) or not isinstance(bridge,(int,float)) or not math.isfinite(bridge) or bridge<=0:
        raise ValueError("连续折弯槽口必须保留正厚度连接壁")
    allowance = proxy.bend_allowance
    spans, bends = path.spans, []
    spans[0].direction = unit(sub(spans[0].end, spans[0].start))
    spans[0].z_axis = unit(spans[0].reference.profile_y_axis)
    spans[0].y_axis = unit(cross(spans[0].z_axis, spans[0].direction))
    for i in range(len(spans)-1):
        a, b = spans[i:i+2]
        b.direction = unit(sub(b.end, b.start))
        if abs(dot(a.direction, b.direction)) > 1e-7:
            raise ValueError("连续外框目前仅支持90°相邻折角")
        axis = unit(cross(a.direction, b.direction))
        turn_y, turn_z = dot(b.direction, a.y_axis), dot(b.direction, a.z_axis)
        roll = math.degrees(math.atan2(-turn_y, turn_z))
        if abs(roll / 90 - round(roll / 90)) > 1e-6:
            raise ValueError("外框折角必须落在管材四个基准面上")
        reserve = abs(turn_y)*profile.depth/2 + abs(turn_z)*profile.width/2 - profile.wall
        if reserve < 0:
            raise ValueError("外框折弯连接壁厚超出截面尺寸")
        a.end_reserve = b.start_reserve = reserve
        b.y_axis = rotate(a.y_axis, axis, math.pi/2)
        b.z_axis = rotate(a.z_axis, axis, math.pi/2)
        bends.append({"sequence":i+1, "angle":90.0, "rotation":round(roll,6),
                      "origin":list(a.end), "axis":list(axis), "turnY":turn_y, "turnZ":turn_z})
    cursor = 0.0
    for i, span in enumerate(spans):
        span.stock_start = cursor
        cursor += math.dist(span.start, span.end) + span.start_reserve + span.end_reserve
        if i < len(bends):
            bends[i]["station"] = round(cursor + allowance/2, 6)
            cursor += allowance
    if path.closed:
        # Straight seam must close the cross-section too, not merely endpoints.
        if dot(spans[0].y_axis, spans[-1].y_axis)<1-1e-6 or dot(spans[0].z_axis, spans[-1].z_axis)<1-1e-6:
            raise ValueError("空间外框首尾截面朝向不一致，无法闭合")
    return profile, proxy, cursor, allowance, bends


def _stock_placement(span):
    axes = (span.direction, span.y_axis, span.z_axis)
    return {"origin":[span.stock_start+span.start_reserve-dot(span.start, axes[0]),
                       -dot(span.start, axes[1]), -dot(span.start, axes[2])],
            **{name:[axis[i] for axis in axes] for i,name in enumerate(("xAxis","yAxis","zAxis"))}}


def _segment_distance(a, b, c, d):
    """Closest distance of two finite 3D line segments, including parallel ones."""
    u,v,w=sub(b,a),sub(d,c),sub(a,c)
    uu,vv,uv,uw,vw=dot(u,u),dot(v,v),dot(u,v),dot(u,w),dot(v,w)
    def clamp(t):
        return min(1,max(0,t))
    candidates=[(0,clamp(vw/vv)),(1,clamp((vw+uv)/vv)),
                (clamp(-uw/uu),0),(clamp((uv-uw)/uu),1)]
    determinant=uu*vv-uv*uv
    if determinant>1e-9:
        s,t=(uv*vw-vv*uw)/determinant,(uu*vw-uv*uw)/determinant
        if 0<=s<=1 and 0<=t<=1:
            candidates.append((s,t))
    return min(math.dist(add(a,scale(u,s)),add(c,scale(v,t))) for s,t in candidates)


def _fold_clearance(path, bends, profile):
    """Plan tail-first folds and sample rigid tube envelopes during each fold.

    This is a product self-clearance check; no machine/tool fixture is implied.
    Corner reserves belong to laser stock geometry, not this rigid-span check.
    """
    vertices=[(0.0,0.0,0.0)]
    for span in path.spans:
        vertices.append(add(vertices[-1],(math.dist(span.start,span.end),0,0)))
    radius=math.hypot(profile.width,profile.depth)/2
    for i in reversed(range(len(bends))):
        axis=unit((0,-bends[i]["turnZ"],bends[i]["turnY"]))
        pivot=vertices[i+1]
        for angle in range(15,91,15):
            trial=vertices[:i+2]+[add(pivot,rotate(sub(p,pivot),axis,math.radians(angle)))
                                  for p in vertices[i+2:]]
            for a in range(len(path.spans)):
                for b in range(a+2,len(path.spans)):
                    if path.closed and a==0 and b==len(path.spans)-1:
                        continue
                    if _segment_distance(*trial[a:a+2],*trial[b:b+2]) < 2*radius-1e-5:
                        return {"method":"rigid_span_envelope_samples","angleStep":15,
                                "status":"interference", "sequence":i+1,"angle":angle,
                                "spanIndices":[a,b],"equipmentChecked":False}
            if angle==90:
                vertices=trial
    return {"method":"rigid_span_envelope_samples","angleStep":15,"status":"clear",
            "equipmentChecked":False}


def emit(path, core, fg, model, shared, parameters, crossing_map, purpose, user_root):
    profile, proxy, length, allowance, bends = _prepare(path, fg, parameters)
    clearance_check = _fold_clearance(path,bends,profile) if mode(parameters)=="spatial_v_notch" else None
    if clearance_check and clearance_check["status"] != "clear":
        raise ValueError(f"空间外框在第{clearance_check['sequence']}次折角的折合过程中发生框架自干涉；请改用平面折弯")
    stock = SimpleNamespace(**proxy.__dict__, length=length)
    stock.user_mould_root = user_root
    display = ""
    if purpose != "manufacturing":
        solids = []
        for i, span in enumerate(path.spans):
            args = {"placement":{"origin":list(span.start), "xAxis":list(span.y_axis), "yAxis":list(span.z_axis)},
                    "contours":profile.contours(swap_axes=True)}
            solids.append(shared.emit_tube(f"{path.key}.display.{i}", profile_arguments=args,
                                          extrude_arguments={"vector":list(sub(span.end, span.start))}))
        display = model.geometry(path.key+".display", "compound", inputs=solids)
    export = display
    if purpose != "display":
        base = fg.Part(path.key+".stock", path.name, (0,0,0), (length,0,0), profile, "main")
        export = fg._emit_tube_geometry(model, base, shared)[1]
        cutters = []
        for i, bend in enumerate(bends):
            roll = math.radians(bend["rotation"])
            rotated_profile = profile
            if abs(bend["turnY"])>.5:
                rotated_profile = replace(profile, width=profile.depth, depth=profile.width,
                                          _fixed_contours=profile.contours(swap_axes=True))
            stock.profile = rotated_profile
            cutter, extent = fg._emit_library_groove_cutter(model, stock, bend["station"], i+1)
            # Tool's local +Z follows the turn; retain its real mould geometry.
            cutter = model.geometry(f"{path.key}.groove.{i}.roll", "transform", inputs=[cutter], arguments={"placement":{
                "origin":[0,0,0], "xAxis":[1,0,0], "yAxis":[0,math.cos(roll),math.sin(roll)],
                "zAxis":[0,-math.sin(roll),math.cos(roll)]}})
            previous = bends[i-1]["station"] if i else 0
            following = bends[i+1]["station"] if i+1<len(bends) else length
            if bend["station"]+extent[0]<=previous or bend["station"]+extent[1]>=following:
                raise ValueError("外框槽口避空范围过大，与相邻折角或端部重叠")
            cutters.append(cutter)
        stock.profile = profile
        for i, span in enumerate(path.spans):
            for j, inserted in enumerate(crossing_map.get(span.reference.key, [])):
                low = min(dot(sub(p,span.start),span.direction) for p in (inserted.start,inserted.end))
                high = max(dot(sub(p,span.start),span.direction) for p in (inserted.start,inserted.end))
                extent = core._profile_half_extent(inserted,span.direction) + float(parameters["assemblyClearance"])
                low,high = low-extent,high+extent
                run = math.dist(span.start,span.end)
                if high<=-span.start_reserve+1e-7 or low>=run+span.end_reserve-1e-7:
                    continue
                source = core._emit_crossing_cutter(model, span.reference, inserted, i*10000+j+1,
                                                   float(parameters["assemblyClearance"]))
                mapped = model.geometry(f"{path.key}.aperture.{i}.{j}", "transform", inputs=[source],
                                        arguments={"placement":_stock_placement(span)})
                if low < -span.start_reserve or high > run+span.end_reserve:
                    # A seam aperture may occur at both stock ends. Crop each
                    # half to its own rigid span so it cannot cut another span.
                    left = span.stock_start
                    right = left+span.start_reserve+run+span.end_reserve
                    h=profile.width/2+1
                    clip = fg._emit_polygon_cutter(model, f"{path.key}.aperture.clip.{i}.{j}",
                        [[left,-h],[right,-h],[right,h],[left,h]], profile.depth/2+1)
                    mapped = model.geometry(f"{path.key}.aperture.crop.{i}.{j}", "boolean", inputs=[mapped,clip],
                                            arguments={"operation":"intersect","target":mapped,"tools":[clip]})
                cutters.append(mapped)
        if cutters:
            export = model.geometry(path.key+".export", "boolean", inputs=[export,*cutters],
                                    arguments={"operation":"subtract", "target":export, "tools":cutters})
    references = list(dict.fromkeys(s.reference.key for s in path.spans))
    properties = {"quantity":1, "group":"main", "length":round(length,3),
                  "manufacturing.categoryKey":"outer_frame.continuous", "manufacturing.categoryName":path.name,
                  "tubeDesigner.profile":fg._profile_properties(profile),
                  "tubeDesigner.cornerProcess":{"joinType":"v_groove_90", "grooveStyle":"tool_library",
                                                "bendAllowance":allowance, "bendLocations":[b["station"] for b in bends]},
                  "tubeDesigner.frameManufacturing":{"mode":mode(parameters), "closed":path.closed,
                    "closure":"straight_mid_edge" if path.closed else "open", "referenceMembers":references,
                    "spans":[{"start":list(s.start),"end":list(s.end),"stockStart":s.stock_start,
                              "startReserve":s.start_reserve,"endReserve":s.end_reserve,
                              "placement":_stock_placement(s)} for s in path.spans],
                    "bends":[{k:v for k,v in b.items() if k not in ("turnY","turnZ")} for b in bends],
                    "foldOrder":list(reversed([b["sequence"] for b in bends])),
                    "clearanceCheck":clearance_check,"assemblyAfterFolding":True},
                  "tubeDesigner.connectionProcess":{"cornerJoin":"v_groove_90", "passesInto":[],
                    "receives":list(dict.fromkeys(p.key for key in references for p in crossing_map.get(key,[])))}}
    return {"key":path.key, "name":path.name, "properties":properties,
            "representations":{"display":display or export,"export":export}}, references
