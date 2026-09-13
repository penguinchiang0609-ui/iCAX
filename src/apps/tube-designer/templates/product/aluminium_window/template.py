"""System-driven aluminium windows, not a manufacturer's production recipe."""
from copy import deepcopy
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import sys
from icax_template_sdk import NeutralModel


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


def generate(parameters, context):
    system=module(Path(__file__).with_name("profile_system.py")).load_system(parameters)
    return generate_system(parameters,context,system)


def generate_system(parameters,context,system):
    p=parameters
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
    model=NeutralModel(template_id="aluminium-window",template_version="1.0.0",
                       package_digest=str(context.get("template",{}).get("packageDigest","")),parameters=deepcopy(p))
    geom=shared("shared_tube_geometry.py")
    builder=geom.SharedTubeGeometry(model)
    plate=shared("plate_geometry.py")
    parts,panels,apertures,bom,hardware,display,export,groups=[],[],[],[],[],[],[],{}
    def profile(role):
        return system.profile(role)[1]
    def face(role):
        return float(profile(role)["face"])
    def boolean(key,raw,tools,op):
        if not tools:return raw
        return model.geometry(key,"boolean",inputs=[raw,*tools],arguments={"operation":op,"target":raw,"tools":tools})
    def prism(key,polygon,y,depth):
        path=model.geometry(key+".profile","profile2d",arguments={
            "placement":{"origin":[0,y-depth/2,0],"xAxis":[1,0,0],"yAxis":[0,0,1]},
            "contours":[{"kind":"polygon","points":polygon}]})
        return model.geometry(key+".solid","extrude",inputs=[path],arguments={"vector":[0,depth,0]})
    def member(key,role,start,end,u,polygon=None,panel_id=""):
        ident,pf=system.profile(role)
        length=math.dist(start,end)
        if length<=0.01:raise ValueError(key+" 型材长度不足")
        z=tuple((end[i]-start[i])/length for i in range(3))
        v=(0,1,0)
        # Profile reference is a datum, not necessarily its envelope centre.
        a=math.radians(float(pf.get("referenceAngle",0)))
        axis_x=tuple(u[i]*math.cos(a)+v[i]*math.sin(a) for i in range(3))
        axis_y=tuple(-u[i]*math.sin(a)+v[i]*math.cos(a) for i in range(3))
        origin=plus(plus(start,axis_x,-pf["referenceOrigin"][0]),axis_y,-pf["referenceOrigin"][1])
        raw=builder.emit_tube(key,profile_arguments={
            "placement":{"origin":list(origin),"xAxis":list(axis_x),"yAxis":list(axis_y)},
            "contours":deepcopy(pf["contours"])},extrude_arguments={"vector":[end[i]-start[i] for i in range(3)]})
        cuts=[]
        if polygon:
            keep=prism(key+".trim",polygon,start[1],float(pf["depth"])+20)
            raw=boolean(key+".cut",raw,[keep],"intersect")
            for a,b in zip(polygon,polygon[1:]+polygon[:1]):
                normal=(b[1]-a[1],0,a[0]-b[0])
                norm=math.hypot(*normal)
                normal=tuple(x/norm for x in normal)
                cuts.append({"normal":[dot(normal,axis_x),dot(normal,axis_y),dot(normal,z)],
                             "offset":dot(normal,(a[0]-origin[0],0,a[1]-origin[2]))})
        else:
            cuts=[{"normal":[0,0,-1],"offset":0},{"normal":[0,0,1],"offset":length}]
        features=[]
        cutters=[]
        for index,rule in enumerate(system.data.get("machiningRules",{}).get(role,[])):
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
            if min(w,h)<=0 or station-h/2<=0 or station+h/2>=length or pos-w/2<=0 or pos+w/2>=pf["face"]:
                raise ValueError(role+" 加工孔槽超出料件边界")
            center=plus(plus(plus(start,u,pos),z,station),v,-pf["depth"]/2-1)
            contour=({"kind":"circle","radius":w/2} if rule["kind"]=="roundThroughDepth" else
                     {"kind":"polygon","points":[[-w/2,-h/2],[w/2,-h/2],[w/2,h/2],[-w/2,h/2]]})
            ck=key+f".machining.{index}"
            path=model.geometry(ck+".profile","profile2d",arguments={
                "placement":{"origin":list(center),"xAxis":list(u),"yAxis":list(z)},"contours":[contour]})
            cutters.append(model.geometry(ck+".solid","extrude",inputs=[path],arguments={"vector":[0,pf["depth"]+2,0]}))
            features.append({"kind":rule["kind"],"station":station,"across":pos,"width":w,"height":h})
        raw=boolean(key+".machined",raw,cutters,"subtract")
        profile_props={"schema":"icax.tube-profile","schemaVersion":2,"id":system.data["id"]+":"+ident,"packageVersion":system.data["revision"],
                       "kind":"fixed-section","profileForm":"fixed","displayName":pf.get("name",ident),
                       "specification":system.data["series"]+"/"+ident,"width":pf["face"],"depth":pf["depth"],
                       "wallThickness":pf.get("wallThickness",0),"cornerRadius":0,"hollow":len(pf["contours"])>1,
                       "contours":deepcopy(pf["contours"]),"geometrySource":"providedBoundary",
                       "sourceRevision":system.data["revision"],"profileSystemId":system.data["id"],
                       "contentDigest":hashlib.sha256(json.dumps(pf,sort_keys=True).encode('utf-8')).hexdigest()}
        record={"partId":key,"panelId":panel_id,"role":role,"profileId":ident,"length":length,
                "cutPlanes":cuts,"machiningFeatures":features,"quantity":1,"material":pf["material"],
                "assemblyTransform":{"origin":list(origin),"xAxis":list(axis_x),"yAxis":list(axis_y),"zAxis":list(z)}}
        props={"length":length,"quantity":1,"partNumber":p["productCode"]+"-"+key,
               "manufacturing.partKind":"tube","manufacturing.materialGrade":pf["material"],
               "manufacturing.categoryKey":"window."+role,"tubeDesigner.profile":profile_props,
               "tubeDesigner.endProcess":{"startCut":"plane","endCut":"plane","cutSource":"finished_geometry","lengthBasis":"blank_axial_extent"},
               "window.profilePart":record}
        item=model.item(key,role,representations={"display":raw,"export":raw},properties=props)
        display.append(item);export.append(item);parts.append(record)
        signature=json.dumps([ident,pf["material"],round(length,6),cuts,features],sort_keys=True)
        groups.setdefault(signature,[]).append(key)
        return item
    def ring(key,bounds,y,roles,joint,panel_id=""):
        l,r,b,t=bounds
        fl,fr,fb,ft=(face(roles[x]) for x in ("left","right","bottom","top"))
        for side_a,side_b in (("left","top"),("top","right"),("right","bottom"),("bottom","left")):
            system.compatible_profiles(roles[side_a],roles[side_b])
        if r-l<=fl+fr or t-b<=fb+ft:raise ValueError(key+" 扣减后没有有效洞口")
        if joint=="miter" and max(fl,fr,fb,ft)-min(fl,fr,fb,ft)>1e-7:
            raise ValueError("45°拼角要求相邻装配面宽相同；请按系列采用直拼规则")
        polygons={"left":[[l,b],[l+fl,b+fb],[l+fl,t-ft],[l,t]],
                  "right":[[r,b],[r,t],[r-fr,t-ft],[r-fr,b+fb]],
                  "bottom":[[l,b],[r,b],[r-fr,b+fb],[l+fl,b+fb]],
                  "top":[[l,t],[l+fl,t-ft],[r-fr,t-ft],[r,t]]}
        side_bottom=b+fb if joint=="horizontal_wrap" else b
        side_top=t-ft if joint=="horizontal_wrap" else t
        rail_left=l+fl if joint=="side_wrap" else l
        rail_right=r-fr if joint=="side_wrap" else r
        ends={"left":((l,y,side_bottom),(l,y,side_top),(1,0,0)),
              "right":((r,y,side_top),(r,y,side_bottom),(-1,0,0)),
              "bottom":((rail_right,y,b),(rail_left,y,b),(0,0,1)),
              "top":((rail_left,y,t),(rail_right,y,t),(0,0,-1))}
        for side,(start,end,u) in ends.items():
            member(key+"."+side,roles[side],start,end,u,polygons[side] if joint=="miter" else None,panel_id)
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
        member(f"mullion.{i+1}","mullion",(xs[i][1],0,b),(xs[i][1],0,zs[-2][1] if merged_top else t),(1,0,0))
    for j in range(rows-1):
        for i,(a,c) in enumerate([(l,r)] if merged_top and j==rows-2 else xs):
            member(f"transom.{j+1}.{i+1}","transom",(c,0,zs[j][1]),(a,0,zs[j][1]),(0,0,1))
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
                                         center=((l+r)/2,y,(b+t)/2),x_axis=(1,0,0),y_axis=(0,0,1))
        row={"partId":key,"kind":"mesh" if kind=="screen" else "glass","width":w,"height":h,
             "thickness":thick,"material":p["meshType"] if kind=="screen" else p["glassType"],"quantity":1}
        display.append(model.item(key,"纱网" if kind=="screen" else "玻璃",
            representations={"display":raw},properties={"window.filling":row,"manufacturing.sourcing":"purchased",
                **({"manufacturing.partKind":"glass","manufacturing.materialCategory":"glass"} if kind!="screen" else {})}))
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
    model.output("display.default","display",display)
    model.output("export.manufacturing","export",export)
    table_rows=[]
    by_key={part["partId"]:part for part in parts}
    for index,members in enumerate(groups.values()):
        part=by_key[members[0]]
        table_rows.append({"key":f"part.{index}","itemKey":members[0],"values":{
            "role":part["role"],"profile":part["profileId"],"length":part["length"],"quantity":len(members),"material":part["material"]}})
    model.table("profiles","型材拆单"+("" if system.ready else "（演示，禁止生产）"),columns=[
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
    document=geom.finish_geometry_request(model,context)
    document.setdefault("extensions",{})["windowAssembly"]={
        "profileSystem":{"id":system.data["id"],"series":system.data["series"],"revision":system.data["revision"],
                         "digest":system.digest,"productionReady":system.ready,"source":system.data.get("source","")},
        "apertures":apertures,"panels":panels,"profileParts":parts,"partGroups":list(groups.values()),
        "fillings":bom,"hardware":hardware,"previewState":"closed"}
    return document
