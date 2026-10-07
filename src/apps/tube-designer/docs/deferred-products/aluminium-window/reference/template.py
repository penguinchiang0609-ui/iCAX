"""System-driven aluminium windows, not a manufacturer's production recipe."""
from copy import deepcopy
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import sys
from icax_template_sdk import NeutralModel, to_resource_model, display_context, to_display_model
from icax_template_sdk import manufacturing_context, manufacturing_declaration, to_manufacturing_model
from icax_template_sdk.manufacturing import is_manufacturing_declaration


def module(path):
    path=Path(path)
    name="icax_window_"+path.stem+hashlib.sha256(path.read_bytes()).hexdigest()[:16]
    if name not in sys.modules:
        spec=importlib.util.spec_from_file_location(name,path)
        obj=importlib.util.module_from_spec(spec)
        sys.modules[name]=obj
        spec.loader.exec_module(obj)
    return sys.modules[name]


def shared(name):
    return module(Path(__file__).resolve().parents[2]/"_shared"/name)


def dot(a,b):
    return sum(x*y for x,y in zip(a,b))


def plus(a,b,factor=1):
    return tuple(a[i]+b[i]*factor for i in range(3))


def manufacturing_face(axis, world_normal):
    """Map an X/Z frame face to the directed member's manufacturing frame."""
    local_z=(-axis[2],0.0,axis[0])
    along_y=world_normal[1]
    along_z=dot(world_normal,local_z)
    if abs(along_y)>0.99:
        return "right" if along_y>0 else "left"
    if abs(along_z)>0.99:
        return "top" if along_z>0 else "bottom"
    raise ValueError("装配节点侧面与构件制造坐标系不对齐")


def _generate_resource_document(parameters, context):
    system=module(Path(__file__).with_name("profile_system.py")).load_system(parameters)
    return to_resource_model(generate_system(parameters,context,system))


