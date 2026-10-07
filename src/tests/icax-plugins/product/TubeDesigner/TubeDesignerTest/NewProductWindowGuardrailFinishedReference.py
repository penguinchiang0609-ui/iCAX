"""Compile finished geometry from current source stocks, functions and tools.

This oracle never takes a Native execution document, request or BRep as input.
The returned graph is evaluated independently by the common geometry kernel.
"""
from copy import deepcopy
import json
import math

import assembly_template_runtime as assembly
import assembly_geometry_process_runtime as machining
import assembly_manufacturing_executor as executor
from icax_template_sdk.resources import to_resource_model


def canonical(value):
    return json.dumps(value, sort_keys=True, ensure_ascii=False, allow_nan=False,
                      separators=(',', ':'))


def profile_input(part):
    return {'profileRef': deepcopy(part['profileRef']),
            'parameters': deepcopy(part.get('parameters', {}))}


def compile_plan(document):
    source = document['extensions']['tubeDesigner.assemblyProcessSource']
    assert source['schema'] == 'icax.assembly-process-source' and source['schemaVersion'] == 1
    items = {item['properties']['tubeDesigner.assemblyProcessStockId']: item
             for item in document['items']}
    profiles, inputs = {}, {}
    for stock in source['stocks']:
        item = items[stock['id']]
        profile = item['properties'].get('tubeDesigner.assemblyProcessStockProfile',
                                         item['properties'].get('tubeDesigner.profile'))
        assert isinstance(profile, dict)
        key = canonical(profile_input(stock))
        assert key not in profiles or profiles[key] == profile
        profiles[key] = deepcopy(profile); inputs[key] = profile_input(stock)
    instances = deepcopy(source['instances'])
    for instance in instances:
        for part in instance['processInput']['parts'].values():
            key = canonical(profile_input(part)); inputs[key] = profile_input(part)
            profile = part.get('section', {}).get('profile')
            if profile is not None:
                if key in profiles:
                    assert profiles[key]['contours'] == profile['contours']
                    for field in ('width', 'depth', 'wallThickness'):
                        assert math.isfinite(profile[field]) and abs(profiles[key][field]-profile[field]) <= 1e-6
                else: profiles[key] = deepcopy(profile)
            assert key in profiles, 'Source observer requires its actual section snapshot'
    dependencies = [{'input': inputs[key], 'profile': profiles[key],
                     'authority': 'product-neutral-section'} for key in sorted(inputs)]
    for instance in instances:
        for part in instance['processInput']['parts'].values():
            part['section'] = {'profile': deepcopy(profiles[canonical(profile_input(part))])}
    plan = assembly.resolve_process_plan(deepcopy(source['stocks']), instances, dependencies)
    assert {part['stockId'] for part in plan['manufacturingParts']} == set(items)
    assert len(plan['manufacturingParts']) == len(items)
    return plan, dependencies


