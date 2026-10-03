"""Window structure to actual local process inputs; no machining recipes."""
from copy import deepcopy
import hashlib
import importlib.util
import math
from pathlib import Path
import sys
from types import SimpleNamespace


def _load(name):
    path=Path(__file__).with_name(name+'.py')
    key='icax_window_machining_'+name+'_'+hashlib.sha256(path.read_bytes()).hexdigest()[:16]
    if key not in sys.modules:
        spec=importlib.util.spec_from_file_location(key,path)
        module=importlib.util.module_from_spec(spec)
        sys.modules[key]=module
        spec.loader.exec_module(module)
    return sys.modules[key]


def dot(a,b):
    return sum(x*y for x,y in zip(a,b))


def sub(a,b):
    return [x-y for x,y in zip(a,b)]


def _reflect_contour(contour):
    """Change the exact section coordinate frame; never rebuild a profile."""
    result=deepcopy(contour)
    for key in ('center',):
        if key in result:
            result[key]=[result[key][0],-result[key][1]]
    if 'points' in result:
        result['points']=[[p[0],-p[1]] for p in result['points']]
    if result.get('kind')=='ellipse':
        result['rotation']=-result.get('rotation',0.)
    for segment in result.get('segments',[]):
        for key in ('start','middle','end','center'):
            if key in segment:
                segment[key]=[segment[key][0],-segment[key][1]]
        for key in ('controlPoints','poles'):
            if key in segment:
                segment[key]=[[p[0],-p[1]] for p in segment[key]]
        if segment.get('kind')=='ellipseArc':
            segment['rotation']=-segment.get('rotation',0.)
            segment['startAngle']=-segment['startAngle']
            segment['endAngle']=-segment['endAngle']
    return result


def actual_part(model,part,profile_arguments):
    f=_load('assembly_tube_machining')
    arguments=profile_arguments(part)
    placement=arguments['placement']
    x=f.unit(sub(part.end,part.start))
    y=f.unit(placement['xAxis'])
    z=f.unit(f.cross(x,y))
    contours=deepcopy(arguments['contours'])
    if dot(z,placement['yAxis'])<0:
        contours=[_reflect_contour(c) for c in contours]
    origin=placement.get('origin',part.start)
    matrix=[value for i in range(3) for value in (x[i],y[i],z[i],origin[i])]+[0.,0.,0.,1.]
    profile=part.profile.properties()
    profile['contours']=contours
    # Dimensions are metadata in actual stock YZ, not the catalogue's XY names.
    geometry=_load('profile_recognition_geometry')
    bounds=geometry.bounds(geometry.contour(contours[0],1.e-3))
    profile.update(width=bounds[2]-bounds[0],depth=bounds[3]-bounds[1])
    result={'id':part.key,'label':part.name or part.key,'parameters':{},
            'length':math.dist(part.start,part.end),'matrix':matrix,
            'section':{'profile':profile,'wallThickness':part.profile.wall}}
    if model is not None:
        # An observer can use another local section basis than its allocated
        # folded stock. Its section resource identity must describe that basis
        # while the participant id continues to identify the physical member.
        result['profileRef']={'scope':'template','id':getattr(part,'section_profile_id',part.key),
                              'templateId':model._document['template']['id']}
    return result


def input_of(parts,geometry=None):
    return {'schema':'icax.assembly-process-input','schemaVersion':1,
            'parts':deepcopy(parts),'geometry':deepcopy(geometry or {})}


def _stock_record(model,stock):
    records=getattr(model,'_assembly_process_records',{})
    record=records.setdefault(stock['id'],{})
    if 'tubeDesigner.assemblyProcessSource' not in record:
        raw={k:deepcopy(v) for k,v in stock.items() if k in ('id','label','profileRef','parameters','length','matrix')}
        record.update({'tubeDesigner.assemblyProcessStockId':stock['id'],
            'tubeDesigner.assemblyProcessStockLength':stock['length'],
            'tubeDesigner.assemblyProcessStockProfile':deepcopy(stock['section']['profile']),
            'tubeDesigner.assemblyProcessSecondaryCutters':[],
            'tubeDesigner.assemblyProcessSource':{'schema':'icax.assembly-process-source','schemaVersion':1,
                                                'stocks':[raw],'instances':[]}})
    model._assembly_process_records=records
    return record


