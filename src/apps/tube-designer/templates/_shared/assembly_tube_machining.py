"""Local, repeatable tube machining functions.

All coordinates are actual geometry: stock X is longitudinal, its exact
section occupies YZ, and participant matrices place their own X/YZ frames.
There are no product names, member keys, catalogue shapes or SDK calls here.
"""
from copy import deepcopy
import hashlib
import importlib.util
import math
from pathlib import Path
import sys

IDENTITY = [1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.]
DEPENDENCIES = ['profile_recognition_geometry.py','section_geometry.py','tube_profile_catalog.py']


def _load(name):
    path = Path(__file__).with_name(name + '.py')
    key = 'icax_tube_machining_' + name + '_' + hashlib.sha256(path.read_bytes()).hexdigest()[:16]
    if key not in sys.modules:
        spec = importlib.util.spec_from_file_location(key, path)
        module = importlib.util.module_from_spec(spec)
        sys.modules[key] = module
        spec.loader.exec_module(module)
    return sys.modules[key]


def _number(value, label, minimum=None):
    if type(value) not in (int, float) or not math.isfinite(value) or minimum is not None and value < minimum:
        raise ValueError(label + ' 数值无效')
    return float(value)


def _vector(value, label):
    if not isinstance(value, (list, tuple)) or len(value) != 3:
        raise ValueError(label + ' 坐标无效')
    return [_number(v, label) for v in value]


def dot(a, b):
    return sum(x*y for x,y in zip(a,b))


def cross(a, b):
    return [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]]


def unit(value):
    value = _vector(value, '方向')
    norm = math.sqrt(dot(value,value))
    if norm <= 1.e-8:
        raise ValueError('加工方向不能退化')
    return [v/norm for v in value]


def matrix(value):
    value = deepcopy(value if value is not None else IDENTITY)
    if not isinstance(value,(list,tuple)) or len(value) != 16:
        raise ValueError('参与管件矩阵须为 row-major 16')
    value = [_number(v, '参与管件矩阵') for v in value]
    axes = [[value[4*i+j] for i in range(3)] for j in range(3)]
    if (any(abs(dot(a,a)-1)>1.e-6 for a in axes) or
        any(abs(dot(axes[i],axes[j]))>1.e-6 for i,j in ((0,1),(0,2),(1,2))) or
        dot(cross(axes[0],axes[1]),axes[2]) < 1-1.e-6 or
        any(abs(value[i]-v)>1.e-8 for i,v in ((12,0),(13,0),(14,0),(15,1)))):
        raise ValueError('参与管件矩阵须为右手正交刚体矩阵')
    return value


def _local(stock_matrix, world, vector=False):
    p = [world[i]-(0 if vector else stock_matrix[4*i+3]) for i in range(3)]
    return [sum(stock_matrix[4*i+j]*p[i] for i in range(3)) for j in range(3)]


def _pose(stock, participant):
    sm, pm = stock['matrix'], participant['matrix']
    return {'origin':_local(sm,[pm[3],pm[7],pm[11]]),
            **{name:_local(sm,[pm[4*i+j] for i in range(3)],True)
               for j,name in enumerate(('xAxis','yAxis','zAxis'))}}


def _section(value):
    if not isinstance(value,dict):
        raise ValueError('加工须提供参与管件的真实截面')
    profile = value.get('profile',value)
    if not isinstance(profile,dict) or not isinstance(profile.get('contours'),list) or not profile['contours']:
        raise ValueError('加工须提供精确闭合截面轮廓')
    # Geometric parsing is independent of profile templates and does not rebuild.
    geom = _load('profile_recognition_geometry')
    loops = [geom.contour(c,1.e-3) for c in profile['contours']]
    box = geom.bounds(loops[0])
    return deepcopy(profile['contours']), {'min':[box[0],box[1]],'max':[box[2],box[3]]}


def _part(value,section_only=False):
    if not isinstance(value,dict):
        raise ValueError('参与管件须为对象')
    contours, bounds = _section(value.get('section'))
    wall = value.get('section',{}).get('wallThickness',value.get('section',{}).get('profile',{}).get('wallThickness'))
    if wall is not None:
        wall = _number(wall,'管壁厚度',1.e-8)
    result={'contours':contours,'bounds':bounds,'wallThickness':wall}
    if not section_only:
        result.update(length=_number(value.get('length'),'参与管件长度',1.e-8),matrix=matrix(value.get('matrix')))
    return result