def source_resource_cutter(model, operation, stock, profile, dependencies, prefix):
    """Evaluate a current recipe without Native tool snapshots or shape bounds."""
    punch = machining._resource_runtime('punch_tool_runtime')
    sections = machining._resource_runtime('section_geometry')
    outside = sections.bounds(next(contour for contour in sections.from_profile(profile)['contours']
                                  if not contour['inner']))
    bounds = {'min': [0., *outside['min']], 'max': [stock['length'], *outside['max']]}
    profiles = {canonical(profile_input(entry['input'])): entry['profile'] for entry in dependencies}
    def with_section(recipe):
        result = deepcopy(recipe)
        if 'section' in result:
            key = canonical(profile_input(result['section']))
            assert key in profiles, 'Recipe section must come from the independent Source dependency set'
            result['section'] = {**result['section'], 'profile': deepcopy(profiles[key])}
        return result
    if 'requestEnd' in operation:
        end = with_section(operation['requestEnd'])
        assert end.get('toolRef', {}).get('id') in ('end-miter', 'paired-end-tabs'), 'Unsupported source end recipe'
        assert end.get('rotation', 0) == 0 and end.get('datum', 'long') == 'long'
        context = {'target': 'end', 'end': operation['end'], 'targetSection': deepcopy(profile),
                   'bounds': bounds, 'lengthUnit': 'mm',
                   'placement': {key: end.get(key, default) for key, default in
                                 (('trim', 0.), ('rotation', 0.), ('datum', 'long'))}}
        if 'section' in end: context['section'] = deepcopy(end['section'])
        recipe = end
    else:
        recipe = with_section(operation['requestFeature'])
        assert recipe.get('toolRef', {}).get('id') in ('branch-profile', 'paired-side-slots'), 'Unsupported source feature recipe'
        assert recipe.get('enabled', True) is True and recipe.get('toolTarget') == 'part'
        assert recipe['reference'] == 'start' and recipe.get('face', 'top') in ('top', 'bottom', 'left', 'right')
        assert recipe.get('arrayCount', 1) == recipe.get('rowCount', 1) == 1
        assert recipe.get('arrayPitch', 0) == recipe.get('rowPitch', 0) == recipe.get('offset', 0) == 0
        assert not recipe.get('opposite', False)
        assert all(recipe.get(field) in (None, [], {}) for field in
                   ('skip', 'groups', 'transforms', 'offsets', 'arrayOffsets', 'rowOffsets'))
        placement_fields = ('rotation', *punch.BRANCH_PLACEMENT_KEYS)
        context = {'target': 'part', 'targetSection': deepcopy(profile), 'bounds': bounds,
                   'lengthUnit': 'mm', 'placement': {key: deepcopy(recipe[key]) for key in placement_fields if key in recipe},
                   'feature': {key: deepcopy(recipe[key]) for key in ('station', 'reference', 'section', 'face', 'offset') if key in recipe}}
    snapshot = punch._evaluate(recipe['toolRef'], recipe['toolParameters'], context)
    geometry = snapshot['geometry']
    assert geometry['mode'] == 'solid'
    assert snapshot['ref']['id'] == recipe['toolRef']['id']
    assert snapshot['ref']['version'] == recipe['toolRef']['version']
    nodes = geometry['model']['geometry']
    mapping = {node['key']: prefix + '.' + node['key'] for node in nodes}
    assert geometry['outputKey'] in mapping
    for node in nodes:
        model.geometry(mapping[node['key']], node['operator'],
                       inputs=[mapping.get(key, key) for key in node.get('inputs', [])],
                       arguments=machining._rewrite(node.get('arguments', {}), mapping))
    cutter = mapping[geometry['outputKey']]
    space = geometry['coordinateSpace']
    lo, hi = bounds['min'], bounds['max']
    center = [(a+b)/2 for a, b in zip(lo, hi)]
    if space == 'end-local':
        assert 'requestEnd' in operation
        sign = 1. if operation['end'] == 'start' else -1.
        origin = [(lo[0] if sign == 1 else hi[0]) + sign * recipe.get('trim', 0), center[1], center[2]]
        placement = {'origin': origin, 'xAxis': [sign, 0., 0.],
                     'yAxis': [0., 1., 0.], 'zAxis': [0., 0., sign]}
        cutter = model.geometry(prefix + '.local-pose', 'transform', inputs=[cutter], arguments={'placement': placement})
    elif space == 'part-local':
        assert 'requestFeature' in operation
        angle = math.radians(recipe.get('rotation', 0))
        placement = {'origin': [recipe['station'], center[1], center[2]], 'xAxis': [1., 0., 0.],
                     'yAxis': [0., math.cos(angle), math.sin(angle)],
                     'zAxis': [0., -math.sin(angle), math.cos(angle)]}
        cutter = model.geometry(prefix + '.local-pose', 'transform', inputs=[cutter], arguments={'placement': placement})
    else:
        assert space == 'part', 'Unsupported source cutter coordinate space'
    return model.geometry(prefix + '.world-pose', 'transform', inputs=[cutter],
                          arguments={'placement': machining._placement(stock['matrix'])})


def folded_post_plan_proof(document, plan):
    values = document['parameters']
    if not (values.get('faceType') == 'three' and values.get('frameManufacturingMode') == 'plane_v_notch'
            and values.get('assemblyPlanningMode') == 'builtin_rules'
            and values.get('foldedPostJoint') in ('insert', 'tabs')):
        return None
    joint = values['foldedPostJoint']
    corners = [instance for instance in plan['instances'] if instance['templateId'] == 'orthogonal-corner']
    assert len(corners) == 4
    receivers = {'outer_frame.plane.top', 'outer_frame.plane.bottom'}
    expected_posts = {'outer_frame.vertical.0002', 'outer_frame.vertical.0003'}
    facts = []
    for corner in corners:
        assert corner['parameters']['abJoint'] == 'continuous' and corner['parameters']['cJoint'] == joint
        operations = [operation for operation in plan['operations'] if operation['instanceId'] == corner['instanceId']]
        receiving = [operation for operation in operations if 'requestFeature' in operation]
        ends = [operation for operation in operations if 'requestEnd' in operation]
        assert len(receiving) == 2 and len(ends) == 1
        assert all(operation['stockId'] in receivers for operation in receiving)
        assert ends[0]['stockId'] in expected_posts
        expected_feature = 'branch-profile' if joint == 'insert' else 'paired-side-slots'
        expected_end = 'end-miter' if joint == 'insert' else 'paired-end-tabs'
        assert all(operation['requestFeature']['toolRef']['id'] == expected_feature for operation in receiving)
        assert ends[0]['requestEnd']['toolRef']['id'] == expected_end
        facts.append({'instanceId': corner['instanceId'], 'postStockId': ends[0]['stockId'],
                      'end': ends[0]['end'], 'receivingStockIds': sorted({entry['stockId'] for entry in receiving}),
                      'featureToolId': expected_feature, 'endToolId': expected_end})
    assert {(fact['postStockId'], fact['end']) for fact in facts} == {
        (post, end) for post in expected_posts for end in ('start', 'end')}
    return {'joint': joint, 'cornerCount': 4, 'receiverFeatureCount': 8, 'postEndCutCount': 4,
            'sourceOnlyAuthority': True, 'corners': facts}