def schedule(model,function_id,stock,observers,geometry,parameters,instance_id):
    process_input=input_of({'stock':stock,**observers},geometry)
    result=_load('assembly_geometry_process_runtime').evaluate(function_id,process_input,parameters)
    if not result['applicable']:
        raise ValueError(stock['label']+'：'+result['reason'])
    record=_stock_record(model,stock)
    source=record['tubeDesigner.assemblyProcessSource']
    instance={'instanceId':instance_id,'templateId':function_id,'processInput':process_input,
              'parameters':deepcopy(parameters),'targets':{'stock':stock['id']}}
    old=next((i for i in source['instances'] if i['instanceId']==instance_id),None)
    if old is not None and old!=instance:
        raise ValueError('同一加工实例不能修改已安排的输入')
    if old is None:
        source['instances'].append(instance)
    return result


def execute(model,raw,result,instance_id,stock_id):
    return _load('assembly_geometry_process_runtime').apply(model,raw,result,instance_id,stock_id)


def aperture(model,raw,stock,branch,instance_id,clearance,clip_interval=None,keep_planes=None):
    geometry={}
    if clip_interval is not None:
        geometry['clipInterval']=list(clip_interval)
    if keep_planes:
        geometry['keepPlanes']=deepcopy(keep_planes)
    result=schedule(model,'tube-profile-aperture',stock,{'branch':branch},geometry,
                    {'clearance':clearance,'checkFit':True},instance_id)
    return execute(model,raw,result,instance_id,stock['id'])


def process_straight(model,raw,part,inserted_parts,clearance,profile_arguments,miter_ends=None):
    stock=actual_part(model,part,profile_arguments)
    planes=[]
    current=raw
    for end,station,adjacent in miter_ends or []:
        instance_id=part.key+'.end.'+end
        result=schedule(model,'tube-end-joint',stock,{},
            {'mode':'miter','end':end,'station':station,'mateDirection':list(adjacent)}, {},instance_id)
        planes.extend({'origin':check['origin'],'inwardNormal':check['inwardNormal']}
                      for check in result['checks'] if check['kind']=='end-plane')
        current=execute(model,current,result,instance_id,stock['id'])
    for index,inserted in enumerate(inserted_parts,start=1):
        branch=actual_part(model,inserted,profile_arguments)
        current=aperture(model,current,stock,branch,part.key+f'.aperture.{index:04d}',clearance,keep_planes=planes)
    return current


def check_profiles(receiver,inserted,depth,clearance,label,receiver_span=None,through=False):
    """Check current product insertion through the shared local process."""
    def actual(profile):
        return {'section':{'contours':profile.contours(),'wallThickness':profile.wall}}
    stock,branch=actual(receiver),actual(inserted)
    geometry={'checkThrough':through}
    geometry['receiverSpan']=receiver.width if receiver_span is None else receiver_span
    result=_load('assembly_geometry_process_runtime').evaluate('tube-insertion-check',
        input_of({'stock':stock,'branch':branch},geometry),{'depth':depth,'clearance':clearance})
    if not result['applicable']:
        raise ValueError(label+result['reason'])


def planar_miter_ends(part,parts):
    """Find the real adjacent members at this rectangle's structural nodes."""
    f=_load('assembly_tube_machining')
    direction=f.unit(sub(part.end,part.start))
    result=[]
    for end,cut in (('start',part.start_cut),('end',part.end_cut)):
        if cut!='miter-45':
            continue
        station=part.profile.width/2 if end=='start' else part.length-part.profile.width/2
        node=[part.start[i]+station*direction[i] for i in range(3)]
        candidates=[]
        for mate in parts:
            if mate.key==part.key or mate.group!=part.group:
                continue
            axis=f.unit(sub(mate.end,mate.start))
            if abs(dot(direction,axis))>1.e-7:
                continue
            for endpoint,sign in ((mate.start,1.),(mate.end,-1.)):
                center=[endpoint[i]+sign*mate.profile.width/2*axis[i] for i in range(3)]
                if math.dist(center,node)<1.e-6:
                    candidates.append([sign*a for a in axis])
        if len(candidates)!=1:
            raise ValueError('斜拼接点缺少唯一实际相邻框件：'+part.key+'.'+end)
        result.append((end,station,candidates[0]))
    return result


