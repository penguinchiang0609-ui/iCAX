"""Parametric door-face assemblies and machining geometry, not NC toolpaths."""
from copy import deepcopy
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import sys
from icax_template_sdk import NeutralModel


def shared(filename):
    path=Path(__file__).resolve().parents[2]/"_shared"/filename
    name="icax_door_"+path.stem+hashlib.sha256(path.read_bytes()).hexdigest()[:16]
    if name not in sys.modules:
        spec=importlib.util.spec_from_file_location(name,path)
        obj=importlib.util.module_from_spec(spec);sys.modules[name]=obj;spec.loader.exec_module(obj)
    return sys.modules[name]


def rect(box):
    l,r,b,t=box
    return [[l,b],[r,b],[r,t],[l,t]]


def path(points):
    return {"kind":"path", "closed":True, "segments":[
        {"kind":"line", "start":list(points[i]), "end":list(points[(i+1)%len(points)])}
        for i in range(len(points))
    ]}


def circle_path(radius):
    k=radius/math.sqrt(2.0)
    return {"kind":"path", "closed":True, "segments":[
        {"kind":"arc","start":[radius,0],"middle":[k,k],"end":[0,radius]},
        {"kind":"arc","start":[0,radius],"middle":[-k,k],"end":[-radius,0]},
        {"kind":"arc","start":[-radius,0],"middle":[-k,-k],"end":[0,-radius]},
        {"kind":"arc","start":[0,-radius],"middle":[k,-k],"end":[radius,0]},
    ]}


def clip_polygon(points,box):
    for normal,limit in [((1,0),box[1]),((-1,0),-box[0]),((0,1),box[3]),((0,-1),-box[2])]:
        result=[]
        for a,b in zip(points,points[1:]+points[:1]):
            da=sum(a[i]*normal[i] for i in range(2))-limit
            db=sum(b[i]*normal[i] for i in range(2))-limit
            if da<=1e-8:result.append(a)
            if (da<-1e-8 and db>1e-8) or (da>1e-8 and db<-1e-8):
                f=da/(da-db);result.append([a[i]+f*(b[i]-a[i]) for i in range(2)])
        points=result
    return points


def bounds(points):
    return min(v[0] for v in points),max(v[0] for v in points),min(v[1] for v in points),max(v[1] for v in points)


def overlap(a,b):
    return min(a[1],b[1])-max(a[0],b[0])>1e-7 and min(a[3],b[3])-max(a[2],b[2])>1e-7


