"""Design straight members first; allocate stocks and call local processes later."""
from copy import deepcopy
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import sys

from icax_template_sdk import NeutralModel


def sub(a,b):
    return [x-y for x,y in zip(a,b)]


def dot(a,b):
    return sum(x*y for x,y in zip(a,b))


def unit(a):
    length=math.sqrt(dot(a,a))
    if length <= 1e-8:
        raise ValueError('成品管段不能退化为零长度')
    return [x/length for x in a]


def cross(a,b):
    return [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]]


def _logical_segment(item,parameters,frame_width=None):
    existing=item['properties'].get('assemblyFrame.member')
    if existing:
        frame=existing['sectionFrame']
        return {'start':deepcopy(existing['start']),'end':deepcopy(existing['end']),
            'placement':{'origin':deepcopy(frame['originAtStart']),
                         'xAxis':deepcopy(frame['xAxis']),'yAxis':deepcopy(frame['yAxis'])},
            'centerlineUV':deepcopy(frame['centerlineUV']),
            'profileCoordinateMap': ([[0.,1.],[1.,0.]] if abs(existing['end'][0]-existing['start'][0]) >= abs(existing['end'][2]-existing['start'][2])
                                     else [[1.,0.],[0.,1.]]),
            'contours':deepcopy(item['properties']['tubeDesigner.profile']['contours'])}
    segment=deepcopy(item['properties']['tubeDesigner.designSegment'])
    start,end=segment['start'],segment['end']
    direction=unit(sub(end,start))
    key=item['key']
    half=item['properties']['tubeDesigner.profile']['width']/2
    if parameters.get('faceType','single') != 'single' and key.startswith('outer_frame.vertical.'):
        # Independent corner-post sections retain the frame centreline datum.
        # Their own half-width does not move the three-way assembly node.
        if _runtime('security_window_post_connections').active_route(parameters) and parameters.get('foldedPostJoint','weld') != 'weld':
            half=float(frame_width)/2
        start[2],end[2]=half,float(parameters['height'])-half
    elif key.startswith(('outer_frame.','access_door.fixed_frame.','access_door.leaf.frame.')):
        if not (key.startswith('outer_frame.') and parameters.get('faceType','single')=='single'
                and parameters.get('frameLayout','four_sides')!='four_sides'):
            start[:]=[x+half*d for x,d in zip(start,direction)]
            end[:]=[x-half*d for x,d in zip(end,direction)]
    segment['placement']['origin']=list(start)
    return segment


def _anchor(key,member,end,point):
    return {'itemKey':key,'kind':'end','end':end,'anchor':{'kind':'end','end':end},
            'localAxialStation':member['axisLength'] if end=='end' else 0.0,
            'centerlinePoint':list(point)}


_ITEM_FACT_ALIASES = {
    'manufacturing.material': 'material',
    'manufacturing.materialGrade': 'materialGrade',
    'manufacturing.materialCategory': 'materialCategory',
    'manufacturing.categoryKey': 'categoryKey',
    'manufacturing.categoryName': 'categoryName',
    'manufacturing.partKind': 'partKind',
    'manufacturing.sourcing': 'sourcing',
}


