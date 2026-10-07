"""Downstream allocation of declared tube paths; never calls a product script."""
from copy import deepcopy
import math
from types import SimpleNamespace

from icax_template_sdk import NeutralModel

# Share only downstream helpers. Allocation must not import a product module.
import importlib.util
from pathlib import Path
_spec = importlib.util.spec_from_file_location('icax_material_input_helpers', Path(__file__).with_name('assembly_window_process.py'))
_data = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_data)
_load, sub, dot, cross, unit = _data._load, _data.sub, _data.dot, _data.cross, _data.unit
IDENTITY = [1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.]
_POST_GEOMETRY = None


def _post_geometry():
    global _POST_GEOMETRY
    if _POST_GEOMETRY is None:
        path=Path(__file__).resolve().parent.parent/'assembly/orthogonal-corner/geometry.py'
        specification=importlib.util.spec_from_file_location('icax_window_allocation_post_geometry',path)
        _POST_GEOMETRY=importlib.util.module_from_spec(specification)
        specification.loader.exec_module(_POST_GEOMETRY)
    return _POST_GEOMETRY


def _stock_input(part):
    return {key:deepcopy(part[key]) for key in ('id','label','profileRef','parameters','length','matrix')}


def _matrix(origin, axis, across, normal):
    return [value for i in range(3) for value in (axis[i],across[i],normal[i],origin[i])] + [0.,0.,0.,1.]


def _part(key, label, start, end, frame, profile):
    axis=unit(sub(end,start)); across=unit(frame['xAxis']); normal=unit(cross(axis,across))
    values=deepcopy(profile)
    recognition=_load('profile_recognition_geometry')
    bounds=recognition.bounds(recognition.contour(values['contours'][0],1.e-3))
    values.update(width=bounds[2]-bounds[0],depth=bounds[3]-bounds[1])
    return {'id':key,'label':label,'parameters':{},'length':math.dist(start,end),
        'profileRef':{'scope':'template','id':key,'templateId':'icax.manufacturing-allocation'},
        'matrix':_matrix(start,axis,across,normal),'section':{'profile':values,'wallThickness':values['wallThickness']}}


def _point(matrix, point):
    return [sum(matrix[4*i+j]*point[j] for j in range(3))+matrix[4*i+3] for i in range(3)]


def _placement(span):
    axes=(span.direction,span.y_axis,span.z_axis)
    return {'origin':[span.stock_start+span.start_reserve-dot(span.start,axes[0]),-dot(span.start,axes[1]),-dot(span.start,axes[2])],
            **{name:[axis[i] for axis in axes] for i,name in enumerate(('xAxis','yAxis','zAxis'))}}