def planar_part(model,raw,part,inserted,clearance,parts,profile_arguments):
    current=process_straight(model,raw,part,inserted,clearance,profile_arguments,
                             planar_miter_ends(part,[p for p in parts if hasattr(p,'start')]))
    by_key={p.key:p for p in parts}
    for end,key in (('start',getattr(part,'start_joint','')),('end',getattr(part,'end_joint',''))):
        if key:
            mate=observed_mate(by_key[key],part,end)
            current=connect_end(model,current,part,mate,end,getattr(part,end+'_joint_mode','butt'),
                getattr(part,end+'_insertion',0.),clearance,profile_arguments)
    return current


def joint_input(stock_part,mate_part,end,mode,depth,clearance,profile_arguments,model=None):
    stock=actual_part(model,stock_part,profile_arguments)
    mate=actual_part(model,mate_part,profile_arguments)
    matrix=stock['matrix']
    axis=[matrix[4*i] for i in range(3)]
    station=dot(sub([mate['matrix'][i] for i in (3,7,11)],
                    [matrix[i] for i in (3,7,11)]),axis)
    geometry={'mode':mode,'end':end,'station':station}
    parameters={'insertionDepth':depth,'clearance':clearance}
    return stock,mate,geometry,parameters


def allocate_joint(stock_part,mate_part,end,mode,depth,clearance,profile_arguments):
    """Use a generic joint's material datum to allocate the actual stock end."""
    stock,mate,geometry,parameters=joint_input(stock_part,mate_part,end,mode,depth,clearance,profile_arguments)
    result=_load('assembly_geometry_process_runtime').evaluate('tube-end-joint',
        input_of({'stock':stock,'mate':mate},geometry),parameters)
    if not result['applicable']:
        raise ValueError(stock_part.name+'：'+result['reason'])
    point=result['checks'][0]['origin']
    m=stock['matrix']
    return tuple(sum(m[4*i+j]*point[j] for j in range(3))+m[4*i+3] for i in range(3))


def allocate_miter(part,end,adjacent,profile_arguments):
    stock=actual_part(None,part,profile_arguments)
    result=_load('assembly_geometry_process_runtime').evaluate('tube-end-joint',input_of({'stock':stock},
        {'mode':'miter','end':end,'station':0. if end=='start' else stock['length'],
         'mateDirection':list(adjacent)}),{})
    if not result['applicable']:
        raise ValueError(part.name+'：'+result['reason'])
    station=result['checks'][0]['allocation']['rawStockStation']
    m=stock['matrix']
    return tuple(m[4*i]*station+m[4*i+3] for i in range(3))


def connect_end(model,raw,part,mate_part,end,mode,depth,clearance,profile_arguments):
    stock,mate,geometry,parameters=joint_input(part,mate_part,end,mode,depth,clearance,profile_arguments,model)
    instance_id=part.key+'.joint.'+end
    result=schedule(model,'tube-end-joint',stock,{'mate':mate},geometry,parameters,instance_id)
    return execute(model,raw,result,instance_id,stock['id'])


def unfolded_stock(model,frame):
    part=SimpleNamespace(key=frame.key,name=frame.name,profile=frame.profile,
                         start=(0.,0.,0.),end=(frame.length,0.,0.))
    return actual_part(model,part,lambda p:{'placement':{'origin':[0.,0.,0.],
        'xAxis':[0.,1.,0.],'yAxis':[0.,0.,1.]},'contours':p.profile.contours(swap_axes=True)})