def _design(parameters,context,core):
    # Joint drafts and stock routing never define a product's straight members.
    layout=deepcopy(parameters)
    layout.update(assemblyPlanningMode='builtin_rules',
                  frameManufacturingMode='segment_weld',frameCornerJoin='rail_miter',
                  frameJoinType='miter_45',doorFrameJoinType='miter_45',doorLeafFrameJoinType='miter_45',
                  assemblyClearance=0.0)
    layout['_foldedPostSectionEnabled'] = (
        _runtime('security_window_post_connections').active_route(parameters)
        and parameters.get('foldedPostJoint', 'weld') != 'weld')
    layout['_foldedPostTerminalSectionEnabled'] = (
        layout['_foldedPostSectionEnabled'] and parameters.get('frameManufacturingMode', 'segment_weld') == 'segment_weld')
    post_section = (core._profile(parameters, 'foldedPost')
                    if layout['_foldedPostSectionEnabled'] and parameters.get('faceType', 'single') == 'single' else None)
    if parameters.get('faceType', 'single') != 'single':
        layout['assemblyPlanningMode'] = 'builtin_rules'
    # Manufacturing drafts cannot alter the product input or reject its layout.
    # The old layout kernel still needs a harmless insertion datum to place bars;
    # use a section-derived value, independently of the editor's process drafts.
    frame = core._profile(parameters, 'frame')
    reserve = min(20.0, max(frame.wall + 0.1, min(5.0, frame.width / 2)))
    layout.update(horizontalBranchReserve=reserve, verticalBranchReserve=reserve)
    source=core._generate_manufacturing_geometry(layout,{**context,'geometryPurpose':'display',
                                                       '_finishedProductContract':True})
    model=NeutralModel(template_id=source['template']['id'],template_version=source['template']['version'],
                       package_digest=source['template']['packageDigest'],parameters=parameters)
    shared=core.SharedTubeGeometry(model)
    logical_segments = {item['key']: _logical_segment(item, parameters, frame.width) for item in source['items']}
    terminal_display_extensions = {}
    if (layout['_foldedPostSectionEnabled']
            and parameters.get('frameManufacturingMode', 'segment_weld') == 'segment_weld'):
        post_keys = [key for key in logical_segments if key.startswith(
            ('outer_frame.vertical.', 'outer_frame.left.', 'outer_frame.right.'))]
        rails = {item['key']: item for item in source['items'] if item['key'].startswith(
            ('outer_frame.top.', 'outer_frame.bottom.', 'outer_frame.back.'))}
        for post in post_keys:
            for post_end in ('start', 'end'):
                point = logical_segments[post][post_end]
                receivers = [key for key in rails if min(math.dist(point, logical_segments[key][end])
                             for end in ('start', 'end')) < 1.e-6]
                if len(receivers) == 1:
                    receiver = receivers[0]
                    end = min(('start', 'end'), key=lambda side: math.dist(point, logical_segments[receiver][side]))
                    terminal_display_extensions[(receiver, end)] = rails[receiver]['properties']['tubeDesigner.profile']['width'] / 2
    members={}
    sections={}
    def generic_contours(contours):
        # Generic geometry uses explicit curves. Preserve polygon vertices and
        # winding exactly while expressing its implicit closing edge as a line.
        return [_runtime('plate_geometry')._path(contour['points']) if contour.get('kind')=='polygon'
                else deepcopy(contour) for contour in contours]
    def section_resource(contours):
        # The tube component keeps its original section datum. Render-local
        # profile coordinates may have rotated axes or a different winding.
        contours=generic_contours(contours)
        identity=json.dumps({'contours':contours,'datum':'profile-local-xy'},
                            sort_keys=True,separators=(',',':'),allow_nan=False)
        if identity in sections:
            return sections[identity]
        for node in model._document['geometry']:
            if node['operator']!='profile2d' or node.get('inputs'):
                continue
            arguments=node['arguments']
            if set(arguments)-{'contours','placement'} or arguments.get('contours')!=contours:
                continue
            placement=arguments.get('placement',{})
            if (placement.get('origin',[0.,0.,0.])!=[0.,0.,0.]
                    or placement.get('xAxis',[1.,0.,0.])!=[1.,0.,0.]
                    or placement.get('yAxis',[0.,1.,0.])!=[0.,1.,0.]
                    or placement.get('zAxis',[0.,0.,1.])!=[0.,0.,1.]):
                continue
            sections[identity]=node['key']
            return node['key']
        key='design.section.'+hashlib.sha256(identity.encode('utf-8')).hexdigest()[:24]
        model.geometry(key,'profile2d',arguments={'contours':deepcopy(contours)})
        sections[identity]=key
        return key
    for item in source['items']:
        if not any(key in item['properties'] for key in ('tubeDesigner.designSegment','assemblyFrame.member')):
            raise ValueError('成品管段缺少真实中心线与截面: '+item['key'])
        segment=deepcopy(logical_segments[item['key']])
        if post_section is not None:
            if item['key'].startswith(('outer_frame.left.', 'outer_frame.right.')):
                item['properties']['tubeDesigner.profile'] = core._profile_properties(post_section)
                segment['contours'] = post_section.contours()
            elif item['key'].startswith('main_grid.horizontal.'):
                # The thinner post moves the physical receiving wall, while
                # its centreline remains at the established frame datum.
                original = frame.width - reserve
                actual = frame.width / 2 + post_section.width / 2 - reserve
                for endpoint in ('start', 'end'):
                    if abs(segment[endpoint][0] - original) < 1.e-7:
                        segment[endpoint][0] = actual
                    elif abs(segment[endpoint][0] - (float(parameters['width']) - original)) < 1.e-7:
                        segment[endpoint][0] = float(parameters['width']) - actual
                segment['placement']['origin'] = list(segment['start'])
        start,end=segment['start'],segment['end']
        axis=unit(sub(end,start))
        props={key:deepcopy(value) for key,value in item['properties'].items()
               if not key.startswith('manufacturing.') and key not in {
                   'tubeDesigner.designSegment','tubeDesigner.endProcess','tubeDesigner.cornerProcess',
                   'tubeDesigner.connectionProcess','tubeDesigner.frameManufacturing',
                   'tubeDesigner.jointProcess','tubeDesigner.displayApproximation',
                   'tubeDesigner.assemblyPlanning'}}
        # Both display and manufacturing reference these same intrinsic facts.
        for original, neutral in _ITEM_FACT_ALIASES.items():
            if original in item['properties']:
                props[neutral]=deepcopy(item['properties'][original])
        props.setdefault('partKind', 'tube')
        props.setdefault('materialCategory', 'tube')
        props.setdefault('sourcing', 'made')
        props.setdefault('quantity', 1)
        props['length']=round(math.dist(start,end),3)
        section=segment['placement']
        across=unit(section['xAxis'])
        normal=unit(cross(axis,across))
        # The layout kernel declares the original section's coordinate map,
        # before render-resource canonicalization. Keep the original contours
        # and datum; a downstream consumer applies this intrinsic orientation.
        coordinate_map=deepcopy(segment.get('profileCoordinateMap', [[1.,0.],[0.,1.]]))
        orientation=dot(unit(section['yAxis']),normal)
        if abs(abs(orientation)-1.)>1.e-7:
            raise ValueError('管型截面与零件轴不正交: '+item['key'])
        coordinate_map[1]=[orientation*value for value in coordinate_map[1]]
        props['tubeDesigner.profile']['sectionCoordinateMap']=coordinate_map
        props['assemblyFrame.member']={'start':start,'end':end,'axisLength':math.dist(start,end),
            'sectionFrame':{'originAtStart':deepcopy(section['origin']),
                'xAxis':across,'yAxis':normal,
                'zAxis':axis,'centerlineUV':deepcopy(segment.get('centerlineUV',[0.0,0.0])),
                'faceNormals':{'right':across,'left':[-v for v in across],
                               'top':normal,'bottom':[-v for v in normal]}}}
        display_start, display_end = list(start), list(end)
        for endpoint in ('start', 'end'):
            extension = terminal_display_extensions.get((item['key'], endpoint), 0.)
            if extension:
                target = display_start if endpoint == 'start' else display_end
                sign = -1 if endpoint == 'start' else 1
                target[:] = [value + sign * extension * direction for value, direction in zip(target, axis)]
        display_section = deepcopy(section)
        display_section['origin'] = display_start
        solid=shared.emit_tube('design.'+item['key'],
            profile_arguments={'placement':display_section,'contours':generic_contours(segment['contours'])},
            extrude_arguments={'vector':sub(display_end,display_start)})
        props['tubeDesigner.profile']['sectionResource']=section_resource(props['tubeDesigner.profile']['contours'])
        model.item(item['key'],item['displayName'],representations={'display':solid},properties=props)
        members[item['key']]=props['assemblyFrame.member']
    # Permanent connections reference logical members, never a manufacturing blank.
    for relation in source.get('relationships',[]):
        if set(relation['items']) <= set(members):
            model.relationship(relation['key'],relation['kind'],relation['items'],properties=relation['properties'])
    if parameters.get('faceType','single')!='single':
        posts=sorted(key for key in members if key.startswith('outer_frame.vertical.'))
        def add_corner(index,side,keys,c_end):
            point=members[keys[2]][c_end]
            ends=[]
            for key in keys:
                end=min(('start','end'),key=lambda end:math.dist(members[key][end],point))
                if math.dist(members[key][end],point)>1e-6:
                    raise ValueError('三向成品节点的三根直管没有共用端点')
                ends.append(end)
            model.relationship(f'outer_frame.corner.{side}.{index:04d}','assembly',keys,properties={
                'topology':'orthogonal-corner','centerlinePoint':point,'origin':point,
                'participantAnchors':[_anchor(key,members[key],end,point) for key,end in zip(keys,ends)]})
        for index in range(1,len(posts)-1):
            for side,c_end in (('top','end'),('bottom','start')):
                keys=[f'outer_frame.{side}.{index:04d}',f'outer_frame.{side}.{index+1:04d}',posts[index]]
                add_corner(index,side,keys,c_end)
        if parameters.get('faceType')=='five':
            for side,c_end,back in (('top','end','outer_frame.back.0001'),
                                    ('bottom','start','outer_frame.back.0002')):
                add_corner(0,side,[back,f'outer_frame.{side}.0001',posts[0]],c_end)
                add_corner(len(posts)-1,side,[f'outer_frame.{side}.{len(posts)-1:04d}',back,posts[-1]],c_end)
    keys=list(members)
    model.output('display.default','display',keys)
    model.table('product.members','成品构件',columns=[{'key':'name','displayName':'构件','valueType':'string'},
        {'key':'length','displayName':'中心线长度','valueType':'number','unit':'mm'}],rows=[{
            'key':'product.row.'+item['key'],'itemKey':item['key'],
            'values':{'name':item['displayName'],'length':item['properties']['length']}}
        for item in model.build()['items']])
    document=model.build()
    document['extensions']=deepcopy(source.get('extensions',{}))
    document['extensions'].pop('tubeDesigner.geometryPurpose',None)
    document['extensions'].pop('tubeDesigner.manufacturingPartCount',None)
    document['extensions']['tubeDesigner.finishedProductContractVersion']=1
    document['extensions']['tubeDesigner.productStockMappingVersion']=1
    review=document['extensions'].get('tubeDesigner.securityWindowReview',{})
    for key in ('outerFrameManufacturing','outerFrameConnection','vGrooveTrialRequired','displayHasUncutMiterStock'):
        review.pop(key,None)
    document['diagnostics']=deepcopy(source.get('diagnostics',[]))
    return document