def _input(value, roles,section_only=False):
    if (not isinstance(value,dict) or value.get('schema')!='icax.assembly-process-input' or
        value.get('schemaVersion')!=1 or set(value)-{'schema','schemaVersion','parts','geometry'}):
        raise ValueError('局部加工输入版本或字段无效')
    parts = value.get('parts')
    if not isinstance(parts,dict) or set(parts)!=set(roles):
        raise ValueError('局部加工参与角色无效')
    geometry = value.get('geometry')
    if not isinstance(geometry,dict):
        raise ValueError('局部加工几何须为对象')
    return {k:_part(v,section_only) for k,v in parts.items()},deepcopy(geometry)


class _Graph:
    def __init__(self):
        self.nodes=[]

    def node(self,key,operator,inputs=None,arguments=None):
        self.nodes.append({'key':key,'operator':operator,'inputs':list(inputs or []),'arguments':deepcopy(arguments or {})})
        return key

    def prism(self,key,points,origin,u,v,axis,depth):
        profile=self.node(key+'.profile','profile2d',arguments={
            'placement':{'origin':origin,'xAxis':u,'yAxis':v},
            'contours':[{'kind':'path','closed':True,'segments':[
                {'kind':'line','start':a,'end':b} for a,b in zip(points,points[1:]+points[:1])]}]})
        return self.node(key+'.solid','extrude',[profile],{'vector':[x*depth for x in axis]})


def _result(function_id,process_input,parameters,build):
    result={'schema':'icax.assembly-process-result','schemaVersion':1,'templateId':function_id,
            'applicable':False,'reason':'','parameters':deepcopy(parameters),'materialRoles':['stock'],'geometrySpace':'stock',
            'geometry':[],'operations':[],'materialRequirements':[],'forming':[],
            'checks':[],'contactRequirements':[],'assemblySteps':[],'bom':[]}
    try:
        if not isinstance(parameters,dict):
            raise ValueError('工艺参数须为对象')
        graph=_Graph()
        extra=build(graph)
        result.update(applicable=True,geometry=graph.nodes,**extra)
    except (ValueError,KeyError,TypeError,OverflowError) as error:
        result.update(reason=str(error) or '加工必要数据无效',geometry=[],operations=[])
    return result


def _plane_cutter(graph,stock,point,inward):
    """A bounded halfspace prism, covering the actual stock envelope exactly."""
    n=unit(inward)
    if abs(n[0])<=1.e-8:
        raise ValueError('端切平面必须横截母材长轴')
    transverse=math.hypot(n[1],n[2])
    v=[0.,n[1]/transverse,n[2]/transverse] if transverse>1.e-8 else [0.,1.,0.]
    axis=cross([1.,0.,0.],v)
    b=stock['bounds']
    yz=[[0.,y,z] for y in (b['min'][0],b['max'][0]) for z in (b['min'][1],b['max'][1])]
    low,high=min(dot(p,v) for p in yz)-2.,max(dot(p,v) for p in yz)+2.
    alo,ahi=min(dot(p,axis) for p in yz)-2.,max(dot(p,axis) for p in yz)+2.
    center=dot(point,n)
    f=lambda t:(center-dot(v,n)*t)/n[0]
    if n[0]>0:
        outside=min(-2.,f(low)-2.,f(high)-2.)
    else:
        outside=max(stock['length']+2.,f(low)+2.,f(high)+2.)
    points=[[outside,low],[f(low),low],[f(high),high],[outside,high]]
    return graph.prism('end-cut',points,[x*alo for x in axis],[1.,0.,0.],v,axis,ahi-alo)