def allocate_window_manufacturing(document, execution_context=None):
    """Return model, pending processes, allocationPlans, and the full assemblySource.

    Fold plans are already evaluated downstream for real allocation and must
    be executed from their resolved requests, rather than called again. Other
    process inputs are fully materialized data; no product/layout callback is needed.
    """
    from icax_template_sdk.manufacturing import is_manufacturing_declaration
    if is_manufacturing_declaration():
        raise RuntimeError('产品制造声明阶段禁止材料分配和工艺求值')
    context=deepcopy(execution_context or {})
    if document.get('schema')!='icax.manufacturing-model' or document.get('schemaVersion')!=2:
        raise ValueError('材料分配需要制造输入协议v2')
    items={item['key']:deepcopy(item) for item in document['items']}
    if any(item.get('manufacturingInput',{}).get('kind')!='tube-path' for item in items.values()):
        raise ValueError('窗口材料分配只处理明确的tube-path输入')
    resources={node['key']:node for node in document['resources']}
    stage_runtime=_load('assembly_process_stages')
    calls=deepcopy(document['processes'])
    # Path allocation starts from stable design facts. This entry point emits
    # a stock-operation source, which cannot encode after-state observations.
    # Keep those calls in the intrinsic-geometry executor instead of silently
    # turning their explicitly requested snapshots into initial material.
    for call in calls:
        for role,binding in call['definition']['processInput']['parts'].items():
            if binding.get('scope')=='manufacturing' and binding.get('state','initial')!='initial':
                raise ValueError('tube-path材料分配不支持after加工快照，请使用实体工序执行入口: '+call['key']+'/'+role)
    # A corner anchor explicitly declares an unfolding facet. Provider
    # capabilities establish the other barriers; dependencies remain binding.
    corner_stages={call['key']:'bend-unfolding' for call in calls
                   if call['definition']['processInput']['geometry'].get('anchor',{}).get('kind')=='path-corner'}
    stage_plan=stage_runtime.plan_stages(calls,stage_by_key=corner_stages)
    calls=stage_runtime.ordered_processes(calls,stage_by_key=corner_stages)
    by_key={call['key']:call for call in calls}
    stage_calls={batch['stage']:[by_key[key] for key in batch['processKeys']] for batch in stage_plan['stages']}
    paths={}; profiles={}; sources={}; owners={}
    for key,item in items.items():
        data=item['manufacturingInput']; path=data['path']; vertices={v['key']:v['point'] for v in path['vertices']}
        profiles[key]=deepcopy(item['properties']['tubeDesigner.profile'])
        profiles[key]['contours']=deepcopy(resources[data['profileResource']]['arguments']['contours'])
        spans=[]
        for segment in path['segments']:
            a,b=vertices[segment['from']],vertices[segment['to']]; frame=segment['sectionFrame']
            spans.append(SimpleNamespace(key=segment['key'],start=list(a),end=list(b),direction=unit(sub(b,a)),
                from_vertex=segment['from'],to_vertex=segment['to'],
                y_axis=list(frame['xAxis']),z_axis=list(frame['yAxis']),stock_start=0.,start_reserve=0.,end_reserve=0.,
                design_start=list(a),design_end=list(b)))
        paths[key]=spans
    # Restore exact design observers from explicit source fractions and paths.
    for mapping in document['sourceMappings']:
        owner=mapping['itemKey']; spans={s.key:s for s in paths[owner]}
        for source in mapping['sources']:
            entry=source['segments'][0]; span=spans[entry['segmentKey']]; a,b=entry['sourceRange']
            if abs(b-a)<1.e-9: raise ValueError('显示来源不能是零区间')
            direction=[value/(b-a) for value in sub(span.end,span.start)]
            start=[span.start[i]-a*direction[i] for i in range(3)]; end=[start[i]+direction[i] for i in range(3)]
            frame={'xAxis':span.y_axis,'yAxis':span.z_axis}
            source_profile=items[owner]['properties'].get('tubeDesigner.sourceMemberProfiles',{}).get(source['itemKey'],profiles[owner])
            source_part=_part(source['itemKey'],source['itemKey'],start,end,frame,source_profile)
            sources[source['itemKey']]={'part':source_part,'start':start,'end':end,'owner':owner,
                'segments':deepcopy(source['segments'])}
            owners[source['itemKey']]=owner
    geometry_runtime=_load('assembly_geometry_process_runtime')
    initial_material_allocations=[]
    boundary_calls=[call for call in stage_calls['material-requirements']
                    if call['definition']['processInput']['geometry'].get('allocation')=='initial-material-boundary']
    if len(boundary_calls)!=len(stage_calls['material-requirements']):
        raise ValueError('tube-path材料要求阶段只接受明确的初始材料边界工艺')
    for call in boundary_calls:
        definition=call['definition']; local=definition['processInput']; geometry=local['geometry']
        key=local['parts']['stock']['itemKey']; original=local['parts']['stock']['data']['sourceMember']
        end=geometry['anchor']['end']; observer=sources[original]; mate=sources[local['parts']['mate']['itemKey']]['part']
        span=next(span for span in paths[key] if span.key==observer['segments'][0 if end=='start' else -1]['segmentKey'])
        extent_policy=geometry.get('extentPolicy')
        if extent_policy is not None:
            if extent_policy!='nominal-member-section' or len(paths[key])!=1:
                raise ValueError('unsupported window initial material extent policy')
            # Established full-height post stock uses the nominal product
            # section width, independent of a neighbouring section's datum.
            # This policy is explicitly requested by the downstream window
            # planner; ordinary material boundaries still use the real mate.
            extent=float(items[key]['properties']['tubeDesigner.profile']['width'])/2
            if not math.isfinite(extent) or extent<=0: raise ValueError('invalid nominal member section extent')
            axis=unit(sub(observer['end'],observer['start']))
            sign=-1 if end=='start' else 1
            setattr(span,end,[observer[end][i]+sign*extent*axis[i] for i in range(3)])
            initial_material_allocations.append({'stockId':key,'sourceItemKey':original,'end':end,
                'policy':extent_policy,'point':deepcopy(getattr(span,end))})
            continue
        stock=_part(key,items[key]['displayName'],span.start,span.end,{'xAxis':span.y_axis},profiles[key])
        axis=[stock['matrix'][4*i] for i in range(3)]; origin=[stock['matrix'][4*i+3] for i in range(3)]
        station=dot(sub([mate['matrix'][4*i+3] for i in range(3)],origin),axis)
        result=geometry_runtime.evaluate('tube-end-joint',{'schema':'icax.assembly-process-input','schemaVersion':1,
            'parts':{'stock':stock,'mate':deepcopy(mate)},'geometry':{'mode':geometry['mode'],'end':end,'station':station}},definition['parameters'])
        if not result['applicable']: raise ValueError(call['key']+': '+result['reason'])
        plane=next(check for check in result['checks'] if check['kind']=='end-plane')
        setattr(span,end,_point(stock['matrix'],[plane['allocation']['rawStockStation'],0,0]))
    parts={}; fold_plans={}; fold_keys=set(); mapping_spans={}; fold_instances=[]; material_bends={}
    runtime=_load('assembly_template_runtime'); adapter=_load('security_window_process_adapter')
    def allocated_spans(spans, bends, allowance):
        return _load('security_window_frame_paths')._allocate(spans,bends,allowance)
    for key,item in items.items():
        spans=paths[key]; related=[call for call in stage_calls['bend-unfolding'] if call['definition']['processInput']['geometry'].get('anchor',{}).get('kind')=='path-corner'
                                and call['definition']['targets'].get('stock')==key]
        if not related:
            if len(spans)!=1: raise ValueError('连续路径缺少明确的转角工艺')
            span=spans[0]; parts[key]=_part(key,item['displayName'],span.start,span.end,
                {'xAxis':span.y_axis,'yAxis':span.z_axis},profiles[key])
            continue
        # Carry the unfolded section basis through the target path. Individual
        # design members may name equivalent support faces differently; those
        # observer frames were already captured above and stay unchanged.
        paths_module=_load('security_window_frame_paths')
        for index in range(1,len(spans)):
            previous,current=spans[index-1:index+1]
            cosine=max(-1.,min(1.,dot(previous.direction,current.direction)))
            if cosine>1-1.e-7:
                current.y_axis=list(previous.y_axis); current.z_axis=list(previous.z_axis)
            else:
                axis=unit(cross(previous.direction,current.direction)); angle=math.acos(cosine)
                current.y_axis=list(paths_module.rotate(previous.y_axis,axis,angle))
                current.z_axis=list(paths_module.rotate(previous.z_axis,axis,angle))
        by_segment={span.key:i for i,span in enumerate(spans)}; bends=[]
        for call in related:
            geometry=call['definition']['processInput']['geometry']; incoming=geometry['anchor']['incomingSegment']
            if incoming not in by_segment or by_segment[incoming]>=len(spans)-1:
                raise ValueError('折弯必须引用母材路径中的有效相邻段: '+call['key'])
            index=by_segment[incoming]
            a,b=spans[index:index+2]
            anchor=geometry['anchor']
            if (anchor.get('outgoingSegment')!=b.key or a.to_vertex!=b.from_vertex
                    or anchor.get('vertexKey')!=a.to_vertex):
                raise ValueError('折弯定位必须匹配母材路径的相邻段和共同顶点: '+call['key'])
            actual_angle=math.degrees(math.acos(max(-1.,min(1.,dot(a.direction,b.direction)))))
            declared_angle=geometry.get('angle')
            if (isinstance(declared_angle,bool) or not isinstance(declared_angle,(int,float))
                    or not math.isfinite(declared_angle) or declared_angle<=0
                    or abs(declared_angle-actual_angle)>1.e-6):
                raise ValueError('声明折弯角度与成品路径转角不一致: '+call['key'])
            if 'axisDirection' in geometry:
                axis=geometry['axisDirection']
                if (not isinstance(axis,list) or len(axis)!=3
                        or any(isinstance(value,bool) or not isinstance(value,(int,float)) or not math.isfinite(value) for value in axis)
                        or math.dist(unit(axis),unit(cross(a.direction,b.direction)))>1.e-6):
                    raise ValueError('声明折弯轴方向与成品路径转角不一致: '+call['key'])
            turn_y,turn_z=dot(b.direction,a.y_axis),dot(b.direction,a.z_axis)
            rotation=math.degrees(math.atan2(-turn_y,turn_z))
            if abs(rotation/90-round(rotation/90))>1.e-6:
                raise ValueError('连续外框折角必须落在管材基准面上')
            profile=profiles[key]
            # Provisional geometry is only a sizing input. The real hinge and
            # compensation below always replace these provisional reserves.
            reserve=abs(turn_y)*profile['depth']/2+abs(turn_z)*profile['width']/2-profile['wallThickness']
            a.end_reserve=b.start_reserve=reserve
            bends.append({'index':index,'sequence':index+1,'angle':geometry['angle'],'rotation':rotation,'call':call})
        bends.sort(key=lambda value:value['index'])
        if [bend['index'] for bend in bends]!=list(range(len(spans)-1)):
            raise ValueError('连续母材每个转角必须有且只有一个折弯工艺: '+key)
        length=allocated_spans(spans,bends,0.)
        def resolve(current_length):
            stock=_part(key,item['displayName'],[0,0,0],[current_length,0,0],{'xAxis':[0,1,0],'yAxis':[0,0,1]},profiles[key])
            instances=[]
            for bend in bends:
                definition=bend['call']['definition']; theta=math.radians(bend['rotation']); station=bend['station']
                local={'schema':'icax.assembly-process-input','schemaVersion':1,'parts':{'stock':deepcopy(stock)},
                    'geometry':{'angle':bend['angle'],'station':station,'rotation':bend['rotation'],'sequence':bend['sequence'],
                                'frame':{'origin':[station,0,0],'xAxis':[1,0,0],
                                         'yAxis':[0,math.cos(theta),math.sin(theta)],'zAxis':[0,-math.sin(theta),math.cos(theta)]}},
                    'resources':deepcopy(definition['processInput'].get('resources',{}))}
                instances.append({'instanceId':bend['call']['key'],'templateId':definition['templateId'],'processInput':local,
                    'parameters':deepcopy(definition['parameters']),'processDrafts':deepcopy(definition['processDrafts']),'targets':{'stock':key}})
            instances.sort(key=lambda instance:instance['processInput']['geometry']['sequence'])
            plan=runtime.resolve_process_plan([_stock_input(stock)],instances,runtime_context={'userMouldRoot':context.get('userMouldRoot','')})
            return stock,plan,instances
        stock,sizing,_=resolve(length)
        allowances=adapter.bend_allowances(sizing)
        by_instance={fold['instanceId']:fold for fold in sizing['forming']}
        for bend in bends:
            reserves=adapter.corner_reserves(by_instance[bend['call']['key']])
            spans[bend['index']].end_reserve=reserves['incoming']
            spans[bend['index']+1].start_reserve=reserves['outgoing']
            bend['lengthAddition']=allowances[bend['call']['key']]
            bend['cornerReserves']=reserves
        amounts=[bend['lengthAddition'] for bend in bends]
        length=allocated_spans(spans,bends,amounts)
        stock,plan,instances=resolve(length)
        # Preserve the established target-centreline check and original mould geometry.
        target_check=_load('security_window_frame_paths')._target_span_check(SimpleNamespace(spans=spans),plan,amounts)
        plan['targetSpanCheck']=target_check
        if key=='outer_frame.spatial.continuous' and item['manufacturingInput']['path']['closed']:
            # Use the resolved tool recipe, including saved library moulds.
            # V-root rounding remains local cutter relief and keeps this hinge.
            # Distributed-root moulds have a different local deformation model
            # and are outside this rigid motion check.
            features=[operation['requestFeature'] for operation in plan['operations'] if 'requestFeature' in operation]
            if len(features)==len(bends) and all(feature['toolParameters'].get('bottomStrategy') in {'sharp', 'rounded'}
                                               for feature in features):
                ranges={check['instanceId']:check['interval'] for check in plan['checks']
                        if check.get('id')=='material-range' and check.get('status')=='pass'}
                intervals=[ranges[bend['call']['key']] for bend in bends]
                rigid=[{'segmentKey':span.key,'interval':[
                    intervals[index-1][1] if index else 0.,
                    intervals[index][0] if index<len(intervals) else length]}
                    for index,span in enumerate(spans)]
                chosen=_load('security_window_forming_sequence').choose_forming_order(
                    rigid,plan['forming'],profiles[key],True)
                rank={identity:index+1 for index,identity in enumerate(chosen['order'])}
                for bend in bends: bend['sequence']=rank[bend['call']['key']]
                # Re-resolve rather than editing a hashed plan after validation.
                stock,plan,instances=resolve(length)
                plan['targetSpanCheck']=target_check
                plan['formingMotionCheck']=chosen['motionCheck']
                plan['formingOrder']=chosen['order']
        material_bends[key]=[{'instanceId':bend['call']['key'],'station':bend['station'],
                             'lengthAddition':bend['lengthAddition'],
                             'cornerReserves':deepcopy(bend['cornerReserves'])} for bend in bends]
        parts[key]=stock; fold_plans[key]=plan
        fold_instances.extend(instances)
        fold_keys.update(call['key'] for call in related)
    if fold_keys!={call['key'] for call in stage_calls['bend-unfolding']}:
        raise ValueError('折弯展开阶段需要明确的母材目标和路径转角')
    # End allocations are real downstream function evaluations, performed
    # together before apertures get any unfolded observer coordinates.
    end_calls=[call for call in calls if call['definition']['templateId'] in ('tube-end-joint', 'tube-post-end') and call not in boundary_calls]
    end_positions={}; plane_results={}; post_end_trims={}; miter_planes={}
    for call in end_calls:
        definition=call['definition']; local=definition['processInput']; stock_key=local['parts']['stock']['itemKey']
        stock=parts[stock_key]; mate=sources[local['parts']['mate']['itemKey']]['part']
        geometry=local['geometry']; end=geometry['anchor']['end']; mode=geometry.get('mode', 'insert')
        axis=[stock['matrix'][4*i] for i in range(3)]; origin=[stock['matrix'][4*i+3] for i in range(3)]
        point=geometry['anchor']['point']
        g={'mode':mode,'end':end,'station':dot(sub(point,origin),axis)}
        if mode=='miter':
            observation=sources[local['parts']['mate']['itemKey']]
            far=max((observation['start'],observation['end']),key=lambda value:math.dist(value,point))
            g['mateDirection']=unit(sub(far,point)); roles={'stock':deepcopy(stock)}
        else:
            g['station']=dot(sub([mate['matrix'][4*i+3] for i in range(3)],origin),axis)
            roles={'stock':deepcopy(stock),'mate':deepcopy(mate)}
        if definition['templateId'] == 'tube-post-end':
            g.pop('mode')
        result=geometry_runtime.evaluate(definition['templateId'],{'schema':'icax.assembly-process-input','schemaVersion':1,'parts':roles,'geometry':g},definition['parameters'])
        if not result['applicable']: raise ValueError(call['key']+': '+result['reason'])
        check=next(check for check in result['checks'] if check['kind']=='end-plane')
        if mode == 'miter':
            miter_planes.setdefault(stock_key, []).append({
                'worldOrigin': _point(stock['matrix'], check['origin']),
                'worldInward': [sum(stock['matrix'][4*i+j] * check['inwardNormal'][j] for j in range(3)) for i in range(3)]})
        station=check['allocation']['rawStockStation']
        if definition['templateId'] == 'tube-post-end':
            # The two actual end tools consume one unchanged initial post.
            # Its part-list extent is a result, not a pre-trimmed raw input.
            post_end_trims.setdefault(stock_key, {})[end] = station if end == 'start' else stock['length'] - station
        else:
            end_positions.setdefault(stock_key,{})[end]=_point(stock['matrix'],[station,0,0])
        plane_results[call['key']]=(g,roles)
    for key,ends in end_positions.items():
        previous=parts[key]; frame={'xAxis':[previous['matrix'][4*i+1] for i in range(3)]}
        start=ends.get('start',_point(previous['matrix'],[0,0,0])); end=ends.get('end',_point(previous['matrix'],[previous['length'],0,0]))
        parts[key]=_part(key,items[key]['displayName'],start,end,frame,profiles[key])
        if key in sources: sources[key]['part']=deepcopy(parts[key])
    model=NeutralModel(template_id='icax.manufacturing-allocation',template_version='1.0.0',package_digest='',parameters={})
    shared=_load('shared_tube_geometry').SharedTubeGeometry(model)
    for key,item in items.items():
        stock=parts[key]; matrix=stock['matrix']; length=stock['length']
        profile=deepcopy(stock['section']['profile']); origin=[matrix[4*i+3] for i in range(3)]
        axis=[matrix[4*i] for i in range(3)]; across=[matrix[4*i+1] for i in range(3)]; normal=[matrix[4*i+2] for i in range(3)]
        root=shared.emit_tube(key,profile_arguments={'placement':{'origin':origin,'xAxis':across,'yAxis':normal},'contours':profile['contours']},
                              extrude_arguments={'vector':[length*value for value in axis]})
        props=deepcopy(item['properties']); props.pop('tubeDesigner.sourceMemberProfiles',None)
        props['length']=length; props['tubeDesigner.profile']=profile
        spans=paths[key]
        if key not in fold_plans:
            spans[0].stock_start=dot(sub(spans[0].start,origin),axis)
        actual=[]
        mapping=next(mapping for mapping in document['sourceMappings'] if mapping['itemKey']==key)
        by_span={span.key:span for span in spans}
        for source in mapping['sources']:
            values=[]
            for entry in source['segments']:
                span=by_span[entry['segmentKey']]
                direction=sub(span.design_end,span.design_start); squared=dot(direction,direction)
                original_range=entry['sourceRange']
                covered=[span.start,span.end]
                if key not in fold_plans:
                    # Ordinary end allocation can shorten the raw material
                    # inside its display centreline. Map their real intersection.
                    stations=[dot(sub(point,origin),axis) for point in covered]
                    low,high=max(0.,stations[0]),min(length,stations[1])
                    if high-low<=1.e-8:
                        raise ValueError('下料直管没有覆盖来源成品区间: '+source['itemKey'])
                    covered=[_point(matrix,[station,0,0]) for station in (low,high)]
                interval=[original_range[0]+dot(sub(point,span.design_start),direction)/squared*(original_range[1]-original_range[0])
                          for point in covered]
                # Full-height initial material can extend beyond the displayed
                # member. Preserve only the covered design interval as its source.
                interval=[min(1.,max(0.,value)) for value in interval]
                mapped=[ [span.design_start[i]+direction[i]*(value-original_range[0])/(original_range[1]-original_range[0])
                           for i in range(3)] for value in interval]
                stock_start=(span.stock_start+dot(sub(mapped[0],span.start),span.direction) if key in fold_plans
                             else max(0.,dot(sub(mapped[0],origin),axis)))
                values.append({'itemKey':source['itemKey'],'start':mapped[0],'end':mapped[1],
                    'stockStart':stock_start,
                    'startReserve':span.start_reserve,'endReserve':span.end_reserve,
                    'placement':_placement(span),'sourceRange':interval})
            actual.append({'itemKey':source['itemKey'],'spans':values})
        props['manufacturing.sourceMembers']=actual; mapping_spans[key]=actual
        model.item(key,item['displayName'],representations={'result':root},properties=props)
    model.output('result','result',list(items))
    pending=[]; post_trims=deepcopy(post_end_trims)
    for call in calls:
        if call['key'] in fold_keys: continue
        copied=deepcopy(call); definition=copied['definition']; local=definition['processInput']; template=definition['templateId']
        roles={}
        for role,reference in local['parts'].items():
            roles[role]=deepcopy(parts[reference['itemKey']] if reference['scope']=='manufacturing' else sources[reference['itemKey']]['part'])
        geometry=deepcopy(local['geometry'])
        if template == 'tube-post-aperture':
            stock_key = definition['targets']['stock']
            matrix = parts[stock_key]['matrix']
            origin = [matrix[4*i+3] for i in range(3)]
            geometry['keepPlanes'] = [{
                'origin': [sum((plane['worldOrigin'][i] - origin[i]) * matrix[4*i+j] for i in range(3)) for j in range(3)],
                'inwardNormal': [sum(plane['worldInward'][i] * matrix[4*i+j] for i in range(3)) for j in range(3)]}
                for plane in miter_planes.get(stock_key, [])]
        if call in boundary_calls:
            if geometry.get('extentPolicy')=='nominal-member-section':
                # This factory allocation has already been applied. Replaying
                # a real mate-boundary cut would undo its explicit policy.
                continue
            original=local['parts']['stock']['data']['sourceMember']; target=definition['targets']['stock']
            if target in fold_plans:
                entry=next(entry for entry in mapping_spans[target] if entry['itemKey']==original)['spans'][0]
                pose=entry['placement']; axes=[pose[name] for name in ('xAxis','yAxis','zAxis')]
                mate=roles['mate']; old=mate['matrix']
                columns=[[old[4*i+j] for i in range(3)] for j in range(3)]
                translation=[pose['origin'][i]+sum(axes[j][i]*old[4*j+3] for j in range(3)) for i in range(3)]
                columns=[[sum(axes[k][i]*column[k] for k in range(3)) for i in range(3)] for column in columns]
                mate['matrix']=_matrix(translation,*columns)
            stock=roles['stock']; mate=roles['mate']
            origin=[stock['matrix'][4*i+3] for i in range(3)]; axis=[stock['matrix'][4*i] for i in range(3)]
            definition['processInput']={'schema':'icax.assembly-process-input','schemaVersion':1,'parts':roles,
                'geometry':{'mode':geometry['mode'],'end':geometry['anchor']['end'],
                            'station':dot(sub([mate['matrix'][4*i+3] for i in range(3)],origin),axis)}}
            pending.append(copied)
            continue
        if template=='orthogonal-corner':
            point=geometry.pop('designNode'); targets={}
            for role,reference in local['parts'].items():
                owner=reference['itemKey'] if reference['scope']=='manufacturing' else owners[reference['itemKey']]
                if role!='memberC':
                    original=reference.get('data',{}).get('sourceMember',reference['itemKey'])
                    if owners.get(original)!=owner:
                        raise ValueError('三向节点角色来源不属于其制造路径: '+role)
                    entry=min(next(entry for entry in mapping_spans[owner] if entry['itemKey']==original)['spans'],
                              key=lambda value:min(math.dist(point,value['start']),math.dist(point,value['end'])))
                    span=next(span for span in paths[owner] if math.dist(span.start,entry['start'])<1.e-7 and math.dist(span.end,entry['end'])<1.e-7)
                    roles[role]=_part(original,original,span.start,span.end,{'xAxis':span.y_axis},profiles[owner])
                    roles[role]['profileRef']=deepcopy(parts[owner]['profileRef'])
                    targets[role]={'stockId':owner,'start':span.stock_start+span.start_reserve,'reverse':False}
                    endpoint='start' if math.dist(point,span.start)<math.dist(point,span.end) else 'end'
                else:
                    targets[role]=owner
                    nominal=sources[reference['itemKey']]
                    endpoint='start' if math.dist(point,nominal['start'])<math.dist(point,nominal['end']) else 'end'
                roles[role]['anchor']={'kind':'end','end':endpoint,'trim':0,'rotation':0}
            definition['targets']=targets
            definition['processInput']={'schema':'icax.assembly-process-input','schemaVersion':1,'parts':roles,'geometry':geometry}
            # Keep the complete initial stock and native recipe untouched.
            # The established corner geometry supplies its finished C extent
            # for the part list; each real endpoint consumes one allowance.
            sizes=_post_geometry().process_sizes(definition['processInput'],definition['parameters'])
            post_key=targets['memberC']
            post_trims.setdefault(post_key,{})[roles['memberC']['anchor']['end']]=sizes['near']-sizes['insertion']
            pending.append(copied)
            continue
        target=definition['targets']['stock']
        if template in ('tube-end-joint', 'tube-post-end'):
            geometry,previous_roles=plane_results[call['key']]
            previous=previous_roles['stock']; current=roles['stock']; old_origin=[previous['matrix'][4*i+3] for i in range(3)]
            new_origin=[current['matrix'][4*i+3] for i in range(3)]; axis=[current['matrix'][4*i] for i in range(3)]
            geometry=deepcopy(geometry); geometry['station']-=dot(sub(new_origin,old_origin),axis)
            if geometry.get('mode')=='miter': roles.pop('mate',None)
        elif template in ('tube-profile-aperture','tube-post-aperture') and 'receiverSource' in geometry:
            receiver=geometry.pop('receiverSource')
            if target in fold_plans:
                entries=next(entry for entry in mapping_spans[target] if entry['itemKey']==receiver)['spans']
                path=items[target]['manufacturingInput']['path']
                first,last=paths[target][0],paths[target][-1]
                source_segments={value['segmentKey'] for value in sources[receiver]['segments']}
                # Only a real straight closure can share a socket across the
                # two raw-stock ends. An ordinary open or mitered end cannot
                # acquire this permission merely by having a clip interval.
                seam_receiver=(template=='tube-post-aperture' and path['closed']
                    and path.get('closure')=='straight-mid-edge-seam'
                    and {first.key,last.key}<=source_segments
                    and math.dist(first.start,last.end)<1.e-7
                    and all(math.dist(getattr(first,name),getattr(last,name))<1.e-7
                            for name in ('direction','y_axis','z_axis'))
                    and not geometry['sharedCorner'] and not geometry['keepPlanes'])
                observations=[]
                # One seam-split receiving member needs one clipped observation per interval.
                for index,entry in enumerate(entries):
                    instance=deepcopy(copied); detail=instance['definition']; branch=deepcopy(roles['branch']); pose=entry['placement']
                    axes=[pose[name] for name in ('xAxis','yAxis','zAxis')]
                    old=branch['matrix']; old_columns=[[old[4*i+j] for i in range(3)] for j in range(3)]
                    old_translation=[old[4*i+3] for i in range(3)]
                    translation=[pose['origin'][i]+sum(axes[j][i]*old_translation[j] for j in range(3)) for i in range(3)]
                    columns=[[sum(axes[k][i]*column[k] for k in range(3)) for i in range(3)] for column in old_columns]
                    branch['matrix']=_matrix(translation,*columns)
                    interval=[entry['stockStart'],entry['stockStart']+entry['startReserve']+math.dist(entry['start'],entry['end'])+entry['endReserve']]
                    # Seam splitting creates candidate observer placements.
                    # Keep only intervals touched by the actual outer section.
                    profile=branch['section']['profile']
                    support=_load('assembly_stock_allowance')._support_interval({
                        'placement':{'origin':translation,'xAxis':columns[1],'yAxis':columns[2]},
                        'contours':profile['contours']},[1.,0.,0.],[0.,0.,0.])
                    clearance=float(detail['parameters'].get('clearance',0.))
                    if support[1]+clearance<=interval[0]+1.e-7 or support[0]-clearance>=interval[1]-1.e-7:
                        continue
                    observed={'clipInterval':interval}
                    if template=='tube-post-aperture':
                        observed.update({name:deepcopy(geometry[name]) for name in ('branchEnd','sharedCorner','keepPlanes')})
                        observed['node']=[pose['origin'][i]+sum(axes[j][i]*geometry['node'][j] for j in range(3)) for i in range(3)]
                    detail['processInput']={'schema':'icax.assembly-process-input','schemaVersion':1,
                        'parts':{'stock':deepcopy(roles['stock']),'branch':branch},
                        'geometry':observed}
                    if len(entries)>1: instance['key']+='.'+str(index)
                    observations.append(instance)
                if seam_receiver:
                    length=roles['stock']['length']
                    start_calls=[value for value in observations if abs(
                        value['definition']['processInput']['geometry']['clipInterval'][0])<1.e-7]
                    end_calls=[value for value in observations if abs(
                        value['definition']['processInput']['geometry']['clipInterval'][1]-length)<1.e-7]
                    # Both actual observations must touch their allocated
                    # intervals; the support check above discards misses.
                    if len(start_calls)==len(end_calls)==1 and start_calls[0] is not end_calls[0]:
                        start_calls[0]['definition']['processInput']['geometry']['endOpening']='start'
                        end_calls[0]['definition']['processInput']['geometry']['endOpening']='end'
                pending.extend(observations)
                continue
        definition['processInput']={'schema':'icax.assembly-process-input','schemaVersion':1,'parts':roles,'geometry':geometry}
        pending.append(copied)
    neutral=model.build(); neutral.setdefault('extensions',{}).update({'tubeDesigner.geometryPurpose':'manufacturing',
        'tubeDesigner.productStockMappingVersion':1,'tubeDesigner.manufacturingPartCount':len(items)})
    instances=deepcopy(fold_instances)
    for process in pending:
        definition=process['definition']
        instances.append({'instanceId':process['key'],'templateId':definition['templateId'],
            'processInput':deepcopy(definition['processInput']),'parameters':deepcopy(definition['parameters']),
            'processDrafts':deepcopy(definition['processDrafts']),'targets':deepcopy(definition['targets'])})
    source={'schema':'icax.assembly-process-source','schemaVersion':1,'stocks':[_stock_input(part) for part in parts.values()],'instances':instances}
    # Freeze a common material map before downstream cutters consume it.
    # Length requirements and source mappings are outputs, never parameter edits.
    material_plan={'schema':'icax.assembly-material-plan','schemaVersion':1,'stocks':[],
                   'initialMaterialAllocations':deepcopy(initial_material_allocations)}
    for key,part in parts.items():
        bends=deepcopy(material_bends.get(key,[]))
        material_plan['stocks'].append({'stockId':key,'length':part['length'],'matrix':deepcopy(part['matrix']),
            'lengthBeforeBendCompensation':part['length']-sum(bend['lengthAddition'] for bend in bends),
            'bends':bends,'sourceMembers':deepcopy(mapping_spans[key])})
        if key in fold_plans:
            material_plan['stocks'][-1]['targetSpanCheck']=deepcopy(fold_plans[key]['targetSpanCheck'])
        if key in fold_plans and 'formingMotionCheck' in fold_plans[key]:
            material_plan['stocks'][-1].update(formingOrder=deepcopy(fold_plans[key]['formingOrder']),
                formingMotionCheck=deepcopy(fold_plans[key]['formingMotionCheck']))
    stage_plan['materializedProcessKeys']=[instance['instanceId'] for instance in instances]
    neutral['extensions']['tubeDesigner.assemblyStagePlan']=deepcopy(stage_plan)
    neutral['extensions']['tubeDesigner.assemblyMaterialPlan']=deepcopy(material_plan)
    for item in neutral['items']:
        key=item['key']; props=item['properties']
        props['tubeDesigner.assemblyProcessStockId']=key
        props['tubeDesigner.assemblyProcessStockLength']=parts[key]['length']
        props['tubeDesigner.assemblyProcessStockProfile']=deepcopy(parts[key]['section']['profile'])
        props['tubeDesigner.assemblyProcessSecondaryCutters']=[]
        if key in post_trims:
            props['length']=parts[key]['length']-sum(post_trims[key].values())
            if props['length']<=0:
                raise ValueError('三向节点加工后零件没有有效轴长: '+key)
    return {'model':neutral,'processes':pending,'allocationPlans':fold_plans,'assemblySource':source,
            'initialMaterialAllocations':initial_material_allocations,'stagePlan':stage_plan,'materialPlan':material_plan}