def _straight_span(segment,key,stock_start=0.0,logical=None):
    start,end=segment['start'],segment['end']
    axial=unit(sub(end,start))
    transverse=unit(segment['placement']['xAxis'])
    normal=unit(cross(axial,transverse))
    axes=(axial,transverse,normal)
    section_origin=segment['placement'].get('origin',start)
    material_origin=[value-stock_start*direction for value,direction in zip(section_origin,axial)]
    result={'itemKey':key,'start':start,'end':end,'stockStart':stock_start,'startReserve':0.0,'endReserve':0.0,
        'placement':{'origin':[-dot(material_origin,axis) for axis in axes],
                     **{name:[axis[index] for axis in axes]
                        for index,name in enumerate(('xAxis','yAxis','zAxis'))}}}
    if logical is not None:
        stations=[]
        for point in (logical['start'],logical['end']):
            delta=sub(point,start)
            station=dot(delta,axial)
            if math.dist(delta,[station*value for value in axial])>1e-6:
                raise ValueError('下料直管与输入成品管段不共用中心线: '+key)
            stations.append(station)
        low=max(0.0,min(stations))
        high=min(math.dist(start,end),max(stations))
        if high-low<=1e-8:
            raise ValueError('下料直管没有覆盖输入成品管段的实际区间: '+key)
        result['start']=[value+low*direction for value,direction in zip(start,axial)]
        result['end']=[value+high*direction for value,direction in zip(start,axial)]
        result['stockStart']=stock_start+low
        logical_axis=unit(sub(logical['end'],logical['start']))
        result['sourceStartStation']=dot(sub(result['start'],logical['start']),logical_axis)
        result['sourceEndStation']=dot(sub(result['end'],logical['start']),logical_axis)
    return result


