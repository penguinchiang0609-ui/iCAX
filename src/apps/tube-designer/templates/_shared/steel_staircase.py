"""Steel stair assemblies: separate route, structure, treads and manufactured parts."""
from copy import deepcopy
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import sys
from icax_template_sdk import NeutralModel

def shared(name):
    path=Path(__file__).with_name(name)
    key="stair_v2_"+path.stem+hashlib.sha256(path.read_bytes()).hexdigest()[:16]
    if key not in sys.modules:
        spec=importlib.util.spec_from_file_location(key,path)
        obj=importlib.util.module_from_spec(spec);sys.modules[key]=obj;spec.loader.exec_module(obj)
    return sys.modules[key]

def clip(points,normal,offset):
    out=[]
    for a,b in zip(points,points[1:]+points[:1]):
        da=sum(x*y for x,y in zip(a,normal))-offset
        db=sum(x*y for x,y in zip(b,normal))-offset
        if da<=1e-8:out.append(a)
        if da*db< -1e-12:
            f=da/(da-db);out.append([a[i]+f*(b[i]-a[i]) for i in (0,1)])
    return out

def route(p):
    h=float(p["floorHeight"]);n=int(p["totalRiserCount"]);g=float(p["treadDepth"]);w=float(p["stairWidth"])
    layout=p["stairRoute"];r=h/n
    if layout=="straight":counts=[n]
    else:
        counts=[int(p["firstFlightRiserCount"]),n-int(p["firstFlightRiserCount"])]
        if min(counts)<2:raise ValueError("每跑至少需要两个级高")
    flights=[];landings=[];origin=[0.,0.,0.];direction=[1.,0.]
    for i,count in enumerate(counts):
        flights.append({"key":f"flight.{i+1}","origin":origin[:],"direction":direction[:],"risers":count})
        if i==len(counts)-1:break
        length=float(p["landingLength"])
        if length<w:raise ValueError("平台长度不能小于梯宽")
        end=[origin[0]+direction[0]*count*g,origin[1]+direction[1]*count*g,origin[2]+count*r]
        sign=1 if p["turnDirection"]=="left" else -1
        gap=float(p["wellGap"]) if layout=="u_turn" else 0
        landings.append({"key":f"landing.{i+1}","origin":end,"direction":direction[:],
                         "length":length,"width":2*w+gap if layout=="u_turn" else w})
        if layout=="straight_landing":origin=[end[0]+length,end[1],end[2]]
        elif layout=="l_turn":
            origin=[end[0]+length-w/2,end[1]+sign*w/2,end[2]];direction=[0.,float(sign)]
        elif layout=="u_turn":
            # Platform runs from the first lane's outside edge to the second.
            landings[-1]["centerOffset"]=sign*(w+gap)/2
            origin=[end[0],end[1]+sign*(w+gap),end[2]];direction=[-1.,0.]
        else:raise ValueError("不支持的楼梯路线")
    return flights,landings,r