def tube_end_joint(process_input,parameters):
    """Resolve a joint from its real node and two member directions.

    `station` is the joint centre in the allocated stock; a miter bisects the
    two outgoing axes. Butt/insert stop at the mate's real near wall plus the
    declared penetration. The mate is observed geometry, never extra material.
    """
    def build(graph):
        roles=('stock','mate') if isinstance(process_input,dict) and 'mate' in process_input.get('parts',{}) else ('stock',)
        parts,g=_input(process_input,roles)
        if set(parameters)-{'insertionDepth','clearance'} or set(g)-{'mode','end','station','mateDirection'}:
            raise ValueError('端连接工艺字段无效')
        stock=parts['stock']
        end,mode=g.get('end'),g.get('mode')
        if end not in ('start','end') or mode not in ('miter','butt','insert','wrap'):
            raise ValueError('端连接类型或端点无效')
        station=_number(g.get('station'),'真实接点站位')
        outgoing=[1.,0.,0.] if end=='start' else [-1.,0.,0.]
        if mode=='miter':
            if not 0<=station<=stock['length']:
                raise ValueError('斜拼接点超出分配母材')
            direction=_local(stock['matrix'],unit(g.get('mateDirection')),True)
            if abs(dot(outgoing,direction))>=1-1.e-8:
                raise ValueError('斜切连接需要不共线的两根管件')
            inward=unit([a-b for a,b in zip(outgoing,direction)])
            point=[station,0.,0.]
        else:
            if 'mate' not in parts:
                raise ValueError('对接/插接需要实际接收管件')
            mate=parts['mate']
            pose=_pose(stock,mate)
            if 'mateDirection' in g:
                raise ValueError('对接/插接使用实际接收管坐标，不接受替代方向')
            requested_depth=parameters.get('insertionDepth',0.)
            penetration=0. if requested_depth=='mid-section' else _number(requested_depth,'入榫深度',0.)
            clearance=_number(parameters.get('clearance',0.),'配合间隙',0.)
            if mode in ('butt','wrap') and penetration:
                raise ValueError('贴焊/外包连接不能带入榫深度')
            if abs(pose['xAxis'][0])>1.e-7:
                raise ValueError('贴焊/插接的接收管轴线必须横截连接方向')
            supports=[sum(pose[name][0]*v for name,v in zip(('yAxis','zAxis'),(y,z)))
                      for y in (mate['bounds']['min'][0],mate['bounds']['max'][0])
                      for z in (mate['bounds']['min'][1],mate['bounds']['max'][1])]
            span=max(supports)-min(supports)
            if span<=1.e-8:
                raise ValueError('接收管截面未横截连接方向')
            if requested_depth=='mid-section':
                if mode!='insert':
                    raise ValueError('截面中部入榫只适用于插接')
                penetration=span/2
            _insertion_fit(mate,stock,penetration,clearance,span)
            if abs(pose['origin'][0]-station)>1.e-6:
                raise ValueError('连接站位必须来自实际接收管轴线')
            surface=(min(supports) if end=='start' else max(supports)) if mode=='wrap' else (
                max(supports) if end=='start' else min(supports))
            point=[station+surface-outgoing[0]*penetration,0.,0.]
            inward=outgoing
        retained_station=point[0]
        allocation={'startStation':retained_station if end=='start' else 0.,
                    'endStation':stock['length'] if end=='start' else retained_station,
                    'length':stock['length']-retained_station if end=='start' else retained_station}
        if mode=='miter':
            b=stock['bounds']
            stations=[(dot(point,inward)-y*inward[1]-z*inward[2])/inward[0]
                      for y in (b['min'][0],b['max'][0]) for z in (b['min'][1],b['max'][1])]
            allocation['rawStockStation']=min(stations) if end=='start' else max(stations)
        else:
            allocation['rawStockStation']=retained_station
        if allocation['length']<=1.e-8:
            raise ValueError('端连接切空了实际母材')
        boundary=(mode!='miter' and abs(point[0]-(0. if end=='start' else stock['length']))<1.e-7)
        if boundary:
            return {'operations':[], 'checks':[{'kind':'end-plane','end':end,'origin':point,
                'inwardNormal':inward,'jointMode':mode,'allocation':allocation}]}
        root=_plane_cutter(graph,stock,point,inward)
        return {'operations':[{'id':'stock:end-cut','kind':'neutral-csg','role':'stock',
                              'operation':'subtract','tool':root}],
                'checks':[{'kind':'end-plane','end':end,'origin':point,'inwardNormal':inward,
                           'jointMode':mode,'allocation':allocation}]}
    return _result('tube-end-joint',process_input,parameters,build)


def _wall(part):
    if part['wallThickness'] is not None:
        return part['wallThickness']
    return _load('section_geometry').closed_shell_metrics(
        _load('section_geometry').from_profile({'contours':part['contours']}))['wallThickness']