def generate_system(parameters,context,system):
    p=parameters
    planning_mode=p.get("assemblyPlanningMode","builtin_rules")
    if planning_mode not in ("builtin_rules","external_templates"):
        raise ValueError("装配工艺来源无效")
    external=planning_mode=="external_templates"
    if not system.ready and context.get("geometryPurpose")!="display":
        raise ValueError("当前为演示或未核验型材系列，不能生产拆单。请接入有来源的厂家截面与工艺规则。")
    def number(key,low,high,integer=False):
        v=p[key]
        if type(v) not in (int,float) or not math.isfinite(v) or not low<=v<=high or (integer and int(v)!=v):
            raise ValueError(key+" 数值无效")
        return int(v) if integer else float(v)
    width,height=number("width",100,10000),number("height",100,10000)
    columns,rows=number("columns",1,3,True),number("rows",1,3,True)
    merged_top=bool(p["mergeTopLight"]) if columns>1 and rows>1 else False
    mode=p["windowType"]
    if mode not in ("fixed","sliding","hinged","mixed"):
        raise ValueError("窗型无效")
    model=NeutralModel(template_id="aluminium-window",template_version="1.1.0",
                       package_digest=str(context.get("template",{}).get("packageDigest","")),parameters=deepcopy(p))
    geom=shared("shared_tube_geometry.py")
    stock_rules=shared("assembly_stock_allowance.py")
    manufacturing=geom.request_geometry_purpose(context)!="display"
    builder=geom.SharedTubeGeometry(model)
    machining=shared("assembly_geometry_process_runtime.py")
    raw_prototypes={}
    plate=shared("plate_geometry.py")
    parts,panels,apertures,bom,hardware,display,export,groups=[],[],[],[],[],[],[],{}
    member_geometry={}
    pending_junctions=[]
    def profile(role):
        return system.profile(role)[1]
    def face(role):
        return float(profile(role)["face"])
    def opposing_allowance(role,u,member_axis,node_point,end):
        pf=profile(role)
        angle=math.radians(float(pf.get("referenceAngle",0)))
        depth_axis=(0,1,0)
        section_x=tuple(u[i]*math.cos(angle)+depth_axis[i]*math.sin(angle) for i in range(3))
        section_y=tuple(-u[i]*math.sin(angle)+depth_axis[i]*math.cos(angle) for i in range(3))
        # The end-node line is at the assembly-face centre, while the profile
        # contour is measured from its own potentially asymmetric datum.
        datum=pf["referenceOrigin"]
        origin=[node_point[i]-section_x[i]*datum[0]-section_y[i]*datum[1]
                -u[i]*pf["face"]/2 for i in range(3)]
        return stock_rules.opposing_end_stock({
            "placement":{"origin":origin,"xAxis":section_x,"yAxis":section_y},
            "contours":pf["contours"]},member_axis,node_point=node_point,end=end)
    def member(key,role,start,end,u,polygon=None,panel_id="",display_ends=None,
               stock_ends=None,design_axis_ends=None):
        ident,pf=system.profile(role)
        stock_allowances=(tuple(item["stockAllowance"] for item in stock_ends)
                          if external else (0.0,0.0))
        length=math.dist(start,end)
        if length<=0.01:raise ValueError(key+" 型材长度不足")
        z=tuple((end[i]-start[i])/length for i in range(3))
        v=(0,1,0)
        # Profile reference is a datum, not necessarily its envelope centre.
        a=math.radians(float(pf.get("referenceAngle",0)))
        axis_x=tuple(u[i]*math.cos(a)+v[i]*math.sin(a) for i in range(3))
        axis_y=tuple(-u[i]*math.sin(a)+v[i]*math.cos(a) for i in range(3))
        origin=plus(plus(start,axis_x,-pf["referenceOrigin"][0]),axis_y,-pf["referenceOrigin"][1])
        span=stock_rules.stock_span(start,end,*stock_allowances) if external else None
        stock_origin=plus(origin,z,-stock_allowances[0]) if external else origin
        stock_vector=([span["end"][i]-span["start"][i] for i in range(3)]
                      if external else [end[i]-start[i] for i in range(3)])
        raw=builder.emit_tube(key,profile_arguments={
            "placement":{"origin":list(stock_origin),"xAxis":list(axis_x),"yAxis":list(axis_y)},
            "contours":deepcopy(pf["contours"])},extrude_arguments={"vector":stock_vector})
        display_solid=raw
        if external and display_ends is not None:
            shown_start,shown_end=display_ends
            shift=dot(tuple(shown_start[i]-start[i] for i in range(3)),z)
            shown_origin=plus(origin,z,shift)
            display_solid=builder.emit_tube(key+".outline",profile_arguments={
                "placement":{"origin":list(shown_origin),"xAxis":list(axis_x),"yAxis":list(axis_y)},
                "contours":deepcopy(pf["contours"])},
                extrude_arguments={"vector":[shown_end[i]-shown_start[i] for i in range(3)]})
        cuts=[]
        if polygon:
            for a,b in zip(polygon,polygon[1:]+polygon[:1]):
                normal=(b[1]-a[1],0,a[0]-b[0])
                norm=math.hypot(*normal)
                normal=tuple(x/norm for x in normal)
                cuts.append({"normal":[dot(normal,axis_x),dot(normal,axis_y),dot(normal,z)],
                             "offset":dot(normal,(a[0]-origin[0],0,a[1]-origin[2]))})
        else:
            cuts=[{"normal":[0,0,-1],"offset":-stock_allowances[0] if external else 0},
                  {"normal":[0,0,1],"offset":span["stockInterval"]["endStation"] if external else length}]
        features=[]
        machining_sites=[]
        for index,rule in enumerate(() if external or not manufacturing else system.data.get("machiningRules",{}).get(role,[])):
            # Intrinsic extrusion grooves are already in the section. Only
            # explicitly declared through-depth secondary operations are cut.
            if rule.get("kind") not in ("roundThroughDepth","rectThroughDepth"):
                raise ValueError(role+" 包含未支持的加工规则，不能忽略后继续拆单")
            evaluate=module(Path(__file__).with_name("profile_system.py")).dimension
            variables={**system.constants,"Length":length,"Face":pf["face"],"Depth":pf["depth"]}
            station=evaluate(rule["station"],variables)
            pos=evaluate(rule["across"],variables)
            w=evaluate(rule["diameter"] if rule["kind"]=="roundThroughDepth" else rule["width"],variables)
            h=w if rule["kind"]=="roundThroughDepth" else evaluate(rule["height"],variables)
            machining_sites.append({"station":station,"across":pos,
                                    "parameters":{"kind":rule["kind"],"width":w,"height":h}})
            features.append({"kind":rule["kind"],"station":station,"across":pos,"width":w,"height":h})
        if manufacturing and not external:
            vector=[end[i]-start[i] for i in range(3)]
            stock_signature=json.dumps({"contours":pf["contours"],"xAxis":axis_x,
                                        "yAxis":axis_y,"vector":vector},
                                       sort_keys=True,separators=(",",":"),allow_nan=False)
            local_stock=raw_prototypes.get(stock_signature)
            if local_stock is None:
                prefix="shared.window.stock."+hashlib.sha256(stock_signature.encode("ascii")).hexdigest()
                local_stock=builder.emit_tube(prefix,profile_arguments={
                    "placement":{"origin":[0,0,0],"xAxis":list(axis_x),"yAxis":list(axis_y)},
                    "contours":deepcopy(pf["contours"])},extrude_arguments={"vector":vector})
                raw_prototypes[stock_signature]=local_stock
            raw=local_stock
            if design_axis_ends is not None and is_manufacturing_declaration():
                first,last=(dot(tuple(point[i]-design_axis_ends[0][i] for i in range(3)),z)
                            for point in (start,end))
                raw=machining.invoke(model,raw,'profile-stock-preparation',{
                    'schema':'icax.assembly-process-input','schemaVersion':1,'parts':{'stock':{}},
                    'geometry':{'axialInterval':{'startStation':first,'endStation':last},
                                'direction':'forward','status':'prepared','ready':True}}, {},
                    instance_id=key+'.prepare',stock_id=key)
            if polygon:
                local_polygon=[[x-origin[0],height-origin[2]] for x,height in polygon]
                raw=machining.invoke(model,raw,"extrusion-end-clip",{
                    "schema":"icax.assembly-process-input","schemaVersion":1,
                    "parts":{"stock":{"depth":pf["depth"]}},
                    "geometry":{"frame":{"origin":[0,start[1]-origin[1],0],"xAxis":[1,0,0],
                                            "yAxis":[0,0,1],"depthAxis":[0,1,0]},
                                "boundary":local_polygon}}, {},
                    instance_id=key+".end-envelope",stock_id=key)
            for index,site in enumerate(machining_sites):
                raw=machining.invoke(model,raw,"through-depth-aperture",{
                    "schema":"icax.assembly-process-input","schemaVersion":1,
                    "parts":{"stock":{"length":length,"width":pf["face"],"depth":pf["depth"]}},
                    "geometry":{"frame":{"origin":[start[i]-origin[i] for i in range(3)],"xAxis":list(u),
                                            "yAxis":list(z),"depthAxis":list(v)},
                                "station":site["station"],"across":site["across"]}},
                    site["parameters"],instance_id=key+f".machining.{index}",stock_id=key)
            raw=model.geometry(key+".processed.placed","transform",inputs=[raw],arguments={
                "placement":{"origin":list(origin),"xAxis":[1,0,0],
                             "yAxis":[0,1,0],"zAxis":[0,0,1]}})
        profile_props={"schema":"icax.tube-profile","schemaVersion":2,"id":system.data["id"]+":"+ident,"packageVersion":system.data["revision"],
                       "kind":"fixed-section","profileForm":"fixed","displayName":pf.get("name",ident),
                       "specification":system.data["series"]+"/"+ident,"width":pf["face"],"depth":pf["depth"],
                       "wallThickness":pf.get("wallThickness",0),"cornerRadius":0,"hollow":len(pf["contours"])>1,
                       "contours":deepcopy(pf["contours"]),"geometrySource":"providedBoundary",
                       "sourceRevision":system.data["revision"],"profileSystemId":system.data["id"],
                       "contentDigest":hashlib.sha256(json.dumps(pf,sort_keys=True).encode('utf-8')).hexdigest()}
        geometry_nodes={node['key']:node for node in model._document['geometry']}
        extrusion=geometry_nodes[geometry_nodes[display_solid]['inputs'][0]]
        profile_props['sectionResource']=extrusion['inputs'][0]
        stock_length=span["length"] if external else length
        record={"partId":key,"panelId":panel_id,"role":role,"profileId":ident,"length":stock_length,
                "cutPlanes":cuts,"machiningFeatures":features,"quantity":1,"material":pf["material"],
                "assemblyTransform":{"origin":list(stock_origin),"xAxis":list(axis_x),"yAxis":list(axis_y),"zAxis":list(z)}}
        end_process={"startCut":"square" if external else "plane",
                     "endCut":"square" if external else "plane",
                     "lengthBasis":"blank_axial_extent"}
        if not external:
            end_process["cutSource"]="finished_geometry"
        shown_length=(math.dist(*display_ends) if external and display_ends is not None else length)
        props={"length":stock_length if manufacturing else shown_length,"quantity":1,"partNumber":p["productCode"]+"-"+key,
               "manufacturing.partKind":"tube","manufacturing.materialGrade":pf["material"],
               "manufacturing.categoryKey":"window."+role,"tubeDesigner.profile":profile_props,
               "tubeDesigner.endProcess":end_process,
               "window.profilePart":record}
        if external:
            center_start=plus(start,u,pf["face"]/2)
            center_end=plus(end,u,pf["face"]/2)
            product_top=(-z[2],0.0,z[0])
            face_normals={"top":list(product_top),
                          "bottom":[-value for value in product_top],
                          "right":[0.0,1.0,0.0],"left":[0.0,-1.0,0.0]}
            member_geometry[key]={"start":center_start,"end":center_end,
                                  "axis":z,"length":length,"face":pf["face"],"u":u,
                                  "allowances":span["allowances"],
                                  "contactInsets":{"start":stock_ends[0]["contactInset"],
                                                   "end":stock_ends[1]["contactInset"]}}
            props.update({"tubeDesigner.manufacturingAxis":list(z),
                          "tubeDesigner.manufacturingStartToEnd":list(z),
                           "tubeDesigner.assemblyPlanning":{"stockState":"uncut","ready":False},
                           "assemblyFrame.member":{"start":list(center_start),"end":list(center_end),
                                                   "stockState":"uncut","axisLength":length,
                                                   "sectionFrame":{
                                                       "originAtStart":list(origin),
                                                       "xAxis":list(axis_x),"yAxis":list(axis_y),
                                                       "centerlineUV":[pf["referenceOrigin"][0]+pf["face"]*math.cos(a)/2,
                                                                       pf["referenceOrigin"][1]-pf["face"]*math.sin(a)/2],
                                                       "faceNormals":face_normals},
                                                   "stockInterval":span["stockInterval"]}})
        item=model.item(key,role,representations={"display":display_solid,"export":raw},properties=props)
        display.append(item);export.append(item);parts.append(record)
        signature=json.dumps([ident,pf["material"],round(stock_length,6),cuts,features],sort_keys=True)
        groups.setdefault(signature,[]).append(key)
        return item
    def end_anchor(key,end,point,approach_normal=None,contact_inset=False):
        info=member_geometry[key]
        if math.dist(info[end],point)>1e-5:
            raise ValueError(key+" 的端部不在声明的 L/T 连接点")
        anchor={"kind":"end","end":end,
                "stockAllowance":info["allowances"][end]}
        if contact_inset:
            anchor["contactInset"]=info["contactInsets"][end]
        if approach_normal is not None:
            anchor["approachFace"]=manufacturing_face(info["axis"],approach_normal)
        return {"itemKey":key,"kind":"end","end":end,"anchor":anchor,
                "localAxialStation":0.0 if end=="start" else info["length"],
                "centerlinePoint":list(point)}
    def side_anchor(key,point,normal):
        info=member_geometry[key]
        relative=tuple(point[i]-info["start"][i] for i in range(3))
        station=dot(relative,info["axis"])
        closest=plus(info["start"],info["axis"],station)
        if not 1e-5<station<info["length"]-1e-5 or math.dist(closest,point)>1e-5:
            raise ValueError(key+" 的侧面连接点不在构件中段")
        if abs(dot(normal,info["u"]))<0.99:
            raise ValueError(key+" 的侧面连接法向与型材装配面不一致")
        face=manufacturing_face(info["axis"],normal)
        contact=plus(point,normal,info["face"]/2)
        return {"itemKey":key,"kind":"side","face":face,
                "anchor":{"kind":"side","face":face,"reference":"start","station":station},
                "localAxialStation":station,"centerlinePoint":list(point),
                "faceNormal":list(normal),"contactPoint":list(contact)}
    def emit_junction(name,host,branch,branch_end,normal):
        point=member_geometry[branch][branch_end]
        host_anchor=side_anchor(host,point,normal)
        branch_anchor=end_anchor(branch,branch_end,point)
        branch_anchor["anchor"]["contactInset"]=math.dist(
            point,host_anchor["contactPoint"])
        model.relationship(name,"assembly",[host,branch],properties={
            "topology":"T","centerlinePoint":list(point),"participantAnchors":[
                host_anchor,branch_anchor]})
    def ring(key,bounds,y,roles,joint,panel_id=""):
        l,r,b,t=bounds
        fl,fr,fb,ft=(face(roles[x]) for x in ("left","right","bottom","top"))
        for side_a,side_b in (("left","top"),("top","right"),("right","bottom"),("bottom","left")):
            system.compatible_profiles(roles[side_a],roles[side_b])
        if r-l<=fl+fr or t-b<=fb+ft:raise ValueError(key+" 扣减后没有有效洞口")
        if not external and joint=="miter" and max(fl,fr,fb,ft)-min(fl,fr,fb,ft)>1e-7:
            raise ValueError("45°拼角要求相邻装配面宽相同；请按系列采用直拼规则")
        polygons={"left":[[l,b],[l+fl,b+fb],[l+fl,t-ft],[l,t]],
                  "right":[[r,b],[r,t],[r-fr,t-ft],[r-fr,b+fb]],
                  "bottom":[[l,b],[r,b],[r-fr,b+fb],[l+fl,b+fb]],
                  "top":[[l,t],[l+fl,t-ft],[r-fr,t-ft],[r,t]]}
        side_bottom=b+fb if joint=="horizontal_wrap" else b
        side_top=t-ft if joint=="horizontal_wrap" else t
        rail_left=l+fl if joint=="side_wrap" else l
        rail_right=r-fr if joint=="side_wrap" else r
        design_ends={"left":((l,y,b+fb/2),(l,y,t-ft/2),(1,0,0)),
               "right":((r,y,t-ft/2),(r,y,b+fb/2),(-1,0,0)),
               "bottom":((r-fr/2,y,b),(l+fl/2,y,b),(0,0,1)),
               "top":((l+fl/2,y,t),(r-fr/2,y,t),(0,0,-1))}
        ends=(design_ends
              if external else
              {"left":((l,y,side_bottom),(l,y,side_top),(1,0,0)),
               "right":((r,y,side_top),(r,y,side_bottom),(-1,0,0)),
               "bottom":((rail_right,y,b),(rail_left,y,b),(0,0,1)),
               "top":((rail_left,y,t),(rail_right,y,t),(0,0,-1))})
        display_ends={"left":((l,y,b),(l,y,t)),
                      "right":((r,y,t),(r,y,b)),
                      "bottom":((r,y,b),(l,y,b)),
                      "top":((l,y,t),(r,y,t))}
        neighbors={"left":("bottom","top"),"right":("top","bottom"),
                   "bottom":("right","left"),"top":("left","right")}
        for side,(start,end,u) in ends.items():
            stock_ends=None
            if external:
                axis=tuple((end[i]-start[i])/math.dist(start,end) for i in range(3))
                nodes=(plus(start,u,face(roles[side])/2),
                       plus(end,u,face(roles[side])/2))
                stock_ends=tuple(opposing_allowance(roles[neighbor],ends[neighbor][2],axis,
                                                    nodes[index],("start","end")[index])
                                 for index,neighbor in enumerate(neighbors[side]))
            member(key+"."+side,roles[side],start,end,u,
                   polygons[side] if not external and joint=="miter" else None,panel_id,
                   display_ends[side] if external else None,stock_ends,design_ends[side][:2])
        if external:
            corners=(("bottom-left","left","start","bottom","end",(1,0,0),(0,0,1)),
                     ("bottom-right","right","end","bottom","start",(-1,0,0),(0,0,1)),
                     ("top-left","left","end","top","start",(1,0,0),(0,0,-1)),
                     ("top-right","right","start","top","end",(-1,0,0),(0,0,-1)))
            for label,vertical,v_end,horizontal,h_end,v_normal,h_normal in corners:
                v_key,h_key=key+"."+vertical,key+"."+horizontal
                point=member_geometry[v_key][v_end]
                end_anchor(h_key,h_end,point,h_normal,contact_inset=True)
                model.relationship(key+".corner."+label,"assembly",[v_key,h_key],properties={
                    "topology":"L","centerlinePoint":list(point),"participantAnchors":[
                        end_anchor(v_key,v_end,point,v_normal,contact_inset=True),
                        end_anchor(h_key,h_end,point,h_normal,contact_inset=True)]})
        return (l+fl,r-fr,b+fb,t-ft)
    frame_roles={s:"frame."+s for s in ("left","right","bottom","top")}
    inner=ring("frame",(0,width,0,height),0,frame_roles,system.data["joints"]["frame"])
    l,r,b,t=inner
    mw=face("mullion") if columns>1 else 0
    th=face("transom") if rows>1 else 0
    def intervals(low,high,count,divider,prefix):
        if count==1:return [(low,high)]
        weights=[number(prefix+str(i+1),0.01,100) for i in range(count)]
        clear=high-low-(count-1)*divider
        if clear<=0:raise ValueError("分格过密")
        bounds=[];position=low
        for weight in weights:
            end=position+clear*weight/sum(weights)
            bounds.append((position,end));position=end+divider
        return bounds
    xs,zs=intervals(l,r,columns,mw,"columnWeight"),intervals(b,t,rows,th,"rowWeight")
    for i in range(columns-1):
        key=f"mullion.{i+1}"
        if external:
            lower=b-face("frame.bottom")/2
            upper=(zs[-2][1]+th/2 if merged_top else t+face("frame.top")/2)
            top_role="transom" if merged_top else "frame.top"
            top_u=(0,0,1) if merged_top else (0,0,-1)
            member(key,"mullion",(xs[i][1],0,lower),(xs[i][1],0,upper),(1,0,0),
                   display_ends=((xs[i][1],0,b),(xs[i][1],0,zs[-2][1] if merged_top else t)),
                   stock_ends=(opposing_allowance("frame.bottom",(0,0,1),(0,0,1),
                                                 (xs[i][1]+face("mullion")/2,0,lower),"start"),
                               opposing_allowance(top_role,top_u,(0,0,1),
                                                 (xs[i][1]+face("mullion")/2,0,upper),"end")))
            pending_junctions.append((f"{key}.bottom","frame.bottom",key,"start",(0,0,1)))
            top_host=(f"transom.{rows-1}.1" if merged_top else "frame.top")
            pending_junctions.append((f"{key}.top",top_host,key,"end",(0,0,-1)))
        else:
            member(key,"mullion",(xs[i][1],0,b),
                   (xs[i][1],0,zs[-2][1] if merged_top else t),(1,0,0))
    for j in range(rows-1):
        spanning=merged_top and j==rows-2
        for i,(a,c) in enumerate([(l,r)] if spanning else xs):
            key=f"transom.{j+1}.{i+1}"
            if external:
                left_host=("frame.left" if spanning or i==0 else f"mullion.{i}")
                right_host=("frame.right" if spanning or i==columns-1 else f"mullion.{i+1}")
                left_axis=l-face("frame.left")/2 if left_host=="frame.left" else xs[i-1][1]+mw/2
                right_axis=r+face("frame.right")/2 if right_host=="frame.right" else xs[i][1]+mw/2
                left_role="frame.left" if left_host=="frame.left" else "mullion"
                right_role="frame.right" if right_host=="frame.right" else "mullion"
                member(key,"transom",(right_axis,0,zs[j][1]),
                       (left_axis,0,zs[j][1]),(0,0,1),
                       display_ends=((c,0,zs[j][1]),(a,0,zs[j][1])),
                       stock_ends=(opposing_allowance(right_role,(-1,0,0) if right_role=="frame.right" else (1,0,0),(-1,0,0),
                                                     (right_axis,0,zs[j][1]+face("transom")/2),"start"),
                                   opposing_allowance(left_role,(1,0,0),(-1,0,0),
                                                     (left_axis,0,zs[j][1]+face("transom")/2),"end")))
                pending_junctions.append((key+".left",left_host,key,"end",(1,0,0)))
                pending_junctions.append((key+".right",right_host,key,"start",(-1,0,0)))
            else:
                member(key,"transom",(c,0,zs[j][1]),(a,0,zs[j][1]),(0,0,1))
    def filling(key,bounds,y,kind,variables):
        l,r,b,t=bounds
        prefix="fixedGlass" if kind=="fixed" else "mesh" if kind=="screen" else "sashGlass"
        local={**variables,"InnerWidth":r-l,"InnerHeight":t-b}
        w,h=system.rule(prefix+"Width",local),system.rule(prefix+"Height",local)
        if min(w,h)<=0:raise ValueError("玻璃/纱网规则扣减后尺寸无效")
        thick=number("glassThickness",1,30) if kind!="screen" else 0.5
        if kind!="screen" and not system.rule("glassMinThickness",local)<=thick<=system.rule("glassMaxThickness",local):
            raise ValueError("玻璃厚度超出当前型材系列的适配范围")
        # Visible size and procurement cut size are separate for inserted
        # glass/mesh. The series gives cut size; preview does not invent grooves.
        sw,sh=min(w,r-l),min(h,t-b)
        raw=plate.emit_rectangular_plate(model,key,width=sw,height=sh,thickness=thick,
                                         center=((l+r)/2,y,(b+t)/2),x_axis=(1,0,0),y_axis=(0,0,1),shared_geometry=builder)
        row={"partId":key,"kind":"mesh" if kind=="screen" else "glass","width":w,"height":h,
             "thickness":thick,"material":p["meshType"] if kind=="screen" else p["glassType"],"quantity":1}
        display.append(model.item(key,"纱网" if kind=="screen" else "玻璃",
            representations={"display":raw},properties={"window.filling":row,"manufacturing.sourcing":"purchased",
                **({"manufacturing.partKind":"glass","manufacturing.materialCategory":"glass"} if kind!="screen" else {"manufacturing.partKind":"accessory"})}))
        bom.append(row)
    for j,(z0,z1) in enumerate(zs):
        for i,(x0,x1) in enumerate([(l,r)] if merged_top and j==rows-1 else xs):
            aid=f"aperture.{j+1}.{i+1}"
            family=p[f"cell{j+1}{i+1}"] if mode=="mixed" else mode
            if merged_top and j==rows-1:family="fixed"
            if family not in ("fixed","sliding","hinged"):raise ValueError("洞口窗型无效")
            system.compatible(family)
            aw,ah=x1-x0,z1-z0
            variables={"Width":width,"Height":height,"ApertureWidth":aw,"ApertureHeight":ah}
            apertures.append({"id":aid,"type":family,"bounds":[x0,x1,z0,z1]})
            if family=="fixed":
                filling(aid+".glass",(x0,x1,z0,z1),0,"fixed",variables)
                panels.append({"panelId":aid+".fixed","apertureId":aid,"type":"fixed","bounds":[x0,x1,z0,z1]})
                continue
            count=number("slidingCount",2,4,True) if family=="sliding" else number("hingedCount",1,2,True)
            tracks=number("trackCount",2,4,True) if family=="sliding" else 1
            screen=bool(p["screenEnabled"]) if family=="sliding" else False
            screen_count=number("screenCount",1,2,True) if screen else 0
            if screen_count>count:raise ValueError("纱窗数量超过玻璃扇数")
            if tracks>system.data["compatibility"].get("maxTracks",1):raise ValueError("轨道数超出当前系列能力")
            if screen and tracks<3:raise ValueError("滑动纱窗需要独立轨道，请选择至少三轨")
            if screen:system.compatible("screen")
            track_indices,screen_track=system.track_layout(tracks,count,screen) if family=="sliding" else ([0]*count,None)
            variables["PanelCount"]=count
            pw,ph=system.rule(family+"Width",variables),system.rule(family+"Height",variables)
            if min(pw,ph)<=0:raise ValueError("窗扇尺寸扣减后无效")
            variables["PanelWidth"]=pw;variables["PanelHeight"]=ph
            spacing=system.rule("trackSpacing",variables) if family=="sliding" else 0
            if family=="sliding" and spacing<=0:raise ValueError("轨距必须为正")
            closed=[]
            for index in range(count):
                variables["PanelIndex"]=index
                left=x0+system.rule(family+"Start",variables)
                bottom=z0+system.rule(family+"Bottom",variables)
                if left<x0-1e-6 or left+pw>x1+1e-6 or bottom<z0-1e-6 or bottom+ph>z1+1e-6:
                    raise ValueError("窗扇尺寸/定位规则超出洞口，请检查系列扣减和搭接")
                track=track_indices[index]
                y=(track-(tracks-1)/2)*spacing
                role_map=({s:"hinged."+s for s in ("top","bottom","left","right")} if family=="hinged" else
                          {"top":"sliding.top","bottom":"sliding.bottom","left":"sliding.jamb" if index==0 else "sliding.interlock",
                           "right":"sliding.jamb" if index==count-1 else "sliding.interlock"})
                if family=="hinged" and count==2:
                    role_map["right" if index==0 else "left"]="hinged.meeting"
                for side in ("top","bottom","left","right"):
                    system.compatible_profiles(frame_roles[side],role_map[side])
                maxdepth=max(profile(role)["depth"] for role in role_map.values())
                if family=="sliding" and (maxdepth>=spacing or abs(y)+maxdepth/2>min(profile(r)["depth"] for r in frame_roles.values())/2):
                    raise ValueError("扇料深度与轨距/边框深度不兼容")
                pid=aid+f".{family}.{index+1}"
                bounds=(left,left+pw,bottom,bottom+ph)
                clear=ring(pid,bounds,y,role_map,system.data["joints"][family],pid)
                filling(pid+".glass",clear,y,family,variables)
                opening=("left" if index==0 else "right") if count==2 else p["openingSide"]
                if family=="hinged" and (opening not in ("left","right") or p["openingDirection"] not in ("inward","outward")):
                    raise ValueError("平开方向无效")
                panel={"panelId":pid,"apertureId":aid,"type":family,"trackIndex":track,
                       "closedPosition":[left,y,bottom],"width":pw,"height":ph,
                       "openingSide":opening if family=="hinged" else "sliding",
                       "openingDirection":p["openingDirection"] if family=="hinged" else "horizontal"}
                panels.append(panel);closed.append(panel)
                for rule in system.data.get("hardwareRules",{}).get(family,[]):
                    hardware.append({"panelId":pid,"name":rule["name"],"quantity":rule["quantity"]})
            # Travel is constrained by the aperture AND neighbours on the same track.
            if family=="sliding":
                for panel in closed:
                    left=panel["closedPosition"][0]
                    same=[other for other in closed if other is not panel and other["trackIndex"]==panel["trackIndex"]]
                    low=max([x0]+[q["closedPosition"][0]+q["width"] for q in same if q["closedPosition"][0]<left])
                    high=min([x1-pw]+[q["closedPosition"][0]-pw for q in same if q["closedPosition"][0]>left])
                    if low>left+1e-6 or high<left-1e-6:raise ValueError("同轨窗扇在关闭位置相交")
                    panel["travelRange"]=[low-left,high-left]
            for index in range(screen_count):
                variables["PanelIndex"]=index
                sw,sh=system.rule("screenWidth",variables),system.rule("screenHeight",variables)
                sy=(screen_track-(tracks-1)/2)*spacing
                sx=closed[index]["closedPosition"][0]
                sz=z0+system.rule("screenBottom",variables)
                if min(sw,sh)<=0 or sx+sw>x1+1e-6 or sz<z0 or sz+sh>z1:raise ValueError("纱窗规则超出洞口")
                roles={s:"screen."+s for s in ("top","bottom","left","right")}
                for side in roles:system.compatible_profiles(frame_roles[side],roles[side])
                if any(profile(role)["depth"]>=spacing or abs(sy)+profile(role)["depth"]/2>min(profile(r)["depth"] for r in frame_roles.values())/2 for role in roles.values()):
                    raise ValueError("纱窗料与独立轨道不兼容")
                pid=aid+f".screen.{index+1}"
                clear=ring(pid,(sx,sx+sw,sz,sz+sh),sy,roles,system.data["joints"]["screen"],pid)
                filling(pid+".mesh",clear,sy,"screen",variables)
                panels.append({"panelId":pid,"apertureId":aid,"type":"screen","trackIndex":screen_track,
                               "closedPosition":[sx,sy,sz],"width":sw,"height":sh,"travelRange":[x0-sx,x1-sw-sx]})
                for rule in system.data.get("hardwareRules",{}).get("screen",[]):
                    hardware.append({"panelId":pid,"name":rule["name"],"quantity":rule["quantity"]})
    aperture_bounds={a["id"]:a["bounds"] for a in apertures}
    for panel in panels:
        if panel["type"] not in ("sliding","screen"):continue
        same=[other for other in panels if other is not panel and other["type"] in ("sliding","screen")
              and other["apertureId"]==panel["apertureId"] and other["trackIndex"]==panel["trackIndex"]]
        left=panel["closedPosition"][0];right=left+panel["width"]
        x0,x1,_,_=aperture_bounds[panel["apertureId"]]
        low,high=x0,x1-panel["width"]
        for other in same:
            ol=other["closedPosition"][0];orr=ol+other["width"]
            if min(right,orr)-max(left,ol)>1e-6:
                raise ValueError("同轨窗扇或纱窗相交，请调整系列尺寸或轨道分配")
            if orr<=left+1e-6:low=max(low,orr)
            if ol>=right-1e-6:high=min(high,ol-panel["width"])
        panel["travelRange"]=[low-left,high-left]
    if external:
        for junction in pending_junctions:
            emit_junction(*junction)
    model.output("display.default","display",display)
    model.output("export.manufacturing","export",export)
    table_rows=[]
    by_key={part["partId"]:part for part in parts}
    for index,members in enumerate(groups.values()):
        part=by_key[members[0]]
        table_rows.append({"key":f"part.{index}","itemKey":members[0],"values":{
            "role":part["role"],"profile":part["profileId"],"length":part["length"],"quantity":len(members),"material":part["material"]}})
    model.table("profiles",("型材原料（待节点工艺）" if external else "型材拆单")+
                ("" if system.ready else "（演示，禁止生产）"),columns=[
        {"key":"role","displayName":"角色","valueType":"string"},{"key":"profile","displayName":"型材","valueType":"string"},
        {"key":"length","displayName":"毛坯长度","valueType":"number","unit":"mm"},
        {"key":"quantity","displayName":"数量","valueType":"integer"},{"key":"material","displayName":"材质","valueType":"string"}],rows=table_rows)
    model.table("purchased","玻璃与纱网采购",columns=[
        {"key":"kind","displayName":"类别","valueType":"string"},
        {"key":"width","displayName":"裁切宽","valueType":"number","unit":"mm"},
        {"key":"height","displayName":"裁切高","valueType":"number","unit":"mm"},
        {"key":"material","displayName":"选型","valueType":"string"},
        {"key":"quantity","displayName":"数量","valueType":"integer"}],
        rows=[{"key":row["partId"],"values":{k:row[k] for k in ("kind","width","height","material","quantity")}} for row in bom])
    model.table("hardware","五金清单",columns=[
        {"key":"panelId","displayName":"所属窗扇","valueType":"string"},
        {"key":"name","displayName":"五金","valueType":"string"},
        {"key":"quantity","displayName":"数量","valueType":"integer"}],
        rows=[{"key":f"hardware.{i}","values":row} for i,row in enumerate(hardware)])
    if not system.ready:model.diagnostic("warning","window.demonstration","结构演示：当前截面和尺寸不是厂家系列，生产拆单已禁用。")
    if external:
        model.diagnostic("warning","window.assembly-unassigned",
                         "场景仅显示成品连接外形；下料仍是方头原料。各节点尚须选择工艺并完成整窗适配。")
    elif geom.request_geometry_purpose(context)!="manufacturing":
        model.diagnostic("info","window.stock-preview","装配显示采用原型材近似，拼角处可能重叠；端切和二次孔槽以拆单加工模型为准。")
    document=geom.finish_geometry_request(model,context)
    document.setdefault("extensions",{})["windowAssembly"]={
        "profileSystem":{"id":system.data["id"],"series":system.data["series"],"revision":system.data["revision"],
                         "digest":system.digest,"productionReady":system.ready,"source":system.data.get("source","")},
        "assemblyPlanningMode":planning_mode,
        "assemblyCuttingReady":False if external else system.ready,
        "apertures":apertures,"panels":panels,"profileParts":parts,"partGroups":list(groups.values()),
        "fillings":bom,"hardware":hardware,"previewState":"closed"}
    return document