def finished_reference(document):
    if 'tubeDesigner.assemblyProcessSource' not in document.get('extensions', {}):
        return {'finishedDocument': deepcopy(document), 'sourceAssemblyPlan': None,
                'finishedReferenceProof': {'assemblySourcePresent': False,
                    'geometryAlreadyExecutedByIndependentSourceExecutor': True}}
    plan, dependencies = compile_plan(document)
    model = executor._builder(document)
    items = {item['properties']['tubeDesigner.assemblyProcessStockId']: item
             for item in model._document['items']}
    stocks = {stock['id']: stock for stock in document['extensions']['tubeDesigner.assemblyProcessSource']['stocks']}
    instances = {instance['instanceId']: instance for instance in plan['instances']}
    neutral_count = feature_count = end_count = 0
    for part in plan['manufacturingParts']:
        key = part['stockId']; item = items[key]; request = part['request']; stock = stocks[key]
        assert request['length'] == stock['length']
        assert not item['properties'].get('tubeDesigner.assemblyProcessSecondaryCutters'), 'Unsupported secondary cutter oracle'
        current = item['representations']['result']
        assert isinstance(current, str) and current in model._geometry_keys
        # Native evaluates resource-owned features before neutral operations.
        feature_operations = [operation for operation in plan['operations']
                              if operation['stockId'] == key and 'requestFeature' in operation]
        assert len(feature_operations) == len(request['features'])
        end_operations = [operation for operation in plan['operations']
                          if operation['stockId'] == key and 'requestEnd' in operation]
        assert len(end_operations) == sum(end.get('type', 'keep') != 'keep' for end in request['ends'].values())
        expected_operation_ids = [operation['operationId'] for operation in plan['operations'] if operation['stockId'] == key]
        represented_ids = {operation['operationId'] for operation in feature_operations}
        represented_ids.update(operation['operationId'] for operation in end_operations)
        represented_ids.update(entry['operationId'] for entry in request.get('neutralOperations', []))
        assert len(expected_operation_ids) == len(set(expected_operation_ids))
        assert represented_ids == set(expected_operation_ids) == set(part['operationIds'])
        profile = item['properties']['tubeDesigner.assemblyProcessStockProfile']
        if end_operations:
            cutters = []
            for index, operation in enumerate(end_operations):
                assert operation['requestEnd'] == request['ends'][operation['end']]
                cutters.append(source_resource_cutter(model, operation, stock, profile, dependencies,
                    'oracle.' + key + f'.end.{index}'))
            current = machining.apply_frozen_cutters(model, current, cutters,
                'oracle.' + key + '.end-cuts', key, {**deepcopy(plan), 'operations': end_operations})
            end_count += len(cutters)
        rich_feature_operations = []
        direct_cutters = []
        for feature_index, (operation, feature) in enumerate(zip(feature_operations, request['features'])):
            assert operation['requestFeature'] == feature and feature['id'] == operation['operationId']
            if 'cutter' not in operation or 'placement' not in operation:
                direct_cutters.append(source_resource_cutter(model, operation, stock, profile, dependencies,
                    'oracle.' + key + f'.direct-feature.{feature_index}'))
                continue
            rich_feature_operations.append(operation)
            allowed_fields = {'id','name','enabled','toolTarget','toolRef','toolParameters','reference','station',
                'face','rotation','offset','arrayCount','arrayPitch','rowCount','rowPitch','opposite','layoutDatum',
                'endDatum','skip','groups','transforms','offsets','arrayOffsets','rowOffsets'}
            assert not set(feature)-allowed_fields, 'Unsupported current source feature field'
            assert feature.get('enabled', True) is True and feature['toolTarget'] == 'part'
            assert feature['reference'] == 'start' and feature.get('face', 'top') == 'top'
            assert feature.get('offset', 0) == 0 and feature.get('opposite', False) is False
            assert feature.get('arrayCount', 1) == feature.get('rowCount', 1) == 1
            assert feature.get('arrayPitch', 0) == feature.get('rowPitch', 0) == 0
            assert feature.get('layoutDatum', '') in ('', 'base') and feature.get('endDatum', 'long') == 'long'
            assert all(feature.get(field) in (None, [], {}) for field in ('skip','groups','transforms','offsets','arrayOffsets','rowOffsets'))
            assert operation['cutter']['mode'] == 'solid' and operation['cutter']['coordinateSpace'] == 'part-local'
            frame = operation['placement']['frame']; angle = math.radians(feature['rotation'])
            expected_frame = {'origin': [feature['station'],0,0], 'xAxis': [1,0,0],
                'yAxis': [0,math.cos(angle),math.sin(angle)], 'zAxis': [0,-math.sin(angle),math.cos(angle)]}
            assert all(abs(a-b) <= 1e-7 for axis in expected_frame for a,b in zip(frame[axis],expected_frame[axis]))
        if direct_cutters:
            current = machining.apply_frozen_cutters(model, current, direct_cutters,
                'oracle.' + key + '.direct-feature-cuts', key, {**deepcopy(plan), 'operations': feature_operations})
            feature_count += len(direct_cutters)
        if rich_feature_operations:
            tool_plan = {**deepcopy(plan), 'operations': rich_feature_operations}
            punch = machining._resource_runtime('punch_tool_runtime')
            original_evaluate = punch._evaluate
            checked_snapshots = []
            def guarded_evaluate(*args, **kwargs):
                snapshot = original_evaluate(*args, **kwargs)
                geometry = snapshot['geometry']
                assert geometry['mode'] == 'solid' and geometry['coordinateSpace'] == 'part-local'
                checked_snapshots.append(geometry['outputKey'])
                return snapshot
            punch._evaluate = guarded_evaluate
            try:
                cutters = machining.materialize_resolved_tool_plan(model, tool_plan, profile,
                    request['length'], 'oracle.'+key+'.features')
            finally: punch._evaluate = original_evaluate
            assert len(checked_snapshots) == len(rich_feature_operations)
            assert len(cutters) == len(rich_feature_operations)
            world_cutters = [model.geometry('oracle.'+key+f'.feature-world.{index}', 'transform',
                inputs=[cutter], arguments={'placement': machining._placement(stock['matrix'])})
                for index, cutter in enumerate(cutters)]
            current = machining.apply_frozen_cutters(model, current, world_cutters,
                'oracle.'+key+'.feature-cuts', key, tool_plan)
            feature_count += len(cutters)
        for index, entry in enumerate(request.get('neutralOperations', [])):
            assert entry['operation']['stockId'] == key
            assert entry['operation']['kind'] == 'neutral-csg' and entry['operation']['role'] == 'stock'
            assert entry['geometrySpace'] in ('stock', 'document')
            instance = instances[entry['instanceId']]
            result = {'schema': machining.SCHEMA, 'schemaVersion': 1,
                'templateId': instance['templateId'], 'functionDigest': plan['planDigest'],
                'applicable': True, 'reason': '', 'parameters': deepcopy(instance['parameters']),
                'processInput': {'schema': 'icax.assembly-process-input', 'schemaVersion': 1,
                    'parts': {'stock': deepcopy(stock)}, 'geometry': {}},
                'geometry': deepcopy(entry['geometry']), 'geometrySpace': entry['geometrySpace'],
                'operations': [{name: deepcopy(value) for name, value in entry['operation'].items()
                    if name not in ('operationId', 'instanceId', 'stockId')}], 'materialRoles': ['stock'],
                'materialRequirements': [], 'forming': [], 'contactRequirements': [],
                'checks': [], 'assemblySteps': [], 'bom': []}
            current = machining.apply(model, current, result, 'oracle.'+key+f'.neutral.{index}', key)
            neutral_count += 1
        item['representations']['result'] = current
    finished = to_resource_model(model.build())
    assert finished['parameters'] == document['parameters']
    return {'finishedDocument': finished, 'sourceAssemblyPlan': plan,
            'sourceProfileDependencies': dependencies,
            'finishedReferenceProof': {'assemblySourcePresent': True,
                'inputAuthority': 'Independent current source stocks, profiles, assembly functions and selected mould packages',
                'actualNativeDocumentsOrBRepsUsed': False,
                'rawStockCount': len(items), 'neutralOperationsApplied': neutral_count,
                'resourceOwnedFeatureCuttersApplied': feature_count,
                'resourceOwnedEndCuttersApplied': end_count,
                'foldedPostConnections': folded_post_plan_proof(document, plan),
                'formingCount': len(plan['forming']),
                'formingTargetGeometryNotManufacturingStockGeometry': True}}