def _through_fit(receiver,branch,clearance):
    rb,bb=receiver['bounds'],branch['bounds']
    available=min(rb['max'][i]-rb['min'][i] for i in (0,1))-2*_wall(receiver)
    required=max(bb['max'][i]-bb['min'][i] for i in (0,1))+2*clearance
    if available<=required:
        raise ValueError(f'无法安全穿管：接收管内腔较小边 {available:g} mm，必须大于穿杆及开孔间隙 {required:g} mm')


def _insertion_fit(receiver,branch,depth,clearance,span):
    if not depth:
        return
    wall=_wall(receiver)
    if depth<=wall+clearance+1.e-7 or depth>=span-wall-clearance-1.e-7:
        raise ValueError('入榫深度必须穿过内侧管壁并保留外侧管壁')
    _through_fit(receiver,branch,clearance)


def _envelope(part,pose,clearance=0.,extension=0.):
    b=part['bounds']
    points=[[pose['origin'][i]+pose['xAxis'][i]*x+pose['yAxis'][i]*y+pose['zAxis'][i]*z for i in range(3)]
            for x in (-extension,part['length']+extension)
            for y in (b['min'][0]-clearance,b['max'][0]+clearance)
            for z in (b['min'][1]-clearance,b['max'][1]+clearance)]
    return {'min':[min(p[i] for p in points) for i in range(3)],
            'max':[max(p[i] for p in points) for i in range(3)]}


def tube_profile_aperture(process_input,parameters):
    """Sweep an exact branch outer boundary through one actual stock site."""
    def build(graph):
        parts,g=_input(process_input,('stock','branch'))
        if set(parameters)-{'clearance','checkFit'} or set(g)-{'clipInterval','keepPlanes'}:
            raise ValueError('穿管开孔工艺字段无效')
        stock,branch=parts['stock'],parts['branch']
        clearance=_number(parameters.get('clearance',0.),'配合间隙',0.)
        check_fit=parameters.get('checkFit',True)
        if type(check_fit) is not bool:
            raise ValueError('穿管配合检查选项须为布尔值')
        if check_fit:
            _through_fit(stock,branch,clearance)
        pose=_pose(stock,branch)
        if abs(pose['xAxis'][0])>=1-1.e-8:
            raise ValueError('穿管开孔须与母材轴线相交')
        outer=deepcopy(branch['contours'][0])
        if clearance:
            outer=_load('tube_profile_catalog')._offset_outer_contour(outer,clearance)
        env=_envelope(branch,pose,clearance,clearance+1.)
        bounds={'min':[0.,*stock['bounds']['min']],'max':[stock['length'],*stock['bounds']['max']]}
        overlap={'min':[max(env['min'][i],bounds['min'][i]) for i in range(3)],
                 'max':[min(env['max'][i],bounds['max'][i]) for i in range(3)]}
        clip=g.get('clipInterval')
        if clip is not None:
            if not isinstance(clip,(list,tuple)) or len(clip)!=2:
                raise ValueError('母材加工分区无效')
            lo,hi=(_number(v,'母材分区站位',0.) for v in clip)
            if hi<=lo or hi>stock['length']+1.e-7:
                raise ValueError('母材加工分区超界或退化')
            overlap['min'][0]=max(overlap['min'][0],lo)
            overlap['max'][0]=min(overlap['max'][0],hi)
        if any(overlap['max'][i]-overlap['min'][i]<=1.e-7 for i in range(3)):
            if clip is not None:
                return {'operations':[],'checks':[{'kind':'no-contact-in-stock-interval','interval':list(clip)}]}
            raise ValueError('穿管开孔没有穿入该母材')
        planes=g.get('keepPlanes',[])
        if not isinstance(planes,list):
            raise ValueError('保留端面须为数组')
        for plane in planes:
            if not isinstance(plane,dict) or set(plane)!={'origin','inwardNormal'}:
                raise ValueError('保留端面数据无效')
            n=unit(plane['inwardNormal']);p=_vector(plane['origin'],'端面原点')
            near=[overlap['min'][i] if n[i]>=0 else overlap['max'][i] for i in range(3)]
            if dot([near[i]-p[i] for i in range(3)],n)<=1.e-7:
                raise ValueError('插接孔进入斜切端面，请调整布置，使孔与拼角保持分离')
        profile=graph.node('aperture.profile','profile2d',arguments={
            'placement':{'origin':pose['origin'],'xAxis':pose['yAxis'],'yAxis':pose['zAxis']},'contours':[outer]})
        # A fully through branch can be bounded to the real receiver envelope.
        # Blind/near-edge cuts retain their original endpoints and penetration.
        projected=[dot([p[i]-pose['origin'][i] for i in range(3)],pose['xAxis'])
                   for x in (0.,stock['length'])
                   for y in (stock['bounds']['min'][0],stock['bounds']['max'][0])
                   for z in (stock['bounds']['min'][1],stock['bounds']['max'][1])
                   for p in ([x,y,z],)]
        tool_length=branch['length']
        if min(projected)>=1.-1.e-7 and max(projected)+1.<=tool_length+1.e-7:
            offset=min(projected)-1.
            graph.nodes[-1]['arguments']['placement']['origin']=[pose['origin'][i]+offset*pose['xAxis'][i] for i in range(3)]
            tool_length=max(projected)-min(projected)+2.
        root=graph.node('aperture.solid','extrude',[profile],{
            'vector':[a*tool_length for a in pose['xAxis']],
            'extendStart':clearance+1.,'extendEnd':clearance+1.})
        if clip is not None:
            ymin,ymax=stock['bounds']['min'][0]-2.,stock['bounds']['max'][0]+2.
            zmin,zmax=stock['bounds']['min'][1]-2.,stock['bounds']['max'][1]+2.
            cutter=graph.prism('aperture.clip',[[lo,zmin],[hi,zmin],[hi,zmax],[lo,zmax]],
                               [0.,ymin,0.],[1.,0.,0.],[0.,0.,1.],[0.,1.,0.],ymax-ymin)
            root=graph.node('aperture.cropped','boolean',[root,cutter],{'operation':'intersect','target':root,'tools':[cutter]})
        return {'operations':[{'id':'stock:aperture','kind':'neutral-csg','role':'stock','operation':'subtract','tool':root}],
                'checks':[{'kind':'profile-aperture','bounds':overlap,'clearance':clearance}]}
    return _result('tube-profile-aperture',process_input,parameters,build)