def _runtime(name='assembly_template_runtime'):
    path=Path(__file__).with_name(name+'.py')
    name='icax_window_node_functions_'+hashlib.sha256(path.read_bytes()).hexdigest()[:16]
    if name not in sys.modules:
        spec=importlib.util.spec_from_file_location(name,path)
        module=importlib.util.module_from_spec(spec)
        sys.modules[name]=module
        spec.loader.exec_module(module)
    return sys.modules[name]


def _matrix(origin,axis,across,normal):
    return [axis[0],across[0],normal[0],origin[0],
            axis[1],across[1],normal[1],origin[1],
            axis[2],across[2],normal[2],origin[2],0,0,0,1]


def _shared_post_processes(document,design,source,raw_sections):
    """Each shared post receives one local corner function at each end."""
    if not any(item['key'].startswith('outer_frame.') and
               item['properties'].get('tubeDesigner.frameManufacturing',{}).get('mode')
               in ('plane_v_notch','spatial_v_notch') for item in document['items']):
        return
    by_design={item['key']:item for item in design['items']}
    by_stock={item['key']:item for item in document['items']}
    allocated={stock['id']:stock for stock in source['stocks']}
    references={}
    rebased_posts=set()
    for item in document['items']:
        for member in item['properties']['manufacturing.sourceMembers']:
            references[member['itemKey']]=(item,member['spans'])
    for node in design['relationships']:
        if node['properties'].get('topology')!='orthogonal-corner':
            continue
        a_key,b_key,c_key=node['items']
        if c_key not in by_stock or a_key not in references or b_key not in references:
            continue
        if not all(references[key][0]['properties'].get('tubeDesigner.assemblyProcessStockId')
                   for key in (a_key,b_key)):
            continue
        point=node['properties']['centerlinePoint']
        parts,targets={},{}
        for role,key in (('memberA',a_key),('memberB',b_key)):
            item,spans=references[key]
            span=min(spans,key=lambda s:min(math.dist(point,s['start']),math.dist(point,s['end'])))
            if min(math.dist(point,span['start']),math.dist(point,span['end']))>1e-6:
                raise ValueError('连续母材源段没有覆盖共享立柱的实际节点')
            stock_id=item['properties']['tubeDesigner.assemblyProcessStockId']
            stock=allocated[stock_id]
            placement=span['placement']
            axes=[[placement[name][index] for name in ('xAxis','yAxis','zAxis')]
                  for index in range(3)]
            section=item['properties']['tubeDesigner.assemblyProcessStockProfile']
            parts[role]={'id':key,'profileRef':deepcopy(stock['profileRef']),
                'parameters':deepcopy(stock['parameters']),'length':math.dist(span['start'],span['end']),
                'matrix':_matrix(span['start'],*axes),'section':{'profile':deepcopy(section)},
                'anchor':{'kind':'end','end':'start' if math.dist(point,span['start'])<1e-6 else 'end',
                          'trim':0,'rotation':0}}
            targets[role]={'stockId':stock_id,'start':span['stockStart']+span['startReserve'],'reverse':False}
        item=by_stock[c_key]
        props=item['properties']
        member=by_design[c_key]['properties']['assemblyFrame.member']
        if c_key not in rebased_posts:
            axis=unit(sub(member['end'],member['start']))
            across=unit(member['sectionFrame']['xAxis'])
            normal=unit(cross(axis,across))
            stock={'id':c_key,'label':item['displayName'],
                'profileRef':{'scope':'template','id':c_key,'templateId':document['template']['id']},
                'parameters':{},'length':member['axisLength'],
                'matrix':_matrix(member['start'],axis,across,normal)}
            if c_key in allocated:
                allocated[c_key].update(stock)
                stock=allocated[c_key]
            else:
                source['stocks'].append(stock)
                allocated[c_key]=stock
            props['tubeDesigner.assemblyProcessStockId']=c_key
            props['tubeDesigner.assemblyProcessStockProfile']=deepcopy(props['tubeDesigner.profile'])
            props['tubeDesigner.assemblyProcessStockProfile']['contours']=deepcopy(raw_sections[c_key])
            profile=props['tubeDesigner.assemblyProcessStockProfile']
            profile['width'],profile['depth']=profile['depth'],profile['width']
            props['tubeDesigner.assemblyProcessStockLength']=stock['length']
            rebased_posts.add(c_key)
            props['tubeDesigner.assemblyProcessSecondaryCutters']=[]
            props['manufacturing.sourceMembers']=[{'itemKey':c_key,'spans':[_straight_span({
                'start':member['start'],'end':member['end'],
                'placement':{'xAxis':across,'yAxis':normal}},c_key)]}]
            # Apertures may already have allocated this post. Rebase those
            # local inputs onto the one full raw stock, keeping actual branch
            # world poses intact; both corner ends then consume its allowances.
            for instance in source['instances']:
                if instance.get('targets',{}).get('stock')!=c_key:
                    continue
                local=instance['processInput']['parts']['stock']
                local.update(length=stock['length'],matrix=deepcopy(stock['matrix']),
                             section={'profile':deepcopy(props['tubeDesigner.assemblyProcessStockProfile'])})
        stock=allocated[c_key]
        parts['memberC']={**deepcopy(stock),'id':c_key,
            'section':{'profile':deepcopy(props['tubeDesigner.assemblyProcessStockProfile'])},
            'anchor':{'kind':'end','end':'start' if math.dist(point,member['start'])<1e-6 else 'end',
                      'trim':0,'rotation':0}}
        targets['memberC']=c_key
        source['instances'].append({'instanceId':node['key']+'.post','templateId':'orthogonal-corner',
            'parameters':{'abJoint':'continuous','cJoint':'weld'},
            'processInput':{'schema':'icax.assembly-process-input','schemaVersion':1,
                            'parts':parts,'geometry':{}},'targets':targets})