def mapped_branch(model,part,profile_arguments,map_point,key=None):
    arguments=deepcopy(profile_arguments(part))
    placement=arguments['placement']
    origin=map_point(placement['origin'])
    for name in ('xAxis','yAxis'):
        endpoint=map_point([placement['origin'][i]+placement[name][i] for i in range(3)])
        placement[name]=sub(endpoint,origin)
    placement['origin']=origin
    transformed=SimpleNamespace(key=key or part.key,name=part.name,profile=part.profile,
        start=map_point(part.start),end=map_point(part.end))
    return actual_part(model,transformed,lambda p:arguments)


def observed_sides(item):
    if hasattr(item,'start'):
        return [item]
    half=item.profile.width/2
    specs=[('left',(item.left+half,0,item.bottom),(item.left+half,0,item.top)),
           ('right',(item.right-half,0,item.bottom),(item.right-half,0,item.top)),
           ('bottom',(item.left,0,item.bottom+half),(item.right,0,item.bottom+half)),
           ('top',(item.left,0,item.top-half),(item.right,0,item.top-half))]
    return [SimpleNamespace(key=item.key,section_profile_id=item.key+'.observed.'+side,
                            name=item.name+' '+side,profile=item.profile,start=start,end=end,
                            group=item.group) for side,start,end in specs]


def observed_mate(item,part,end):
    f=_load('assembly_tube_machining')
    axis=f.unit(sub(part.end,part.start));endpoint=part.start if end=='start' else part.end
    candidates=[]
    for candidate in observed_sides(item):
        direction=f.unit(sub(candidate.end,candidate.start))
        if abs(dot(axis,direction))>1.e-7:
            continue
        station=dot(sub(endpoint,candidate.start),direction)
        if not -1.e-7<=station<=math.dist(candidate.start,candidate.end)+1.e-7:
            continue
        node=[candidate.start[i]+direction[i]*station for i in range(3)]
        candidates.append((math.dist(node,endpoint),candidate))
    if not candidates:
        raise ValueError('结构接点没有实际接收管件：'+part.key+'.'+end)
    return min(candidates,key=lambda value:value[0])[1]


def select_end_mate(items,part,end,prefixes):
    endpoint=part.start if end=='start' else part.end
    candidates=[]
    for item in items:
        if not item.key.startswith(prefixes):
            continue
        try:
            mate=observed_mate(item,part,end)
        except ValueError:
            continue
        f=_load('assembly_tube_machining')
        direction=f.unit(sub(mate.end,mate.start))
        station=dot(sub(endpoint,mate.start),direction)
        node=[mate.start[i]+direction[i]*station for i in range(3)]
        distance=math.dist(node,endpoint)
        if distance<=max(mate.profile.width,mate.profile.depth)/2+1.e-6:
            candidates.append((distance,item,mate))
    if not candidates:
        return None,None
    _,item,mate=min(candidates,key=lambda value:value[0])
    return item,mate


def transform_source(properties,placement):
    """Carry real participant poses through a product surface placement."""
    source=properties.get('tubeDesigner.assemblyProcessSource')
    if not source:
        return
    axes=[placement[name] for name in ('xAxis','yAxis','zAxis')]
    origin=placement['origin']
    def vector(value):
        return [sum(axes[j][i]*value[j] for j in range(3)) for i in range(3)]
    def matrix(value):
        translation=vector([value[i] for i in (3,7,11)])
        columns=[vector([value[4*i+j] for i in range(3)]) for j in range(3)]
        return [v for i in range(3) for v in (*[axis[i] for axis in columns],translation[i]+origin[i])]+[0.,0.,0.,1.]
    for stock in source['stocks']:
        stock['matrix']=matrix(stock['matrix'])
    for instance in source['instances']:
        local=instance['processInput']
        for part in local['parts'].values():
            if 'matrix' in part:
                part['matrix']=matrix(part['matrix'])
        if 'mateDirection' in local['geometry']:
            local['geometry']['mateDirection']=vector(local['geometry']['mateDirection'])