def generate(parameters,context):
    p=parameters
    def num(key,lo=0,hi=100000):
        x=p[key]
        if type(x) not in (int,float) or not math.isfinite(x) or not lo<=x<=hi:
            raise ValueError(key+" 参数无效")
        return float(x)
    W=num("stairWidth",400,3000);H=num("floorHeight",300,10000);G=num("treadDepth",150,500)
    N=num("totalRiserCount",3,60)
    if int(N)!=N:raise ValueError("级数必须是整数")
    if H/N>250:raise ValueError("当前模型级高超过250mm，请增加级数")
    if p["stairRoute"] not in ("straight","straight_landing","l_turn","u_turn"):raise ValueError("楼梯路线无效")
    if p["stairRoute"]!="straight":
        if int(num("firstFlightRiserCount",2,58))!=p["firstFlightRiserCount"]:raise ValueError("首跑级数必须是整数")
        num("landingLength",W,10000)
        if p["stairRoute"]=="u_turn":num("wellGap",0,5000)
    catalog=shared("tube_profile_catalog.py");plate=shared("plate_geometry.py")
    def profile(prefix):
        values=p
        if prefix=='stringer' and p['stringerProfileType']=='channel':
            values={**p,'stringerSectionModel':p['stringerChannelModel']}
        obj=catalog.load_profile(values,prefix,depth_key="treadDepthProfile" if prefix=="tread" else prefix+"Depth")
        return obj
    frame=profile("tread")
    landing=profile("landing") if p["stairRoute"]!="straight" else frame
    structure=p["stringerSystem"]
    zigzag=p.get("stringerConstruction","profile")=="zigzag"
    zigzag_t=num("zigzagThickness",4,40) if zigzag else 0
    zigzag_depth=num("zigzagDepth",40,500) if zigzag else 0
    beam=profile("stringer") if not zigzag else None
    beam_width=zigzag_t if zigzag else beam.width
    beam_depth=zigzag_depth if zigzag else beam.depth
    if structure=="mono":offsets=[0.]
    elif structure=="twin":
        spacing=num("stringerSpacing",100,W-beam_width);offsets=[-spacing/2,spacing/2]
        if spacing<=beam_width:raise ValueError("双梁间距不足")
    elif structure=="side":offsets=[-(W-beam_width)/2,(W-beam_width)/2]
    else:raise ValueError("承重结构无效")
    if W<=beam_width or frame.width>=G:raise ValueError("梯梁／踏步支撑规格超出梯宽或踏步深度")
    if not zigzag and beam.kind=="rect" and frame.depth>beam.width-2*beam.radius:
        raise ValueError("踏步立支架宽度超出梯梁顶面的平直支承区")
    tread_kind=p["treadType"]
    if tread_kind not in ("steel","wood","glass","stone","none"):raise ValueError("踏板类型无效")
    deck=num("treadThickness",2,100) if tread_kind!="none" else 0.
    tread_gap=num("treadGap",0,30) if tread_kind!="none" else 0.
    if G-tread_gap<=2*frame.width:raise ValueError("踏板深度不足以容纳框架")
    support=p["treadSupport"]
    if support not in ("cross_tube","tube_frame"):raise ValueError("踏步支撑方式无效")
    plate_bracket=p.get("bracketType","tube")=="plate"
    bracket_t=num("bracketThickness",3,30) if plate_bracket and not zigzag else 0
    if plate_bracket and not zigzag and bracket_t>=beam.width:raise ValueError("支架板厚必须小于梯梁宽度")
    connection=p["connectionType"]
    if connection not in ("weld","end_plate"):raise ValueError("连接方式无效")
    plate_t=num("connectionPlateThickness",3,30) if connection=="end_plate" else 0.
    hole_d=num("boltHoleDiameter",3,40) if plate_t else 0.
    margin=num("boltEdgeDistance",10,100) if plate_t else 0.
    if plate_t and margin<=hole_d/2+3:raise ValueError("螺栓孔边距不足")
    if plate_t and p["stairRoute"]=="l_turn" and p["landingLength"]<W+2*margin:
        raise ValueError("L型平台长度需至少为梯宽加两倍连接孔边距，避免两跑端板在转角相撞")
    side=p["railingSide"]
    if side not in ("none","left","right","both"):raise ValueError("栏杆侧别无效")
    rails=side!="none"
    continuous=rails and p.get("handrailConnection","separate")=="continuous"
    network=shared("stair_nodes.py").RailNetwork()
    post_every=int(num("postEveryRisers",1,10)) if rails else 1
    post=profile("post") if rails else None
    hand=profile("handrail") if rails else None
    guard_height=num("railingHeight",300,2000) if rails else 0
    fill=p["railingInfill"] if rails else "none"
    if fill not in ("none","horizontal","vertical"):raise ValueError("栏杆填充无效")
    infill=profile("infill") if rails and fill!="none" else None
    horizontal_count=int(num("horizontalRailCount",1,8)) if fill=="horizontal" else 0
    max_gap=num("maximumInfillGap",20,300) if fill=="vertical" else 0
    sides=([1] if side=="left" else [-1] if side=="right" else [-1,1] if rails else [])
    extension=(post.depth+10) if rails else 0
    FW=W+2*extension
    if p['stairRoute']=='u_turn':
        plate_gap=max(0,beam_width+4*margin-W+2*max(offsets)) if plate_t else 0
        guard_gap=max(2*extension,post.depth+10+hand.width) if rails else 0
        required_gap=max(plate_gap,guard_gap)
        if p['wellGap']<required_gap-1e-7:
            raise ValueError(f'U型井道间距至少需要 {required_gap:g} mm，避免两跑端板、横撑和内侧栏杆相撞')
    flights,landings,R=route(p);slope=R/G;c=1/math.sqrt(1+slope*slope)
    if zigzag and zigzag_depth<=R+5:raise ValueError("锯齿梁带宽必须大于级高加5mm，保证齿根连通")
    # The complete tread-support frame stays above the sloping beam.
    beam_top_offset=-deck-frame.depth
    if not zigzag and R/2-deck-frame.depth-slope*frame.width/2<=plate_t+2:
        raise ValueError("首级支架无法落在梯梁上：请减小踏板/支撑高度或增加级高")
    model=NeutralModel(template_id="straight-steel-staircase",template_version="2.1.0",
        package_digest=str(context.get("template",{}).get("packageDigest","")),parameters=deepcopy(p))
    display=[];export=[];rows=[];placement={"origin":[0,0,0],"xAxis":[1,0,0],"yAxis":[0,1,0],"zAxis":[0,0,1]}
    signatures={};tube_centers={};pending_posts=[]
    def boolean(key,target,tools,operation,keep_point=None):
        if target in tube_centers:tube_centers[key]=tube_centers[target]
        return model.geometry(key,"boolean",inputs=[target,*tools],arguments={"operation":operation,"target":target,"tools":tools,
                              **({'keepConnectedTo':keep_point} if keep_point is not None else {})}) if tools else target
    def prism(key,points,origin,x,y,vector):
        outline=model.geometry(key+".outline","profile2d",arguments={"placement":{"origin":origin,"xAxis":x,"yAxis":y},
                         "contours":[{"kind":"polygon","points":points}]})
        return model.geometry(key+".solid","extrude",inputs=[outline],arguments={"vector":vector})
    def box(key,x0,x1,y0,y1,z0,z1):
        return prism(key,[[x0,y0],[x1,y0],[x1,y1],[x0,y1]],[0,0,z0],[1,0,0],[0,1,0],[0,0,z1-z0])
    def tube(key,start,end,pf,x,y,outer=False):
        tube_centers[key+'.solid']=[(start[i]+end[i])/2 for i in range(3)]
        path=model.geometry(key+".profile","profile2d",arguments={"placement":{"origin":list(start),"xAxis":list(x),"yAxis":list(y)},
                                 "contours":pf.contours()[:1] if outer else pf.contours()})
        return model.geometry(key+".solid","extrude",inputs=[path],arguments={"vector":[end[i]-start[i] for i in range(3)]})
    def item(key,name,solid,kind,dimensions,pf=None,operations=None,purchased=False):
        if continuous and name in ('栏杆立柱','平台栏杆立柱'):
            center=tube_centers[solid]
            witness=[center[0]+pf.width/2-pf.wall/2,center[1],center[2]]
            witness=[placement['origin'][i]+sum(witness[j]*placement[k][i] for j,k in enumerate(('xAxis','yAxis','zAxis'))) for i in range(3)]
            center=[placement['origin'][i]+sum(center[j]*placement[k][i] for j,k in enumerate(('xAxis','yAxis','zAxis'))) for i in range(3)]
            placed=model.geometry(key+'.pending','transform',inputs=[solid],arguments={'placement':deepcopy(placement)})
            pending_posts.append((key,name,placed,kind,dimensions,pf,operations,center,witness))
            return
        placed=model.geometry(key+".placed","transform",inputs=[solid],arguments={"placement":deepcopy(placement)})
        props={"partNumber":p["productCode"]+"-"+key,"quantity":1,"group":key.split(".")[0]+"."+key.split(".")[1],
               "manufacturing.partKind":kind,"manufacturing.sourcing":"purchased" if purchased else "made",
               "manufacturing.material":p["treadMaterial"] if purchased else p["materialGrade"],
               "manufacturing.categoryKey":"stair."+name,"manufacturing.categoryName":name,
               "length":max(dimensions),"manufacturing.operations":operations or [],
               "stair.assemblyTransform":deepcopy(placement)}
        if pf:
            props["tubeDesigner.profile"]=pf.properties()
            props["tubeDesigner.endProcess"]={"startCut":"geometry","endCut":"geometry","cutSource":"finished_geometry",
                   "lengthBasis":"blank_axial_extent","connection":"weld"}
        else:props["manufacturing.plate"]={"width":dimensions[0],"height":dimensions[1],"thickness":dimensions[2],"areaMm2":dimensions[0]*dimensions[1]}
        rep={"display":placed}
        if not purchased:rep["export"]=placed
        k=model.item(key,name,representations=rep,properties=props)
        display.append(k)
        if not purchased:export.append(k)
        signature=json.dumps([kind,props["manufacturing.material"],[round(v,6) for v in dimensions],pf.properties() if pf else None,operations or []],sort_keys=True)
        signatures.setdefault(signature,[]).append(k)
        rows.append({"key":key,"values":{"name":name,"kind":kind,"length":round(max(dimensions),3),"quantity":1,"sourcing":props["manufacturing.sourcing"]}})
    def metal_plate(key,name,width,height,thick,center,x=(1,0,0),y=(0,1,0),holes=()):
        solid=plate.emit_rectangular_plate(model,key,width=width,height=height,thickness=thick,center=center,x_axis=x,y_axis=y,holes=holes)
        item(key,name,solid,"plate",[width,height,thick],operations=list(holes))
        return solid
    def rect_tube(key,name,start,end,pf,x,y,cut=None,ops=None,receivers=()):
        solid=tube(key,start,end,pf,x,y)
        if cut:solid=boolean(key+".trimmed",solid,[cut],"intersect")
        solid=boolean(key+".coped",solid,list(receivers),"subtract")
        if receivers:ops=list(ops or [])+[{"kind":"outer_envelope_cope","receivers":list(receivers)}]
        item(key,name,solid,"tube",[math.dist(start,end)],pf,ops)
        return solid

    for f in flights:
        prefix=f["key"];run=f["risers"]*G;rise=f["risers"]*R
        dx,dy=f["direction"]
        placement={"origin":f["origin"],"xAxis":[dx,dy,0],"yAxis":[-dy,dx,0],"zAxis":[0,0,1]}
        normal=[-slope*c,0,c];axis=[c,0,slope*c]
        # Beam top in the flight elevation plane: z = slope*x + beam_top_offset.
        center_offset=beam_top_offset-beam_depth/(2*c)
        pad=beam_depth/c+100
        outer_poly=[[-pad,slope*(-pad)+beam_top_offset],[run+pad,slope*(run+pad)+beam_top_offset],
                    [run+pad,slope*(run+pad)+beam_top_offset-beam_depth/c],[-pad,slope*(-pad)+beam_top_offset-beam_depth/c]]
        outer_poly=clip(clip(outer_poly,[0,-1],-plate_t) if f==flights[0] else clip(outer_poly,[-1,0],-plate_t),[1,0],run-plate_t)
        if len(outer_poly)<3:raise ValueError("梯梁被楼面和端部裁切后为空")
        projections=[v[0]*c+v[1]*slope*c for v in outer_poly]
        blank_length=max(projections)-min(projections)
        beam_envelopes=[]
        for j,y in enumerate(offsets):
            key=prefix+f".beam.{j+1}"
            if not zigzag:
                raw=tube(key,[-pad,y,-slope*pad+center_offset],[run+pad,y,slope*(run+pad)+center_offset],beam,[0,1,0],normal)
                keep=prism(key+".keep",outer_poly,[0,y-beam_width,0],[1,0,0],[0,0,1],[0,2*beam_width,0])
                solid=boolean(key+".finished",raw,[keep],"intersect")
                envelope=tube(key+".envelope",[-pad,y,-slope*pad+center_offset],[run+pad,y,slope*(run+pad)+center_offset],beam,[0,1,0],normal,outer=True)
                beam_envelopes.append(envelope)
            start_cut={"kind":"floor_cut","z":plate_t} if f==flights[0] else {"kind":"vertical_start_cut","x":plate_t}
            if zigzag:
                teeth=[]
                for step in range(f['risers']):
                    z=(step+1)*R-deck-frame.depth
                    teeth.extend([[step*G,z],[(step+1)*G,z]])
                polygon=teeth+[[v[0],v[1]-zigzag_depth] for v in reversed(teeth)]
                polygon=clip(polygon,[0,-1],-plate_t) if f==flights[0] else clip(polygon,[-1,0],-plate_t)
                polygon=clip(polygon,[1,0],run-plate_t)
                solid=prism(key+'.zigzag',polygon,[0,y-zigzag_t/2,0],[1,0,0],[0,0,1],[0,zigzag_t,0])
                item(key,"锯齿板梁",solid,"plate",[run,rise+zigzag_depth,zigzag_t],operations=[{'kind':'plate_contour','points':polygon}])
            else:
                item(key,"梯梁",solid,"tube",[blank_length],beam,[start_cut,{"kind":"vertical_end_cut","x":run-plate_t}])
            if plate_t:
                floor_x0=(plate_t-beam_top_offset)/slope
                floor_x1=(plate_t-beam_top_offset+beam_depth/c)/slope
                if zigzag and f==flights[0]:
                    floor_points=[v[0] for v in polygon if abs(v[1]-plate_t)<1e-6]
                    floor_x0=min(floor_points);floor_x1=max(floor_points)
                pw=beam_width+4*margin;pl=floor_x1-floor_x0+4*margin
                holes=[{"x":a*(pl/2-margin),"y":b*(pw/2-margin),"diameter":hole_d} for a in (-1,1) for b in (-1,1)]
                if f==flights[0]:
                    metal_plate(key+".base","梯梁落地底板",pl,pw,plate_t,[(floor_x0+floor_x1)/2,y,plate_t/2],holes=holes)
                    model.relationship(key+'.base.joint','weld',[key,key+'.base'])
                ph=(beam_depth if zigzag else beam_depth/c)+frame.depth+4*margin
                ztop=rise-deck
                holes=[{"x":a*(pw/2-margin),"y":b*(ph/2-margin),"diameter":hole_d} for a in (-1,1) for b in (-1,1)]
                metal_plate(key+".end","梯梁端板",pw,ph,plate_t,[run-plate_t/2,y,ztop-ph/2],
                            x=(0,1,0),y=(0,0,1),holes=holes)
                model.relationship(key+'.end.joint','weld',[key,key+'.end'])
                if f!=flights[0]:
                    metal_plate(key+".start","梯梁起端连接板",pw,ph,plate_t,[plate_t/2,y,-deck-ph/2],
                                x=(0,1,0),y=(0,0,1),holes=holes)
                    model.relationship(key+'.start.joint','weld',[key,key+'.start'])
        # Treads have separate sloped-bottom upright brackets and horizontal supports.
        for i in range(f["risers"]):
            key=prefix+f".step.{i+1}";x=(i+.5)*G;top=(i+1)*R-deck
            frame_bottom=top-frame.depth
            rear_clearance=plate_t if i==f["risers"]-1 else 0
            cross_xs=[x] if support=="cross_tube" else [i*G+frame.width/2+tread_gap/2,(i+1)*G-frame.width/2-tread_gap/2-rear_clearance,x]
            for k,cx in enumerate(cross_xs):
                inset=frame.width if support=="tube_frame" and k==2 else 0
                rect_tube(key+f".cross.{k+1}","踏步横撑",[cx,-FW/2+inset,top-frame.depth/2],[cx,FW/2-inset,top-frame.depth/2],
                          frame,[1,0,0],[0,0,1])
                if zigzag:
                    for j in range(len(offsets)):
                        model.relationship(key+f'.cross.{k+1}.beam.{j}','weld',[key+f'.cross.{k+1}',prefix+f'.beam.{j+1}'])
            if support=="tube_frame":
                for k,cy in enumerate([-FW/2+frame.width/2,FW/2-frame.width/2]):
                    rect_tube(key+f".edge.{k+1}","踏步框边梁",
                        [cross_xs[0]+frame.width/2,cy,top-frame.depth/2],[cross_xs[1]-frame.width/2,cy,top-frame.depth/2],
                        frame,[0,1,0],[0,0,1])
            for j,y in enumerate(offsets if not zigzag else []):
                # Support the centre of each cross-member, including the two-rail frame variant.
                for k,cx in enumerate([x]):
                    bottom=slope*(cx-frame.width/2)+center_offset
                    if frame_bottom-bottom<=2:raise ValueError("踏步支架高度不足")
                    if plate_bracket:
                        raw=box(key+f".bracket.{j}.{k}",cx-frame.width/2,cx+frame.width/2,y-bracket_t/2,y+bracket_t/2,bottom,frame_bottom)
                    else:
                        raw=tube(key+f".bracket.{j}.{k}",[cx,y,bottom],[cx,y,frame_bottom],frame,[1,0,0],[0,1,0])
                    poly=[[cx-frame.width,frame_bottom],[cx+frame.width,frame_bottom],
                          [cx+frame.width,slope*(cx+frame.width)+center_offset],
                          [cx-frame.width,slope*(cx-frame.width)+center_offset]]
                    keep=prism(key+f".bracket.{j}.{k}.keep",poly,[0,y-frame.depth,0],[1,0,0],[0,0,1],[0,2*frame.depth,0])
                    solid=boolean(key+f".bracket.{j}.{k}.cut",raw,[keep],"intersect")
                    witness=[cx if plate_bracket else cx+frame.width/2-frame.wall/2,y,frame_bottom-1]
                    solid=boolean(key+f".bracket.{j}.{k}.cope",solid,[beam_envelopes[j]],"subtract",keep_point=witness)
                    item(key+f".support.{j}.{k}","钢板支架" if plate_bracket else "踏步立支架",solid,"plate" if plate_bracket else "tube",
                         [frame.width,frame_bottom-bottom,bracket_t] if plate_bracket else [frame_bottom-bottom],None if plate_bracket else frame,
                         [{"kind":"outer_envelope_cope","receiver":prefix+f'.beam.{j+1}',"slope":slope}])
                    model.relationship(key+f'.support.{j}.{k}.beam','weld',[key+f'.support.{j}.{k}',prefix+f'.beam.{j+1}'])
                    model.relationship(key+f'.support.{j}.{k}.cross','weld',[key+f'.support.{j}.{k}',key+('.cross.1' if support=='cross_tube' else '.cross.3')])
            if tread_kind!="none":
                solid=plate.emit_rectangular_plate(model,key+".deck",width=G-tread_gap,height=W,thickness=deck,
                          center=[x,0,(i+1)*R-deck/2],x_axis=(1,0,0),y_axis=(0,1,0))
                item(key+".deck","踏板",solid,"glass" if tread_kind=="glass" else "plate",[G-tread_gap,W,deck],purchased=tread_kind!="steel")
        if rails:
            post_indices=list(range(0,f["risers"],post_every))
            if post_indices[-1]!=f["risers"]-1:post_indices.append(f["risers"]-1)
            for sign in sides:
                y=sign*(W/2+post.depth/2+5);a=G/2;b=run-G/2
                rail_z=lambda x:slope*x+R/2+guard_height
                rail_start=[a-post.width,y,rail_z(a-post.width)];rail_end=[b+post.width,y,rail_z(b+post.width)]
                if continuous and f!=flights[0]:rail_start=[a,y,rail_z(a)]
                if continuous and f!=flights[-1]:rail_end=[b,y,rail_z(b)]
                hs=tube(prefix+f".guard.{sign}.rail",rail_start,rail_end,hand,[0,1,0],normal)
                if continuous:
                    network.add(prefix+f".guard.{sign}.top",rail_start,rail_end,hand,placement,f!=flights[0],f!=flights[-1])
                else:item(prefix+f".guard.{sign}.top","扶手",hs,"tube",[math.dist(rail_start,rail_end)],hand)
                hand_envelope=tube(prefix+f".guard.{sign}.envelope",[rail_start[0]-200,y,rail_z(rail_start[0]-200)],
                                   [rail_end[0]+200,y,rail_z(rail_end[0]+200)],hand,[0,1,0],normal,outer=True)
                post_envelopes={}
                for i in post_indices:
                    x=(i+.5)*G;bottom=(i+1)*R-deck
                    top=rail_z(x)
                    raw=tube(prefix+f".guard.{sign}.post.{i}",[x,y,bottom],[x,y,top+slope*post.width],post,[1,0,0],[0,1,0])
                    post_envelopes[i]=tube(prefix+f".guard.{sign}.post.{i}.envelope",[x,y,bottom],[x,y,top+slope*post.width],post,[1,0,0],[0,1,0],outer=True)
                    poly=[[x-post.width,bottom],[x+post.width,bottom],
                          [x+post.width,rail_z(x+post.width)],[x-post.width,rail_z(x-post.width)]]
                    keep=prism(prefix+f".guard.{sign}.post.{i}.keep",poly,[0,y-post.depth,0],[1,0,0],[0,0,1],[0,2*post.depth,0])
                    solid=boolean(prefix+f".guard.{sign}.post.{i}.cut",raw,[keep],"intersect")
                    if not continuous:solid=boolean(prefix+f".guard.{sign}.post.{i}.cope",solid,[hand_envelope],"subtract")
                    item(prefix+f".guard.{sign}.post.{i}","栏杆立柱",solid,"tube",[top+slope*post.width-bottom],post,[{"kind":"slope_top","slope":slope}])
                if fill!="none":
                    for i,last in zip(post_indices,post_indices[1:]):
                        left=(i+.5)*G;right=(last+.5)*G
                        if fill=="horizontal":
                            for k in range(horizontal_count):
                                h=guard_height*(k+1)/(horizontal_count+1)
                                cut=box(prefix+f".guard.{sign}.infill.{i}.{k}.keep",left,right,y-100,y+100,0,rise+guard_height+100)
                                rect_tube(prefix+f".guard.{sign}.infill.{i}.{k}","横向填充",
                                          [left-infill.depth,y,slope*(left-infill.depth)+R/2+h],[right+infill.depth,y,slope*(right+infill.depth)+R/2+h],infill,[0,1,0],normal,cut=cut,receivers=[post_envelopes[i],post_envelopes[last]])
                        else:
                            lower=lambda x:slope*x+R/2+100
                            lowcut=box(prefix+f".guard.{sign}.lower.{i}.keep",left,right,y-100,y+100,0,rise+guard_height+100)
                            rect_tube(prefix+f".guard.{sign}.lower.{i}","栏杆下横杆",
                                      [left-infill.depth,y,lower(left-infill.depth)],[right+infill.depth,y,lower(right+infill.depth)],
                                      infill,[0,1,0],normal,cut=lowcut,receivers=[post_envelopes[i],post_envelopes[last]])
                            lower_envelope=tube(prefix+f".guard.{sign}.lower.{i}.envelope",[left-infill.depth,y,lower(left-infill.depth)],
                                                [right+infill.depth,y,lower(right+infill.depth)],infill,[0,1,0],normal,outer=True)
                            count=max(1,math.ceil((right-left-post.width-max_gap)/(infill.width+max_gap)))
                            for k in range(count):
                                x=left+(right-left)*(k+1)/(count+1)
                                bottom=lower(x);top=rail_z(x)
                                poly=[[x-infill.width,lower(x-infill.width)],
                                      [x+infill.width,lower(x+infill.width)],
                                      [x+infill.width,rail_z(x+infill.width)],
                                      [x-infill.width,rail_z(x-infill.width)]]
                                keep=prism(prefix+f".guard.{sign}.infill.{i}.{k}.keep",poly,[0,y-infill.depth,0],[1,0,0],[0,0,1],[0,2*infill.depth,0])
                                rect_tube(prefix+f".guard.{sign}.infill.{i}.{k}","竖向填充",
                                          [x,y,bottom-slope*infill.width],[x,y,top+slope*infill.width],infill,[1,0,0],[0,1,0],cut=keep,receivers=[lower_envelope,hand_envelope])
    for l in landings:
        dx,dy=l["direction"];placement={"origin":l["origin"],"xAxis":[dx,dy,0],"yAxis":[-dy,dx,0],"zAxis":[0,0,1]}
        key=l["key"];length=l["length"];width=l["width"];offset=l.get("centerOffset",0)
        y0=offset-width/2;y1=offset+width/2;z=-deck-landing.depth/2
        for suffix,x in [("front",landing.width/2),("back",length-landing.width/2)]:
            rect_tube(key+"."+suffix,"平台横边梁",[x,y0,z],[x,y1,z],landing,[1,0,0],[0,0,1])
        for suffix,y in [("left",y0+landing.width/2),("right",y1-landing.width/2)]:
            rect_tube(key+"."+suffix,"平台纵边梁",[landing.width,y,z],[length-landing.width,y,z],landing,[0,1,0],[0,0,1])
        spacing=num("landingSupportSpacing",100,2000)
        count=max(0,math.ceil((length-2*landing.width)/spacing)-1)
        for i in range(count):
            x=landing.width+(length-2*landing.width)*(i+1)/(count+1)
            rect_tube(key+f".cross.{i}","平台横撑",[x,y0+landing.width,z],[x,y1-landing.width,z],landing,[1,0,0],[0,0,1])
        deck_tools=[]
        if rails:
            # Keep the entire post footprint inside the landing, including
            # wide oval sections, not just its centre on the frame centreline.
            inset_x=max(landing.width/2,post.width/2+5)
            inset_y=max(landing.width/2,post.depth/2+5)
            xlo,xhi=inset_x,length-inset_x
            ylo,yhi=y0+inset_y,y1-inset_y
            if xhi<=xlo or yhi<=ylo:raise ValueError('平台尺寸不足以容纳当前栏杆立柱截面')
            segments=[]
            if p["stairRoute"]!="straight_landing":segments.append(((xhi,ylo),(xhi,yhi)))
            for sign,cy in [(-1,ylo),(1,yhi)]:
                end=xhi
                if p["stairRoute"]=="l_turn" and sign==(1 if p["turnDirection"]=="left" else -1):
                    end=length-W-inset_x
                if end-xlo>post.width:segments.append(((xlo,cy),(end,cy)))
            # A separate well-side assembly reserves a rotation-independent
            # post/joint installation envelope. Tighter valid wells use the
            # continuous return instead of squeezing a separate corner post in.
            well_post_envelope=max(post.width,post.depth)
            if p["stairRoute"]=="u_turn" and p["wellGap"]>2*well_post_envelope:
                low=min(W/2,offset*2+W/2)+well_post_envelope/2
                high=max(-W/2,offset*2-W/2)-well_post_envelope/2
                if high>low:segments.append(((xlo,low),(xlo,high)))
            seen=set();previous_corners=set();corner_infill_envelopes={}
            for si,(a,b) in enumerate(segments):
                length_s=math.dist(a,b);d=((b[0]-a[0])/length_s,(b[1]-a[1])/length_s,0)
                lateral=(-d[1],d[0],0)
                count=max(1,math.ceil(length_s/num("maximumPostSpacing",200,2000)))
                start_extension=-hand.width/2 if a in previous_corners else hand.width/2
                end_extension=-hand.width/2 if b in previous_corners else hand.width/2
                start=[a[0]-d[0]*start_extension,a[1]-d[1]*start_extension,guard_height]
                end=[b[0]+d[0]*end_extension,b[1]+d[1]*end_extension,guard_height]
                if continuous:start=[*a,guard_height];end=[*b,guard_height]
                raw=tube(key+f".guard.{si}.rail",start,end,hand,lateral,[0,0,1])
                if continuous:network.add(key+f".guard.{si}.rail",start,end,hand,placement,platform=True)
                else:item(key+f".guard.{si}.rail","平台扶手",raw,"tube",[math.dist(start,end)],hand)
                hand_envelope=tube(key+f".guard.{si}.rail.envelope",[start[i]-d[i]*200 for i in range(3)],
                                   [end[i]+d[i]*200 for i in range(3)],hand,lateral,[0,0,1],outer=True)
                previous_corners.update((a,b))
                post_envelopes=[]
                for pi in range(count+1):
                    px=a[0]+d[0]*length_s*pi/count;py=a[1]+d[1]*length_s*pi/count
                    post_envelopes.append(tube(key+f".guard.{si}.post.{pi}.envelope",[px,py,-deck],[px,py,guard_height],post,[1,0,0],[0,1,0],outer=True))
                    ident=(round(px,6),round(py,6))
                    if ident in seen:continue
                    seen.add(ident)
                    rect_tube(key+f".guard.{si}.post.{pi}","平台栏杆立柱",[px,py,-deck],[px,py,guard_height],
                              post,[1,0,0],[0,1,0],receivers=[] if continuous else [hand_envelope])
                    if deck:
                        deck_tools.append(box(key+f".guard.{si}.seat.{pi}",px-post.width/2-1,px+post.width/2+1,
                                              py-post.depth/2-1,py+post.depth/2+1,-deck-.1,.1))
                for pi in range(count):
                    v0=length_s*pi/count
                    v1=length_s*(pi+1)/count
                    if v1<=v0:continue
                    positions=([guard_height*(i+1)/(horizontal_count+1) for i in range(horizontal_count)] if fill=="horizontal"
                               else [100] if fill=="vertical" else [])
                    for ni,zfill in enumerate(positions):
                        fill_key=key+f".guard.{si}.fill.{pi}.{ni}"
                        start=[a[0]+d[0]*v0,a[1]+d[1]*v0,zfill]
                        end=[a[0]+d[0]*v1,a[1]+d[1]*v1,zfill]
                        ends=[tuple(round(value,6) for value in point) for point in (start,end)]
                        receivers=[post_envelopes[pi],post_envelopes[pi+1]]
                        # Narrow/oval posts do not necessarily contain the whole
                        # intersection of adjacent rails. Cope the later member
                        # to the earlier member as well as to the shared post.
                        for point in ends:receivers.extend(corner_infill_envelopes.get(point,[]))
                        rect_tube(fill_key,"平台横杆",start,end,infill,lateral,[0,0,1],receivers=receivers)
                        envelope=tube(fill_key+'.envelope',start,end,infill,lateral,[0,0,1],outer=True)
                        for point in ends:corner_infill_envelopes.setdefault(point,[]).append(envelope)
                    if fill=="vertical":
                        amount=max(1,math.ceil((v1-v0-max(post.width,post.depth)-max_gap)/(infill.width+max_gap)))
                        lower_envelope=tube(key+f".guard.{si}.lower.{pi}.envelope",[a[0]+d[0]*v0,a[1]+d[1]*v0,100],
                                            [a[0]+d[0]*v1,a[1]+d[1]*v1,100],infill,lateral,[0,0,1],outer=True)
                        for ni in range(amount):
                            v=v0+(v1-v0)*(ni+1)/(amount+1);px=a[0]+d[0]*v;py=a[1]+d[1]*v
                            rect_tube(key+f".guard.{si}.picket.{pi}.{ni}","平台竖杆",[px,py,100],
                                      [px,py,guard_height],infill,d,lateral,receivers=[lower_envelope,hand_envelope])
        if tread_kind!="none":
            solid=plate.emit_rectangular_plate(model,key+".deck",width=length,height=width,thickness=deck,
                  center=[length/2,offset,-deck/2],x_axis=(1,0,0),y_axis=(0,1,0))
            solid=boolean(key+".deck.clearance",solid,deck_tools,"subtract")
            item(key+".deck","平台铺板",solid,"glass" if tread_kind=="glass" else "plate",[length,width,deck],purchased=tread_kind!="steel")
        if p["landingColumns"]:
            intervals=[(landing.width,length-landing.width)]
            if p["stairRoute"]=="l_turn":
                # Columns remain under the side beams, away from the outgoing
                # stringers and the full end-plate envelopes.
                sign=1 if p["turnDirection"]=="left" else -1
                exclusion=beam_width/2+2*margin+landing.width/2+10
                for lateral in offsets:
                    centre=length-W/2-sign*lateral
                    updated=[]
                    for lo,hi in intervals:
                        if lo<centre-exclusion:updated.append((lo,min(hi,centre-exclusion)))
                        if hi>centre+exclusion:updated.append((max(lo,centre+exclusion),hi))
                    intervals=[v for v in updated if v[1]>v[0]+1]
            if not intervals or intervals[-1][1]-intervals[0][0]<landing.width+4*margin:
                raise ValueError("平台没有足够空间布置避开梯梁连接的支撑柱，请增大平台")
            xfirst,xlast=intervals[0][0],intervals[-1][1]
            for i,(cx,cy) in enumerate([(xfirst,y0+landing.width/2),(xfirst,y1-landing.width/2),
                                       (xlast,y0+landing.width/2),(xlast,y1-landing.width/2)]):
                bottom=-l["origin"][2]+plate_t;top=-deck-landing.depth
                rect_tube(key+f".column.{i}","平台支撑柱",[cx,cy,bottom],[cx,cy,top],landing,[1,0,0],[0,1,0])
                if plate_t:
                    pw=landing.width+4*margin;ph=landing.depth+4*margin
                    holes=[{"x":a*(pw/2-margin),"y":b*(ph/2-margin),"diameter":hole_d} for a in (-1,1) for b in (-1,1)]
                    metal_plate(key+f".column.{i}.base","平台柱底板",pw,ph,plate_t,
                                [cx,cy,-l["origin"][2]+plate_t/2],holes=holes)
    if continuous:
        placement={"origin":[0,0,0],"xAxis":[1,0,0],"yAxis":[0,1,0],"zAxis":[0,0,1]}
        rail_envelopes=network.finish(tube,prism,boolean,item,model.relationship)
        continuous=False
        for key,name,solid,kind,dimensions,pf,ops,center,witness in pending_posts:
            tools=[]
            for envelope,a,b,receiver in rail_envelopes:
                dx,dy=b[0]-a[0],b[1]-a[1];length2=dx*dx+dy*dy
                t=max(0,min(1,((center[0]-a[0])*dx+(center[1]-a[1])*dy)/length2)) if length2 else 0
                if math.hypot(center[0]-a[0]-t*dx,center[1]-a[1]-t*dy)<max(pf.width,pf.depth)+max(receiver.width,receiver.depth):tools.append(envelope)
            # Mitered receivers share end faces. Unite their material envelope
            # before coping the post: repeated cuts at the same miter edge can
            # leave coincident sliver faces on curved post sections.
            envelope=boolean(key+'.joint_envelope',tools[0],tools[1:],'union') if tools else None
            solid=boolean(key+'.transition_cope',solid,[envelope] if envelope else [],'subtract',keep_point=witness)
            item(key,name,solid,kind,dimensions,pf,list(ops or [])+[{'kind':'outer_envelope_cope','receivers':tools}])
    model.output("display.default","display",display);model.output("export.manufacturing","export",export)
    model.table("parts","钢楼梯零件",columns=[{"key":key,"displayName":label,"valueType":typ} for key,label,typ in
         [("name","零件","string"),("kind","类别","string"),("length","毛坯尺寸","number"),("quantity","数量","integer"),("sourcing","供应","string")]],rows=rows)
    model.diagnostic("warning","stair.engineering","模型提供制造几何，不代表结构验算；焊缝、锚栓、建筑连接和外购踏板承载须由工程确定。")
    result=shared("shared_tube_geometry.py").finish_geometry_request(model,context)
    result.setdefault("extensions",{})["steelStaircase"]={"flights":flights,"landings":landings,"riserHeight":R,
       "stringerOffsets":offsets,"partGroups":[{"members":ids,"quantity":len(ids)} for ids in signatures.values()]}
    return result