def _manufacturing(parameters,context,core,design):
    document=core._generate_manufacturing_geometry(parameters,{**context,'geometryPurpose':'manufacturing'})
    records=document.get('extensions',{}).pop('tubeDesigner.assemblyProcessRecords',{})
    source={'schema':'icax.assembly-process-source','schemaVersion':1,'stocks':[],'instances':[]}
    design_keys={item['key'] for item in design['items']}
    design_members={item['key']:item['properties']['assemblyFrame.member'] for item in design['items']}
    assigned=set()
    raw_sections={}
    for item in document['items']:
        props=item['properties']
        if props.get('tubeDesigner.designSegment'):
            raw_sections[item['key']]=deepcopy(props['tubeDesigner.designSegment']['contours'])
        if item['key'] in records:
            props.update(deepcopy(records[item['key']]))
        local_source=props.pop('tubeDesigner.assemblyProcessSource',None)
        props.pop('tubeDesigner.assemblyProcessPlan',None)
        if local_source:
            source['stocks'].extend(deepcopy(local_source['stocks']))
            source['instances'].extend(deepcopy(local_source['instances']))
        spans=props.get('tubeDesigner.frameManufacturing',{}).get('spans') or props.get('tubeDesigner.sourceSpans')
        if not spans:
            segment=props.get('tubeDesigner.designSegment')
            stock_start=0.0
            if not segment and props.get('assemblyFrame.member'):
                member=props['assemblyFrame.member']
                section=member['sectionFrame']
                segment={'start':deepcopy(member['start']),'end':deepcopy(member['end']),
                         'placement':{'origin':deepcopy(section['originAtStart']),
                                      'xAxis':deepcopy(section['xAxis']),'yAxis':deepcopy(section['yAxis'])}}
                stock_start=-member.get('stockInterval',{}).get('startStation',0.0)
            if not segment or item['key'] not in design_keys:
                raise ValueError('下料件缺少成品管段来源: '+item['key'])
            spans=[_straight_span(segment,item['key'],stock_start,design_members[item['key']])]
        grouped={}
        for span in spans:
            key=span['itemKey']
            if key not in design_keys:
                raise ValueError('下料映射引用不存在的成品构件: '+key)
            grouped.setdefault(key,[]).append(deepcopy(span))
        if assigned.intersection(grouped):
            raise ValueError('同一成品构件被重复归入不同下料件')
        assigned.update(grouped)
        props['manufacturing.sourceMembers']=[{'itemKey':key,'spans':values} for key,values in grouped.items()]
        props.pop('tubeDesigner.designSegment',None)
        props.pop('tubeDesigner.sourceSpans',None)
        props.pop('assemblyFrame.member',None)
    if assigned!=design_keys:
        raise ValueError('成品构件与下料件映射没有完整覆盖')
    _shared_post_processes(document,design,source,raw_sections)
    stock_lengths={item['key']:item['properties']['length'] for item in document['items']}
    for table in document.get('tables',[]):
        for row in table['rows']:
            if row.get('itemKey') in stock_lengths and 'length' in row.get('values',{}):
                row['values']['length']=stock_lengths[row['itemKey']]
    document['extensions']['tubeDesigner.productStockMappingVersion']=1
    document['extensions']['tubeDesigner.finishedProductContractVersion']=1
    if source['stocks']:
        # Resolve all repeated folds and both post ends together before native
        # execution. A bad allocation must not become a partial saved product.
        _runtime().resolve_process_plan(source['stocks'],source['instances'],
            runtime_context={'userMouldRoot':context.get('userMouldRoot','')})
        document['extensions']['tubeDesigner.assemblyProcessSource']=source
    return document