def tube_insertion_check(process_input,parameters):
    """Validate one insertion or two neighboring actual branch envelopes."""
    def build(graph):
        roles=('stock','branch','other') if isinstance(process_input,dict) and 'other' in process_input.get('parts',{}) else ('stock','branch')
        raw_geometry=process_input.get('geometry',{}) if isinstance(process_input,dict) else {}
        section_only=isinstance(raw_geometry,dict) and 'receiverSpan' in raw_geometry and not raw_geometry.get('checkInterference',False)
        parts,g=_input(process_input,roles,section_only)
        if set(parameters)-{'clearance','depth'} or set(g)-{'receiverSpan','checkThrough','checkInterference'}:
            raise ValueError('插接配合检查字段无效')
        if any(type(g[key]) is not bool for key in ('checkThrough','checkInterference') if key in g):
            raise ValueError('配合检查选项须为布尔值')
        stock,branch=parts['stock'],parts['branch']
        clearance=_number(parameters.get('clearance',0.),'配合间隙',0.)
        depth=_number(parameters.get('depth',0.),'入榫深度',0.)
        span=g.get('receiverSpan')
        if span is None:
            pose=_pose(stock,branch)
            span=sum(abs(pose['xAxis'][i+1])*(stock['bounds']['max'][i]-stock['bounds']['min'][i]) for i in (0,1))
        span=_number(span,'接收方向截面跨度',1.e-8)
        if g.get('checkThrough',False):
            _through_fit(stock,branch,clearance)
        _insertion_fit(stock,branch,depth,clearance,span)
        if g.get('checkInterference',False):
            if 'other' not in parts:
                raise ValueError('相邻入榫干涉检查缺少另一根实际杆件')
            first=_envelope(branch,_pose(stock,branch))
            second=_envelope(parts['other'],_pose(stock,parts['other']))
            if all(min(first['max'][i],second['max'][i])-max(first['min'][i],second['min'][i])>1.e-7 for i in range(3)):
                raise ValueError('接收管内的两根杆件端部相交，请减小入榫深度或错开连接位置')
        return {'operations':[],'checks':[{'kind':'insertion-fit','depth':depth,'clearance':clearance,'receiverSpan':span}]}
    return _result('tube-insertion-check',process_input,parameters,build)


FUNCTIONS={'tube-end-joint':tube_end_joint,'tube-profile-aperture':tube_profile_aperture,
           'tube-insertion-check':tube_insertion_check}