def generate(parameters,context):
    p=parameters
    def num(key,low,high,integer=False):
        v=p[key]
        if type(v) not in (int,float) or not math.isfinite(v) or not low<=v<=high or (integer and int(v)!=v):
            raise ValueError(key+" 数值无效")
        return int(v) if integer else float(v)
    def enum(key,values):
        if p[key] not in values:raise ValueError(key+" 选项无效")
        return p[key]
    W,H,T=num("width",300,10000),num("height",300,4000),num("thickness",2,100)
    door=enum("doorType",("single","double","mother","multi"))
    count=1 if door=="single" else num("leafCount",2,6,True) if door=="multi" else 2
    gap=num("leafGap",1,30) if count>1 else 0
    composition=enum("composition",("repeat","mirror","continuous")) if count>1 else "repeat"
    widths=[(W-gap*(count-1))/count]*count
    if door=="mother":
        ratio=num("motherRatio",0.51,0.85)
        widths=[(W-gap)*ratio,(W-gap)*(1-ratio)]
        if enum("motherSide",("left","right"))=="right":widths.reverse()
    border=num("border",10,400)
    meeting=num("meetingBorder",10,200) if count>1 and composition=="continuous" else border
    if min(widths)<=2*max(border,meeting)+20 or H<=2*border+20:
        raise ValueError("门扇尺寸不足以容纳边框和装饰区")
    definitions=json.loads(Path(__file__).with_name("patterns.json").read_text(encoding="utf-8"))
    patterns={d["id"]:d for d in definitions["patterns"]}
    if p["pattern"] not in patterns:raise ValueError("未知装饰模块")
    pattern=patterns[p["pattern"]];generator=pattern["generator"]
    process=pattern["process"]
    if process=="region":process=enum("regionProcess",("through","recess"))
    through=process in ("through","glass")
    depth=T if through else num("cutDepth",0.2,50)
    sides=["front"] if through else (["front","back"] if enum("machiningSide",("front","back","both"))=="both" else [p["machiningSide"]])
    if not through and T-depth*len(sides)<num("minSkin",1,80):
        raise ValueError("加工深度超限，双面叠加后仍须保留最小板厚")
    vgroove=process=="line" and enum("lineTool",("flat","v"))=="v"
    angle=num("vAngle",30,120) if vgroove else 0
    groove=2*depth*math.tan(math.radians(angle/2)) if vgroove else (num("grooveWidth",1,100) if generator in ("parallel_paths","round_scene") else 0)
    if groove and groove<pattern["minimumFeature"]:raise ValueError("槽宽小于模块最小特征")
    trim=process=="panel" and bool(p["trimEnabled"])
    trimw=num("trimWidth",2,50) if trim else 0
    trimh=num("trimHeight",1,30) if trim else 0
    ncols=num("columns",1,12,True) if generator=="cell_regions" else 0
    nrows=num("rows",1,16,True) if generator=="cell_regions" else 0
    web=num("webWidth",5,200) if generator=="cell_regions" else 0
    if trim and web<2*trimw+5:raise ValueError("扣线宽度与单元间距冲突")
    layout=enum("layout",("full","center_band","offset_band","upper","lower","two_blocks"))
    band=num("bandRatio",0.1,0.9) if layout in ("center_band","offset_band") else 1
    position=num("bandOffset",0,1) if layout=="offset_band" else .5
    protect=bool(p["protectHardware"])
    hinge_side=enum("firstHingeSide",("left","right"))
    if protect:
        lock_z,lock_w,lock_h=num("lockHeight",100,3500),num("lockWidth",20,300),num("lockLength",50,700)
        hinge_w,hinge_h=num("hingeWidth",20,300),num("hingeLength",40,300)
        if lock_z-lock_h/2<0 or lock_z+lock_h/2>H or hinge_h>H*.24:
            raise ValueError("五金保留区超出门扇")
        if max(lock_w,hinge_w)>=min(widths):raise ValueError("五金区宽度超出门扇")
    model=NeutralModel(template_id="decorative-door",template_version="1.0.0",
                       package_digest=str(context.get("template",{}).get("packageDigest","")),parameters=deepcopy(p))
    plate=shared("plate_geometry.py");finish=shared("shared_tube_geometry.py").finish_geometry_request
    display,export,parts,features,zones_record=[],[],[],[],[]
    skipped=0
    def boolean(key,target,tools,operation):
        if not tools:return target
        return model.geometry(key,"boolean",inputs=[target,*tools],arguments={"operation":operation,"target":target,"tools":tools})
    def prism(key,contours,y,d,origin=(0,0)):
        outline=model.geometry(key+".profile","profile2d",arguments={
            "placement":{"origin":[origin[0],y,origin[1]],"xAxis":[1,0,0],"yAxis":[0,0,1]},
            "contours":contours})
        return model.geometry(key+".solid","extrude",inputs=[outline],arguments={"vector":[0,d,0]})
    def polygon_solid(key,points,y,d):
        return prism(key,[path(points)],y,d)
    def item(key,name,solid,width,height,thickness,kind="plate",purchased=False):
        props=plate.plate_properties(width,height,thickness,p["material"],"door."+kind,name)
        props.update({"partNumber":p["productCode"]+"-"+key,"door.surfaceFinish":p["finish"],
                      "door.ncReady":False,"manufacturing.sourcing":"purchased" if purchased else "made"})
        if kind=="glass":
            props["manufacturing.partKind"]="glass"
            props["manufacturing.materialCategory"]="glass"
        if purchased:props["manufacturing.material"]=p["glassType"]
        rep={"display":solid}
        if not purchased:rep["export"]=solid
        k=model.item(key,name,representations=rep,properties=props);display.append(k)
        if not purchased:export.append(k)
        parts.append({"partId":key,"kind":kind,"width":width,"height":height,"thickness":thickness,
                      "quantity":1,"purchased":purchased})
    def regions(box):
        l,r,b,t=box;w,h=r-l,t-b
        if layout in ("center_band","offset_band"):
            start=l+(w-w*band)*position
            return [(start,start+w*band,b,t)]
        if layout=="upper":return [(l,r,b+h*.5,t)]
        if layout=="lower":return [(l,r,b,b+h*.5)]
        if layout=="two_blocks":return [(l,r,b,b+h*.44),(l,r,b+h*.56,t)]
        return [box]
    def motifs(box):
        result=[]
        for region in regions(box):
            l,r,b,t=region;w,h=r-l,t-b
            if generator=="parallel_paths":
                phi=math.radians(num("lineAngle",-90,90));d=(math.cos(phi),math.sin(phi));n=(-d[1],d[0])
                values=[v[0]*n[0]+v[1]*n[1] for v in rect(region)]
                lo,hi=min(values)+groove/2,max(values)-groove/2
                if hi<lo:raise ValueError("装饰区域不足以容纳槽宽")
                if enum("arrayMode",("count","pitch"))=="count":
                    number=num("lineCount",1,100,True);pitch=(hi-lo)/(number+1)
                else:
                    pitch=num("linePitch",5,1000);number=int((hi-lo)/pitch)+1
                if number>100 or (number>1 and pitch<groove+2):raise ValueError("线条过密，槽之间没有足够保留材料")
                start=(lo+hi-(number-1)*pitch)/2
                span=2*math.hypot(W,H)
                for index in range(number):
                    c=start+index*pitch
                    # Get centreline endpoints by clipping a narrow convex strip.
                    points=[[d[0]*a+n[0]*q,d[1]*a+n[1]*q] for a,q in [(-span,c-.0001),(span,c-.0001),(span,c+.0001),(-span,c+.0001)]]
                    cut=clip_polygon(points,region)
                    if len(cut)<3:continue
                    stations=[v[0]*d[0]+v[1]*d[1] for v in cut]
                    a,z=min(stations),max(stations)
                    result.append({"kind":"line","a":[d[0]*a+n[0]*c,d[1]*a+n[1]*c],"b":[d[0]*z+n[0]*c,d[1]*z+n[1]*c],"region":region})
            elif generator=="cell_regions":
                cw,ch=w/ncols,h/nrows
                if min(cw,ch)-web<pattern["minimumFeature"]:raise ValueError("花格/池板单元太小，请减少数量或保留宽度")
                for row in range(nrows):
                    for col in range(ncols):
                        x,z=l+(col+.5)*cw,b+(row+.5)*ch
                        points=[[x+v[0]*(cw-web)/2,z+v[1]*(ch-web)/2] for v in pattern["polygon"]]
                result.append({"kind":"polygon","points":points,"region":region})
            elif generator=="round_scene":
                radius=min(w,h)/2-groove
                if radius<=2*groove:raise ValueError("圆景尺寸不足")
                cx,cz=(l+r)/2,(b+t)/2
                result.append({"kind":"ring","center":[cx,cz],"radius":radius,"region":region})
                for path in pattern["paths"]:
                    for a,z in zip(path,path[1:]):
                        result.append({"kind":"line","a":[cx+a[0]*radius,cz+a[1]*radius],
                                       "b":[cx+z[0]*radius,cz+z[1]*radius],"region":region})
            else:raise ValueError("未知图案生成机制")
        return result
    global_motifs=motifs((border,W-border,border,H-border)) if composition=="continuous" else None
    cursor=0
    for leaf,width in enumerate(widths):
        left,right=cursor,cursor+width;cursor=right+gap
        leafkey=f"leaf.{leaf+1}"
        box=(left+(border if leaf==0 or composition!="continuous" else meeting),
             right-(border if leaf==count-1 or composition!="continuous" else meeting),border,H-border)
        zones=[]
        if protect:
            hinge_left=(hinge_side=="left") != bool(leaf%2)
            lx=(right-lock_w,right) if hinge_left else (left,left+lock_w)
            zones.append((lx[0],lx[1],lock_z-lock_h/2,lock_z+lock_h/2))
            hx=(left,left+hinge_w) if hinge_left else (right-hinge_w,right)
            for z in (H*.12,H*.5,H*.88):zones.append((hx[0],hx[1],z-hinge_h/2,z+hinge_h/2))
        zones_record.append({"leafId":leafkey,"bounds":box,"hardware":zones})
        raw=plate.emit_rectangular_plate(model,leafkey,width=width,height=H,thickness=T,center=((left+right)/2,0,H/2))
        cutters=[]
        source=global_motifs if global_motifs is not None else motifs(box)
        source=deepcopy(source)
        if composition=="mirror" and leaf%2:
            for motif in source:
                if motif["kind"]=="polygon":motif["points"]=[[left+right-x,z] for x,z in motif["points"]][::-1]
                elif motif["kind"]=="line":
                    for k in ("a","b"):motif[k][0]=left+right-motif[k][0]
                else:motif["center"][0]=left+right-motif["center"][0]
                reg=motif["region"];motif["region"]=(left+right-reg[1],left+right-reg[0],reg[2],reg[3])
        if len(source)*len(sides)*count>800:raise ValueError("装饰特征超过上限，请减少阵列")
        for index,motif in enumerate(source):
            key=leafkey+f".decoration.{index+1}"
            region=motif["region"]
            keep_box=(max(box[0],region[0]),min(box[1],region[1]),max(box[2],region[2]),min(box[3],region[3]))
            if min(keep_box[1]-keep_box[0],keep_box[3]-keep_box[2])<=.01:continue
            if motif["kind"]=="polygon":
                points=clip_polygon(motif["points"],keep_box)
                if len(points)<3:continue
                mb=bounds(points)
                if min(mb[1]-mb[0],mb[3]-mb[2])<pattern["minimumFeature"]:
                    skipped+=1;continue
                expanded=(mb[0]-trimw,mb[1]+trimw,mb[2]-trimw,mb[3]+trimw)
                if trim and (expanded[0]<box[0] or expanded[1]>box[1] or expanded[2]<box[2] or expanded[3]>box[3]):
                    skipped+=1;continue
                if (through or trim) and any(overlap(expanded,z) for z in zones):
                    skipped+=1;continue
            elif motif["kind"]=="line":
                a,b=motif["a"],motif["b"]
                mb=(min(a[0],b[0])-groove/2,max(a[0],b[0])+groove/2,min(a[1],b[1])-groove/2,max(a[1],b[1])+groove/2)
                if not overlap(mb,keep_box):continue
            else:
                c=motif["center"];r=motif["radius"]+groove/2;mb=(c[0]-r,c[0]+r,c[1]-r,c[1]+r)
                if not overlap(mb,keep_box):continue
            clipped_bounds=(max(mb[0],keep_box[0]),min(mb[1],keep_box[1]),max(mb[2],keep_box[2]),min(mb[3],keep_box[3]))
            if any(z[0]<=clipped_bounds[0] and z[1]>=clipped_bounds[1] and z[2]<=clipped_bounds[2] and z[3]>=clipped_bounds[3] for z in zones):
                skipped+=1;continue
            for side in sides:
                sk=key+"."+side
                sign=1 if side=="front" else -1
                surface=-T/2 if side=="front" else T/2
                y=surface-sign*.02
                actualdepth=T+.04 if through else depth+.02
                if motif["kind"]=="polygon":
                    tool=polygon_solid(sk,points,y,sign*actualdepth)
                elif motif["kind"]=="ring":
                    r=motif["radius"]
                    tool=prism(sk,[circle_path(r+groove/2),circle_path(r-groove/2)],y,sign*actualdepth,motif["center"])
                else:
                    a,b=motif["a"],motif["b"];length=math.dist(a,b)
                    if length<.01:continue
                    d=((b[0]-a[0])/length,(b[1]-a[1])/length);n=(-d[1],d[0])
                    if vgroove:
                        half=(depth+.02)*math.tan(math.radians(angle/2))
                        path=model.geometry(sk+".section","profile2d",arguments={
                            "placement":{"origin":[a[0],surface,a[1]],"xAxis":[n[0],0,n[1]],"yAxis":[0,sign,0]},
                            "contours":[path([[-half,-.02],[half,-.02],[0,depth]])]})
                        tool=model.geometry(sk+".solid","extrude",inputs=[path],arguments={"vector":[b[0]-a[0],0,b[1]-a[1]]})
                    else:
                        points=[[q[0]+n[0]*offset,q[1]+n[1]*offset] for q,offset in [(a,-groove/2),(b,-groove/2),(b,groove/2),(a,groove/2)]]
                        tool=polygon_solid(sk,points,y,sign*actualdepth)
                keep=polygon_solid(sk+".boundary",rect(keep_box),-T/2-.1,T+.2)
                tool=boolean(sk+".clipped",tool,[keep],"intersect")
                if not through and zones:
                    reserved=[polygon_solid(sk+f".protect.{zi}",rect(z),-T/2-.1,T+.2) for zi,z in enumerate(zones) if overlap(mb,z)]
                    tool=boolean(sk+".safe",tool,reserved,"subtract")
                cutters.append(tool)
                features.append({"leafId":leafkey,"side":side,"process":process,"depth":depth,"geometryKey":tool,
                                 "primitive":motif,"actualBoundary":keep_box,"ncReady":False})
                if trim:
                    outer=rect((mb[0]-trimw,mb[1]+trimw,mb[2]-trimw,mb[3]+trimw))
                    # A separate raised ring touches the uncut face, never a fused decoration.
                    solid=prism(sk+".trim",[path(outer),path(rect(mb))],surface,-sign*trimh)
                    item(sk+".trim","池板扣线",solid,mb[1]-mb[0]+2*trimw,mb[3]-mb[2]+2*trimw,trimh,"trim")
        result=boolean(leafkey+".finished",raw,cutters,"subtract")
        item(leafkey,"装饰门扇",result,width,H,T,"leaf")
        if process=="glass":
            thick=num("glassThickness",2,20);space=num("glassGap",.5,20)
            gx,gz=(box[0]+box[1])/2,(box[2]+box[3])/2
            solid=plate.emit_rectangular_plate(model,leafkey+".glass",width=box[1]-box[0],height=box[3]-box[2],thickness=thick,
                                                center=(gx,T/2+space+thick/2,gz))
            item(leafkey+".glass","门花背衬玻璃",solid,box[1]-box[0],box[3]-box[2],thick,"glass",True)
    model.output("display.default","display",display)
    model.output("export.manufacturing","export",export)
    model.table("parts","门板与装饰件",columns=[
        {"key":"kind","displayName":"类别","valueType":"string"},
        {"key":"width","displayName":"宽","valueType":"number","unit":"mm"},
        {"key":"height","displayName":"高","valueType":"number","unit":"mm"},
        {"key":"thickness","displayName":"厚","valueType":"number","unit":"mm"},
        {"key":"quantity","displayName":"数量","valueType":"integer"}],
        rows=[{"key":row["partId"],"values":{k:row[k] for k in ("kind","width","height","thickness","quantity")}} for row in parts])
    if skipped:model.diagnostic("info","door.reserved-regions",f"已避让 {skipped} 个五金冲突或过小的镂空／扣线单元，避免孤立材料。")
    model.diagnostic("warning","door.geometry-only","当前输出为门面加工几何与板件清单，不含雕刻刀路、NC、艺术浮雕、门框及五金安装设计；V槽端部为裁切平面，需由雕刻CAM校核实际刀具扫掠。")
    document=finish(model,context)
    document.setdefault("extensions",{})["doorDecoration"]={
        "pattern":pattern,"composition":composition,"leafWidths":widths,"gap":gap,"parts":parts,
        "protectedRegions":zones_record,"features":features,"skippedFeatures":skipped,
        "minimumRemainingThickness":0 if through else T-depth*len(sides),
        "machiningGeometryOnly":True,"ncReady":False}
    return document