def _result(design):
    result=deepcopy(design)
    keys=result['outputs'][0]['items']
    for item in result['items']:
        item['representations']={'result':item['representations']['display']}
    result['outputs']=[{'key':'result','purpose':'result','items':keys,'properties':{}}]
    result['extensions']['tubeDesigner.geometryPurpose']='display'
    return result


def _combined(design,stock):
    result=deepcopy(design)
    geometry_keys={node['key']:'stock.geometry.'+node['key'] for node in stock['geometry']}
    def rewrite(value):
        if isinstance(value,str): return geometry_keys.get(value,value)
        if isinstance(value,list): return [rewrite(v) for v in value]
        if isinstance(value,dict): return {k:rewrite(v) for k,v in value.items()}
        return value
    result['geometry'].extend(rewrite(stock['geometry']))
    items={item['key']:item for item in result['items']}
    stock_keys={item['key']:('manufacturing.'+item['key'] if item['key'] in items else item['key'])
                for item in stock['items']}
    for item in stock['items']:
        copied=deepcopy(item)
        copied['key']=stock_keys[item['key']]
        copied['representations']={'export':geometry_keys[item['representations']['result']]}
        if 'tubeDesigner.assemblyProcessSecondaryCutters' in copied['properties']:
            copied['properties']['tubeDesigner.assemblyProcessSecondaryCutters']=[
                geometry_keys[key] for key in copied['properties']['tubeDesigner.assemblyProcessSecondaryCutters']]
        result['items'].append(copied)
    result['outputs'].append({'key':'export.manufacturing','purpose':'export',
                              'items':[stock_keys[key] for key in stock['outputs'][0]['items']],'properties':{}})
    for table in stock.get('tables',[]):
        table=deepcopy(table)
        if any(existing['key']==table['key'] for existing in result['tables']):
            table['key']='manufacturing.'+table['key']
        for row in table['rows']:
            if row.get('itemKey') in stock_keys:
                row['itemKey']=stock_keys[row['itemKey']]
        result['tables'].append(table)
    result['extensions'].update({key:deepcopy(value) for key,value in stock['extensions'].items()
        if key in ('tubeDesigner.assemblyProcessSource','tubeDesigner.finishedProductContractVersion')})
    provenance=stock['extensions'].get('tubeDesigner.assemblyGeometryProcesses')
    if provenance:
        result['extensions']['tubeDesigner.assemblyGeometryProcesses']=_runtime(
            'assembly_geometry_process_runtime').remap_extension(provenance,geometry_keys)
    result['extensions']['tubeDesigner.productStockMappingVersion']=1
    result['extensions']['tubeDesigner.manufacturingPartCount']=len(stock['items'])
    return result



def generate(parameters,context,core):
    design=_design(parameters,context,core)
    purpose=core.request_geometry_purpose(context)
    if purpose=='display':
        return _result(design)
    stock=_manufacturing(parameters,context,core,design)
    return stock if purpose=='manufacturing' else _combined(design,stock)