def display(parameter_values):
    """Generate display data from the values owned by the product instance."""
    design_values = deepcopy(parameter_values)
    design_values['assemblyPlanningMode'] = 'external_templates'
    result = to_display_model(_generate_resource_document(design_values, display_context(__file__)))
    for item in result['items']:
        properties = item.get('properties', {})
        part = properties.get('window.profilePart')
        if part is not None:
            for name in ('cutPlanes', 'machiningFeatures', 'assemblyTransform'):
                part.pop(name, None)
            part['length'] = properties['length']
    return result




def manufacturing(parameter_values):
    """Return manufacturing declarations from the values owned by the host."""
    with manufacturing_declaration():
        document = _generate_resource_document(parameter_values, manufacturing_context(__file__))
        result = to_manufacturing_model(document)
        if parameter_values.get('assemblyPlanningMode', 'builtin_rules') == 'external_templates':
            for item in document['items']:
                if 'assemblyFrame.member' not in item.get('properties', {}):
                    continue
                key = item['key']
                connections = [connection['key'] for connection in result['connections']
                    if any(anchor.get('itemKey') == key and anchor.get('anchor', {}).get('kind') == 'end'
                           for anchor in connection.get('properties', {}).get('participantAnchors', []))]
                result['processes'].append({'key': key + '.prepare', 'kind': 'assembly-process', 'definition': {
                    'templateId': 'profile-stock-preparation',
                    'processInput': {'schema': 'icax.assembly-process-input', 'schemaVersion': 2,
                        'parts': {'stock': {'scope': 'manufacturing', 'itemKey': key, 'state': 'initial'}},
                        'geometry': {'connectionKeys': connections, 'status': 'await-external', 'ready': False}},
                    'parameters': {}, 'processDrafts': {}, 'targets': {'stock': key}, 'dependencies': []}})
        return result
