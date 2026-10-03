"""Execute a product's manufacturing input independently of its Python template.

Only this downstream boundary evaluates machining providers, allocates material
and freezes tools. The input remains immutable and owns no computed results.
"""
from copy import deepcopy
import hashlib
import importlib.util
import math
from pathlib import Path
import sys

from icax_template_sdk import NeutralModel, expand_resource_model, to_resource_model
from icax_template_sdk.manufacturing import validate_manufacturing_model, initial_manufacturing_model, inverse_frame, IDENTITY_FRAME, compose_manufacturing_model
from icax_template_sdk.resources import _compose


def _load(name):
    path = Path(__file__).with_name(name + '.py')
    identity = 'icax_manufacturing_executor_' + name + '_' + hashlib.sha256(path.read_bytes()).hexdigest()[:16]
    if identity not in sys.modules:
        specification = importlib.util.spec_from_file_location(identity, path)
        module = importlib.util.module_from_spec(specification)
        sys.modules[identity] = module
        specification.loader.exec_module(module)
    return sys.modules[identity]


def _builder(document):
    model = NeutralModel(template_id='assembly-manufacturing', template_version='2', package_digest='', parameters={})
    model._document = deepcopy(expand_resource_model(document))
    model._geometry_keys = {node['key'] for node in model._document['geometry']}
    model._item_keys = {item['key'] for item in model._document['items']}
    model._relationship_keys = {item['key'] for item in model._document.get('relationships', [])}
    return model


def _merge_prepared_geometry(model, temporary):
    known = {node['key']: node for node in model._document['geometry']}
    for node in temporary._document['geometry']:
        if node['key'] in known:
            if known[node['key']] != node:
                raise ValueError('prepared workpiece conflicts with a shared geometry resource')
            continue
        model._document['geometry'].append(deepcopy(node))
        model._geometry_keys.add(node['key'])
        known[node['key']] = node


def _prepare_shared_stock(model, process_key, call, item, placement):
    """Prepare a workpiece from intrinsic part facts and local requirements."""
    stock = call['processInput']['parts']['stock'].get('data', {}) if call else {}
    temporary = NeutralModel(template_id='assembly-stock-preparation', template_version='1',
                             package_digest='', parameters={})
    shared = _load('shared_tube_geometry').SharedTubeGeometry(temporary)
    prefix = process_key + '.prepared-stock'
    if call is None or call['templateId'] == 'surface-feature-cut':
        plate = item.get('properties', {}).get('plate')
        if not isinstance(plate, dict) or not all(name in plate for name in ('center', 'xAxis', 'yAxis')):
            raise ValueError('surface machining requires the shared part plate dimensions and placement')
        relative = inverse_frame(placement)
        center = [relative['origin'][i] + sum(relative[name][i] * plate['center'][j]
                  for j, name in enumerate(('xAxis', 'yAxis', 'zAxis'))) for i in range(3)]
        axes = {axis: [sum(relative[name][i] * plate[axis][j]
                       for j, name in enumerate(('xAxis', 'yAxis', 'zAxis'))) for i in range(3)]
                for axis in ('xAxis', 'yAxis')}
        if 'outline' in plate:
            x, y = axes['xAxis'], axes['yAxis']
            normal = [x[1] * y[2] - x[2] * y[1], x[2] * y[0] - x[0] * y[2],
                      x[0] * y[1] - x[1] * y[0]]
            outline = plate['outline']
            result = shared.emit_tube(prefix, profile_arguments={
                'placement': {'origin': [center[i] - normal[i] * plate['thickness'] / 2 for i in range(3)],
                              'xAxis': x, 'yAxis': y},
                'contours': [{'kind': 'path', 'closed': True, 'segments': [
                    {'kind': 'line', 'start': deepcopy(point), 'end': deepcopy(outline[(index + 1) % len(outline)])}
                    for index, point in enumerate(outline)]}]},
                extrude_arguments={'vector': [coordinate * plate['thickness'] for coordinate in normal]})
        else:
            result = _load('plate_geometry').emit_rectangular_plate(
                temporary, prefix, width=plate['width'], height=plate['height'], thickness=plate['thickness'],
                center=center, x_axis=axes['xAxis'], y_axis=axes['yAxis'], shared_geometry=shared)
    elif call['templateId'] == 'structural-stock-fit' and isinstance(stock.get('section'), dict) \
            and all(name in stock['section'] for name in ('placement', 'contours', 'vector')):
        section = stock['section']
        result = shared.emit_tube(prefix, profile_arguments={
            'placement': deepcopy(section['placement']), 'contours': deepcopy(section['contours'])},
            extrude_arguments={'vector': deepcopy(section['vector'])})
    else:
        return None
    _merge_prepared_geometry(model, temporary)
    return result


def _prepare_profile_stock(model, process, item, connections, placement=IDENTITY_FRAME, processes=None):
    """Materialize the declared uncut member from its section and end anchors."""
    call = process['definition']
    inp = call['processInput']
    requirements = inp['geometry']
    interval = requirements.get('axialInterval')
    source_key = requirements.get('sourceProcessKey')
    parameters = call['parameters']
    if (set(parameters) - {'requestedPartNumber', 'reportingLengthPrecision'}
            or 'requestedPartNumber' in parameters and (not isinstance(parameters['requestedPartNumber'], str)
                                                       or not parameters['requestedPartNumber'])
            or 'reportingLengthPrecision' in parameters and (type(parameters['reportingLengthPrecision']) is not int
                                                             or not 0 <= parameters['reportingLengthPrecision'] <= 9)):
        raise ValueError('stock preparation numbering or reporting precision requirement is invalid')
    if (set(inp['parts']) != {'stock'} or inp.get('resources')
            or call.get('processDrafts') or inp['parts']['stock'].get('state') != 'initial'
            or set(inp['parts']['stock'].get('data', {})) - {'initialGeometry'}
            or (set(requirements) - {'connectionKeys', 'freeEnds', 'axialInterval', 'direction', 'sourceProcessKey', 'status', 'ready'})
            or requirements.get('status') not in ('await-external', 'prepared')
            or requirements.get('ready') is not (requirements['status'] == 'prepared')
            or sum(name in requirements for name in ('connectionKeys', 'axialInterval', 'sourceProcessKey')) != 1):
        raise ValueError('profile stock preparation requires explicit shared-member extent requirements')
    keys = requirements.get('connectionKeys', [])
    if (not isinstance(keys, list) or any(not isinstance(key, str) or not key for key in keys)
            or len(set(keys)) != len(keys)):
        raise ValueError('profile stock preparation requires distinct endpoint connections')
    relations = {connection['key']: connection for connection in connections}
    allowances, endpoints = {}, {}
    for key in keys:
        if key not in relations or item['key'] not in relations[key]['items']:
            raise ValueError('stock preparation connection does not belong to the shared member')
        for participant in relations[key].get('properties', {}).get('participantAnchors', []):
            if participant.get('itemKey') != item['key']:
                continue
            anchor = participant.get('anchor', {})
            if anchor.get('kind') != 'end':
                continue
            end, reserve = anchor.get('end'), anchor.get('stockAllowance')
            if (end not in ('start', 'end') or isinstance(reserve, bool)
                    or not isinstance(reserve, (int, float)) or not math.isfinite(reserve) or reserve < 0):
                raise ValueError('stock preparation requires a finite nonnegative end allowance')
            if end in allowances and abs(allowances[end] - reserve) > 1.e-8:
                raise ValueError('stock preparation endpoint declarations disagree')
            allowances[end] = reserve
            point = participant.get('centerlinePoint', anchor.get('point'))
            point = _load('shared_tube_geometry')._vector(point, 'preparation endpoint')
            if end in endpoints and math.dist(endpoints[end], point) > 1.e-8:
                raise ValueError('stock preparation endpoint points disagree')
            endpoints[end] = point
    properties = item['properties']
    member, profile = properties.get('assemblyFrame.member'), properties.get('tubeDesigner.profile')
    if not isinstance(member, dict) or not isinstance(profile, dict):
        raise ValueError('stock preparation requires shared member and profile components')
    frame = member.get('sectionFrame', {})
    if not all(name in frame for name in ('originAtStart', 'xAxis', 'yAxis')):
        raise ValueError('stock preparation requires the shared section datum')
    shared_module = _load('shared_tube_geometry')
    start = shared_module._vector(member.get('start'), 'shared member start')
    end = shared_module._vector(member.get('end'), 'shared member end')
    axis = shared_module._unit([end[i] - start[i] for i in range(3)], 'shared member axis')
    origin = shared_module._vector(frame['originAtStart'], 'shared section origin')
    section_axes = [shared_module._vector(frame[name], 'shared section ' + name) for name in ('xAxis', 'yAxis')]
    if (any(abs(math.hypot(*value)-1.0) > 1.e-7 for value in section_axes)
            or abs(sum(section_axes[0][i]*section_axes[1][i] for i in range(3))) > 1.e-7
            or any(abs(sum(value[i]*axis[i] for i in range(3))) > 1.e-7 for value in section_axes)):
        raise ValueError('stock preparation requires orthonormal transverse section axes')
    x, y = section_axes
    normal = shared_module._unit([x[1]*y[2]-x[2]*y[1], x[2]*y[0]-x[0]*y[2],
                                 x[0]*y[1]-x[1]*y[0]], 'shared section normal')
    if (abs(abs(sum(normal[i]*axis[i] for i in range(3)))-1.0) > 1.e-7
            or 'zAxis' in frame and math.dist(shared_module._vector(frame['zAxis'], 'shared section zAxis'), normal) > 1.e-7):
        raise ValueError('stock preparation requires a right-handed intrinsic section frame')
    if source_key is not None:
        if set(requirements) != {'sourceProcessKey', 'status', 'ready'}:
            raise ValueError('referenced preparation input contains competing extent requirements')
        referenced = next((entry['definition'] for entry in processes or [] if entry['key'] == source_key), None)
        if (referenced is None or referenced['targets'].get('stock') != item['key']
                or referenced['templateId'] == 'profile-stock-preparation'
                or process['key'] not in referenced['dependencies']):
            raise ValueError('preparation requires an explicitly dependent process for the same shared item')
        binding = referenced['processInput']['parts']['stock']
        if binding['state'] != {'after': process['key']}:
            raise ValueError('referenced machining must consume the prepared workpiece state')
        facts = binding.get('data', {})
        if 'start' in facts and 'end' in facts:
            requested_start, requested_end = facts['start'], facts['end']
        elif 'matrix' in facts and 'length' in facts:
            matrix = facts['matrix']
            length = facts['length']
            if (not isinstance(matrix, list) or len(matrix) != 16
                    or any(isinstance(value, bool) or not isinstance(value, (int, float))
                           or not math.isfinite(value) for value in matrix)
                    or isinstance(length, bool) or not isinstance(length, (int, float))
                    or not math.isfinite(length) or length <= 0):
                raise ValueError('referenced workpiece requires its actual stock frame')
            requested_start = [matrix[index] for index in (3, 7, 11)]
            requested_end = [requested_start[i] + matrix[4*i] * facts['length'] for i in range(3)]
        elif referenced['templateId'] == 'structural-male-head':
            geometry, source_parameters = referenced['processInput']['geometry'], referenced['parameters']
            requested_start = [geometry['visibleLeft'] - source_parameters['length'], 0.0, geometry['centerZ']]
            requested_end = [geometry['visibleRight'] + source_parameters['length'], 0.0, geometry['centerZ']]
        else:
            raise ValueError('referenced process does not declare physical workpiece extents')
        source_frame = facts.get('initialGeometry', {}).get('itemPlacement', IDENTITY_FRAME)
        points = []
        for value in (requested_start, requested_end):
            value = shared_module._vector(value, 'referenced workpiece endpoint')
            point = [source_frame['origin'][i] + sum(source_frame[name][i] * value[j]
                     for j, name in enumerate(('xAxis', 'yAxis', 'zAxis'))) for i in range(3)]
            delta = [point[i] - start[i] for i in range(3)]
            station = sum(delta[i] * axis[i] for i in range(3))
            if math.dist(delta, [station * value for value in axis]) > 1.e-7:
                raise ValueError('referenced workpiece must lie on the shared member axis')
            points.append(station)
        interval = {'startStation': points[0], 'endStation': points[1]}
    if interval is not None:
        if (source_key is None and set(requirements) != {'axialInterval', 'direction', 'status', 'ready'}
                or not isinstance(interval, dict) or set(interval) != {'startStation', 'endStation'}
                or any(isinstance(value, bool) or not isinstance(value, (int, float))
                       or not math.isfinite(value) for value in interval.values())):
            raise ValueError('stock preparation interval requires two finite shared-axis stations')
        first, last = interval['startStation'], interval['endStation']
        direction = requirements.get('direction', 'forward' if last > first else 'reverse')
        if (direction not in ('forward', 'reverse') or abs(last-first) <= 1.e-9
                or (last > first) != (direction == 'forward')):
            raise ValueError('stock preparation direction must agree with its signed interval')
        stock_start = [start[i] + axis[i] * first for i in range(3)]
        stock_end = [start[i] + axis[i] * last for i in range(3)]
        allowances = None
    else:
        if set(requirements) - {'connectionKeys', 'freeEnds', 'status', 'ready'}:
            raise ValueError('connection preparation cannot declare competing interval or direction requirements')
        free = requirements.get('freeEnds', [])
        if (not isinstance(free, list) or any(not isinstance(value, str) for value in free)
                or len(set(free)) != len(free)
                or any(value not in ('start', 'end') or value in allowances for value in free)):
            raise ValueError('free preparation endpoints must be explicit and unconnected')
        for name in free:
            allowances[name] = 0.0
            endpoints[name] = start if name == 'start' else end
        if set(allowances) != {'start', 'end'}:
            raise ValueError('stock preparation requires both declared endpoint allowances')
        for point in endpoints.values():
            delta = [point[i] - start[i] for i in range(3)]
            station = sum(delta[i] * axis[i] for i in range(3))
            if math.dist(delta, [station * value for value in axis]) > 1.e-7:
                raise ValueError('stock preparation endpoints must lie on the shared member axis')
        stock_start = [endpoints['start'][i] - axis[i] * allowances['start'] for i in range(3)]
        stock_end = [endpoints['end'][i] + axis[i] * allowances['end'] for i in range(3)]
        if sum((stock_end[i]-stock_start[i])*axis[i] for i in range(3)) <= 1.e-9:
            raise ValueError('stock preparation endpoints must retain the declared member direction')
    vector = [stock_end[i] - stock_start[i] for i in range(3)]
    section = next((node for node in model._document['geometry']
                    if node['key'] == profile.get('sectionResource') and node['operator'] == 'profile2d'), None)
    if section is None:
        raise ValueError('stock preparation requires the shared profile section resource')
    contours = deepcopy(section['arguments']['contours'])
    coordinate_map = profile.get('sectionCoordinateMap', [[1.0, 0.0], [0.0, 1.0]])
    if (not isinstance(coordinate_map, list) or len(coordinate_map) != 2
            or any(not isinstance(row, list) or len(row) != 2 for row in coordinate_map)
            or any(isinstance(value, bool) or not isinstance(value, (int, float))
                   or not math.isfinite(value) for row in coordinate_map for value in row)
            or coordinate_map not in ([[1.0, 0.0], [0.0, 1.0]], [[1.0, 0.0], [0.0, -1.0]],
                                      [[0.0, 1.0], [1.0, 0.0]], [[0.0, 1.0], [-1.0, 0.0]])):
        raise ValueError('stock preparation requires an exact intrinsic section coordinate map')
    if coordinate_map[0] == [0.0, 1.0]:
        contours = [_load('tube_profile_catalog')._swap_contour_axes(contour) for contour in contours]
    if coordinate_map[1] in ([0.0, -1.0], [-1.0, 0.0]):
        contours = [_load('security_window_machining_adapter')._reflect_contour(contour) for contour in contours]
    temporary = NeutralModel(template_id='profile-stock-preparation', template_version='1',
                             package_digest='', parameters={})
    shared = shared_module.SharedTubeGeometry(temporary)
    relative = inverse_frame(placement)
    station = sum((stock_start[i] - start[i]) * axis[i] for i in range(3))
    world_origin = [origin[i] + axis[i] * station for i in range(3)]
    def rotate(value):
        return [sum(relative[name][i] * value[j] for j, name in enumerate(('xAxis', 'yAxis', 'zAxis')))
                for i in range(3)]
    local_origin = [relative['origin'][i] + rotate(world_origin)[i] for i in range(3)]
    output = shared.emit_tube(process['key'] + '.prepared-stock', profile_arguments={
        'placement': {'origin': local_origin, 'xAxis': rotate(frame['xAxis']), 'yAxis': rotate(frame['yAxis'])},
        'contours': contours}, extrude_arguments={'vector': rotate(vector)})
    _merge_prepared_geometry(model, temporary)
    length = math.dist(stock_start, stock_end)
    properties['length'] = round(length, parameters['reportingLengthPrecision']) if 'reportingLengthPrecision' in parameters else length
    if 'requestedPartNumber' in parameters:
        properties['partNumber'] = parameters['requestedPartNumber']
    properties['tubeDesigner.assemblyPlanning'] = {'stockState': 'uncut', 'ready': requirements['ready']}
    physical_axis = shared_module._unit(vector, 'prepared member direction')
    properties['tubeDesigner.manufacturingAxis'] = physical_axis
    properties['tubeDesigner.manufacturingStartToEnd'] = physical_axis
    result = {'schema': 'icax.workpiece-preparation-result', 'schemaVersion': 1,
              'templateId': call['templateId'], 'processKey': process['key'], 'itemKey': item['key'],
              'sourceSectionResource': profile['sectionResource'], 'resultGeometry': output,
              'physicalLength': length, 'reportedLength': properties['length'],
              'status': requirements['status'], 'ready': requirements['ready']}
    if parameters:
        result['reportingRequirements'] = deepcopy(parameters)
    result['endAllowances' if allowances is not None else 'axialInterval'] = deepcopy(allowances if allowances is not None else interval)
    model._document.setdefault('extensions', {}).setdefault('tubeDesigner.workpiecePreparations', []).append(result)
    return output


def _ordered(processes):
    return _load('assembly_process_stages').ordered_processes(processes)


def _resolved_input(definition, initial, states, stock_geometry=None, model=None, process_key='', stock_placement=None, state_frames=None):
    source = definition['processInput']
    if source.get('resources') or definition.get('processDrafts'):
        raise ValueError('local machining provider does not support selected resource slots or drafts: ' + definition['templateId'])
    if source['schemaVersion'] == 1:
        return deepcopy(source)
    parts = {}
    for role, binding in source['parts'].items():
        scope = binding['scope']
        if scope == 'input':
            parts[role] = deepcopy(binding['data'])
        elif scope == 'manufacturing':
            facts = deepcopy(binding.get('data', {}))
            item = binding['itemKey']
            key = initial[item] if binding['state'] == 'initial' else states[(binding['state']['after'], item)]
            facts.pop('initialGeometry', None)
            if 'intrinsicGeometryKey' in facts:
                if role == 'stock':
                    key = stock_geometry or key
                else:
                    world_frame = IDENTITY_FRAME if binding['state'] == 'initial' else state_frames[(binding['state']['after'], item)]
                    frame = _compose(inverse_frame(stock_placement or IDENTITY_FRAME), world_frame)
                    if any(abs(frame[name][i]-IDENTITY_FRAME[name][i]) > 1.e-8 for name in IDENTITY_FRAME for i in range(3)):
                        key = model.geometry(process_key+'.observer.'+role, 'transform', inputs=[key], arguments={'placement': frame})
                facts['intrinsicGeometryKey'] = key
            if not facts:
                raise ValueError('manufacturing binding requires downstream allocated section and dimensions')
            parts[role] = facts
        elif scope == 'resource':
            key = binding['resourceKey']
            placement = _compose(inverse_frame(stock_placement or IDENTITY_FRAME), binding.get('placement', IDENTITY_FRAME))
            if any(abs(placement[name][i]-IDENTITY_FRAME[name][i]) > 1.e-8 for name in IDENTITY_FRAME for i in range(3)):
                key = model.geometry(process_key+'.observer.'+role, 'transform', inputs=[key], arguments={'placement': placement})
            parts[role] = {**deepcopy(binding.get('data', {})), 'intrinsicGeometryKey': key}
        else:
            raise ValueError('display observation requires an explicit downstream design snapshot')
    return {'schema': 'icax.assembly-process-input', 'schemaVersion': 1,
            'parts': parts, 'geometry': deepcopy(source['geometry'])}


def _part_list(model):
    document = model._document
    rows = []
    for item in document['items']:
        if not item.get('representations', {}).get('result'):
            continue
        properties = item.get('properties', {})
        profile = properties.get('tubeDesigner.profile', {})
        row = {'itemKey': item['key'], 'partNumber': properties.get('partNumber', item['key']),
               'name': item['displayName'], 'profile': profile.get('specification', ''),
               'material': properties.get('manufacturing.materialGrade', properties.get('manufacturing.material', properties.get('materialGrade', properties.get('material', '')))),
               'quantity': properties.get('quantity', 1)}
        for field in ('length', 'width', 'height', 'thickness'):
            if field in properties:
                row[field] = properties[field]
        rows.append({'key': 'row.' + item['key'], 'itemKey': item['key'], 'values': row})
    if not document.get('tables'):
        document['tables'] = [{'key': 'manufacturing.parts', 'displayName': '制造零件清单',
            'columns': [{'key': key, 'displayName': label, 'valueType': kind, **({'unit': 'mm'} if key == 'length' else {})}
                        for key, label, kind in [('itemKey', '零件', 'string'), ('partNumber', '编号', 'string'),
                            ('name', '名称', 'string'), ('profile', '管型', 'string'), ('material', '材质', 'string'),
                            ('length', '下料长度', 'number'), ('quantity', '数量', 'integer')]], 'rows': rows}]


def _components(document, context):
    """Resolve initial accessory inputs from host-frozen component snapshots."""
    snapshots = context.get('componentSnapshots', {})
    resource_keys = {node['key'] for node in document['resources']}
    for item in document['items']:
        if 'componentReference' not in item:
            continue
        declaration = item.pop('componentReference')
        ref = declaration.get('ref', declaration)
        reference = declaration.get('reference') or (ref.get('scope', '') + ':' + ref.get('id', ''))
        if reference not in snapshots:
            raise ValueError('manufacturing component has no host-frozen snapshot: ' + reference)
        snapshot = snapshots[reference]
        if not isinstance(snapshot, dict) or not snapshot.get('brep') or not snapshot.get('geometryDigest'):
            raise ValueError('manufacturing component snapshot is incomplete')
        key = 'component.initial.' + hashlib.sha256(reference.encode('utf-8')).hexdigest()[:24]
        if key not in resource_keys:
            document['resources'].append({'key': key, 'operator': 'resource', 'inputs': [],
                'arguments': {'reference': reference, 'brep': snapshot['brep'], 'geometryDigest': snapshot['geometryDigest']}})
            resource_keys.add(key)
        item['geometry'] = {'resource': key}
        if 'placement' in declaration:
            item['geometry']['placement'] = deepcopy(declaration['placement'])
        properties = item.setdefault('properties', {})
        properties.update({'manufacturing.modelReference': reference, 'manufacturing.partKind': 'accessory',
            'manufacturing.materialCategory': 'accessory', 'manufacturing.sourcing': snapshot.get('sourcing', 'purchased'),
            'manufacturing.material': snapshot.get('material', ''), 'length': 0.0})
    for node in document['resources']:
        reference = node.get('arguments', {}).get('reference') if node.get('operator') == 'resource' else None
        if reference and reference in snapshots:
            snapshot = snapshots[reference]
            node['arguments'].update(brep=snapshot['brep'], geometryDigest=snapshot['geometryDigest'])


_ASSEMBLY_PLANNERS = {
    'security-window-assembly': ('assembly_window_process', 'plan'),
}


def _planner_inputs(call, document, output_sets):
    """Resolve immutable typed inputs; generated sets require named producers."""
    items = {item['key']: item for item in document['items']}
    resources = {node['key']: node for node in document['resources']}
    resolved = {}
    for role, binding in call['processInput']['parts'].items():
        scope = binding['scope']
        if scope == 'design':
            selected = set(binding['itemKeys'])
            resolved[role] = {'kind': 'design-set',
                'items': [deepcopy(items[key]) for key in binding['itemKeys']],
                'sourceMappings': [deepcopy(mapping) for mapping in document['sourceMappings'] if mapping['itemKey'] in selected]}
        elif scope == 'process-output':
            producer = (binding['processKey'], binding['outputKey'])
            if producer not in output_sets:
                raise ValueError('assembly input has no completed named output set')
            resolved[role] = deepcopy(output_sets[producer])
        elif scope == 'input':
            resolved[role] = {'kind': 'input', 'data': deepcopy(binding['data'])}
        elif scope == 'resource':
            resolved[role] = {'kind': 'resource', 'resource': deepcopy(resources[binding['resourceKey']]),
                              'placement': deepcopy(binding['placement']), 'data': deepcopy(binding.get('data', {}))}
        else:
            raise ValueError('whole assembly planner requires explicit design inputs or named generated sets')
    return resolved


def _execute_assembly_planners(definition, context):
    """Whole processes create their own manufacturing objects, independently."""
    ordered = _ordered(definition['processes'])
    planners = [process for process in ordered if 'outputs' in process['definition']]
    fixed = [process for process in ordered if 'targets' in process['definition']]
    planner_keys = {process['key'] for process in planners}
    if any(planner_keys.intersection(process['definition']['dependencies']) for process in fixed):
        raise ValueError('local machining of a generated set must be declared by its consuming assembly planner')
    consumed = {key for process in planners for binding in process['definition']['processInput']['parts'].values()
                if binding['scope'] == 'design' for key in binding['itemKeys']}
    if any(consumed.intersection(process['definition']['targets'].values()) for process in fixed):
        raise ValueError('one input cannot be both an existing local target and a generated-set design source')
    known = deepcopy(definition)
    known['items'] = [item for item in known['items'] if item['key'] not in consumed]
    for item in known['items']:
        item['children'] = [key for key in item.get('children', []) if key not in consumed]
    known['roots'] = [key for key in known['roots'] if key not in consumed]
    known['sourceMappings'] = [mapping for mapping in known['sourceMappings'] if mapping['itemKey'] not in consumed]
    known['processes'] = fixed
    base = expand_resource_model(execute_manufacturing(known, context))
    geometry = {node['key']: node for node in base['geometry']}
    items = {item['key']: item for item in base['items']}
    original = {item['key']: item for item in definition['items']}
    parents = {key: item['key'] for item in definition['items'] for key in item.get('children', [])}
    replacement, output_sets, records, generated_entries = {}, {}, [], []
    consumed_outputs = {(binding['processKey'], binding['outputKey'])
                        for process in planners for binding in process['definition']['processInput']['parts'].values()
                        if binding['scope'] == 'process-output'}
    for process in planners:
        call = process['definition']
        registered = _ASSEMBLY_PLANNERS.get(call['templateId'])
        if registered is None:
            raise ValueError('unregistered whole assembly planner: ' + call['templateId'])
        module_name, entry_name = registered
        planner = getattr(_load(module_name), entry_name)
        inputs = _planner_inputs(call, definition, output_sets)
        planned = planner(deepcopy(call), inputs, deepcopy(definition['resources']), deepcopy(context))
        if not isinstance(planned, dict) or set(planned) != {'definition', 'sourceItemKeys'}:
            raise ValueError('registered assembly planner must return its private definition and design sources')
        declared_sources = planned['sourceItemKeys']
        source_lookup = {key: {key} for key in consumed}
        for snapshot in inputs.values():
            for mapping in snapshot.get('sourceMappings', []):
                source_lookup[mapping['itemKey']] = {source['itemKey'] for source in mapping['sources']}
        if (not isinstance(declared_sources, list) or not declared_sources
                or len(declared_sources) != len(set(declared_sources))
                or not set(declared_sources) <= set(source_lookup)):
            raise ValueError('generated manufacturing set requires explicit existing design sources')
        sources = sorted({key for source in declared_sources for key in source_lookup[source]})
        if not set(sources) <= consumed:
            raise ValueError('generated manufacturing set has an unknown original design source')
        generated = expand_resource_model(execute_manufacturing(planned['definition'], context))
        actual = [item['key'] for item in generated['items'] if item.get('representations', {}).get('result')]
        if not actual:
            raise ValueError('registered assembly planner produced an empty manufacturing set')
        source_map = {}
        for mapping in planned['definition']['sourceMappings']:
            referenced = {source['itemKey'] for source in mapping['sources']}
            if not referenced <= set(source_lookup):
                raise ValueError('generated manufacturing item has an unknown source output')
            source_map[mapping['itemKey']] = {key for source in referenced for key in source_lookup[source]}
        if len(call['outputs']) != 1:
            raise ValueError('registered assembly planner currently requires one explicit manufacturing-set output')
        for role, output in call['outputs'].items():
            record = {'processKey': process['key'], 'outputKey': role, 'key': output['key'],
                      'kind': 'manufacturing-set', 'itemKeys': actual, 'sourceItemKeys': deepcopy(sources)}
            records.append(record)
            output_sets[(process['key'], role)] = {**deepcopy(record), 'model': deepcopy(generated),
                'sourceMappings': [{'itemKey': key, 'sources': [{'itemKey': source} for source in sorted(source_map.get(key, set(sources)))]}
                                   for key in actual]}
            if (process['key'], role) not in consumed_outputs:
                generated_entries.append((generated, sources, source_map))
    for generated, sources, source_map in generated_entries:
        for item in generated['items']:
            key = item['key']
            if key in items:
                raise ValueError('generated manufacturing item identity conflicts with another output: ' + key)
            referenced = source_map.get(key, set(sources))
            if not referenced or not referenced <= set(sources):
                raise ValueError('generated manufacturing item has an unknown design source')
            containers = {parents.get(source) for source in referenced}
            if len(containers) != 1:
                raise ValueError('a manufactured object cannot merge design members from different product groups')
            for source in referenced:
                replacement.setdefault(source, []).append(key)
            items[key] = deepcopy(item)
        for node in generated['geometry']:
            if node['key'] in geometry and geometry[node['key']] != node:
                raise ValueError('generated manufacturing geometry resource conflicts with another output')
            if node['key'] not in geometry:
                geometry[node['key']] = deepcopy(node)
        for name, value in generated.get('extensions', {}).items():
            if name in {'tubeDesigner.manufacturingPartCount', 'tubeDesigner.manufacturingInputVersion'}:
                continue
            extensions = base.setdefault('extensions', {})
            if name not in extensions:
                extensions[name] = deepcopy(value)
            elif name == 'tubeDesigner.assemblyStagePlan':
                extensions[name] = _load('assembly_process_stages').merge_stage_plans(extensions[name], value)
            elif name == 'tubeDesigner.assemblyMaterialPlan':
                extensions[name] = _load('assembly_process_stages').merge_material_plans(extensions[name], value)
            elif isinstance(value, list) and isinstance(extensions[name], list):
                extensions[name].extend(deepcopy(value))
            elif extensions[name] != value:
                raise ValueError('independent assembly outputs have conflicting execution metadata: ' + name)
    def replaced(keys):
        result, seen = [], set()
        for key in keys:
            candidates = replacement.get(key, []) if key in consumed else [key]
            for candidate in candidates:
                if candidate not in seen:
                    result.append(candidate)
                    seen.add(candidate)
        return result
    for key, item in items.items():
        if key in original and key not in consumed:
            item['children'] = replaced(original[key].get('children', []))
    roots = replaced(definition['roots'])
    visible, visited = [], set()
    def collect(key):
        if key in visited:
            raise ValueError('generated manufacturing hierarchy contains a duplicate instance')
        visited.add(key)
        item = items[key]
        if item.get('representations', {}).get('result'):
            visible.append(key)
        for child in item.get('children', []):
            collect(child)
    for key in roots:
        collect(key)
    if visited != set(items):
        raise ValueError('generated manufacturing hierarchy has unreachable output objects')
    base['geometry'] = list(geometry.values())
    base['items'] = list(items.values())
    base['outputs'][0]['items'] = visible
    base['tables'] = [table for table in base.get('tables', []) if table['key'] != 'manufacturing.parts']
    base['template'] = deepcopy(context.get('template', base['template']))
    extensions = base.setdefault('extensions', {})
    extensions.update({'tubeDesigner.manufacturingPartCount': len(visible),
                       'tubeDesigner.manufacturingInputVersion': definition['schemaVersion'],
                       'tubeDesigner.assemblyOutputSets': records})
    model = _builder(base)
    _part_list(model)
    return to_resource_model(model.build())


def _declared_tube_extent(document, item):
    """Read one actual stock primitive, never infer it from names or reports."""
    shared = _load('shared_tube_geometry')
    properties = item.get('properties', {})
    nodes = {node['key']: node for node in document.get('resources', document.get('geometry', []))}
    reference = item.get('geometry') or item.get('representations', {}).get('result')
    if isinstance(reference, str):
        key, pose = reference, deepcopy(IDENTITY_FRAME)
    elif isinstance(reference, dict):
        key, pose = reference.get('resource'), deepcopy(reference.get('placement', IDENTITY_FRAME))
    else:
        raise ValueError('source mapping requires an actual tube geometry reference: ' + item['key'])
    visited = set()
    def frame(value):
        if not isinstance(value, dict):
            raise ValueError('source mapping geometry requires an explicit rigid frame')
        origin = shared._vector(value.get('origin'), 'stock origin')
        x = shared._unit(shared._vector(value.get('xAxis'), 'stock xAxis'), 'stock xAxis')
        y = shared._unit(shared._vector(value.get('yAxis'), 'stock yAxis'), 'stock yAxis')
        z = shared._unit(shared._cross(x, y), 'stock zAxis')
        if abs(shared._dot(x, y)) > 1.e-7:
            raise ValueError('source mapping stock axes must be orthogonal')
        if 'zAxis' in value and math.dist(z, shared._vector(value['zAxis'], 'stock zAxis')) > 1.e-7:
            raise ValueError('source mapping stock frame must be right handed and rigid')
        return {'origin': origin, 'xAxis': x, 'yAxis': y, 'zAxis': z}
    pose = frame(pose)
    def point(value):
        value = shared._vector(value, 'stock point')
        return [pose['origin'][i] + sum(pose[name][i]*value[j]
                for j,name in enumerate(('xAxis','yAxis','zAxis'))) for i in range(3)]
    def vector(value):
        value = shared._vector(value, 'stock direction')
        return [sum(pose[name][i]*value[j] for j,name in enumerate(('xAxis','yAxis','zAxis')))
                for i in range(3)]
    while True:
        if key in visited or key not in nodes:
            raise ValueError('source mapping stock geometry is missing or cyclic: ' + item['key'])
        visited.add(key)
        node = nodes[key]; operator = node['operator']; inputs = node.get('inputs', [])
        args = node.get('arguments', {})
        if operator == 'transform':
            if len(inputs) != 1:
                raise ValueError('source mapping transform requires one actual base')
            pose = _compose(pose, frame(args.get('placement')))
            key = inputs[0]
            continue
        if operator == 'boolean':
            if (args.get('operation') not in ('subtract', 'intersect') or len(inputs) < 2
                    or args.get('target') != inputs[0] or args.get('tools') != inputs[1:]):
                raise ValueError('source mapping supports only an explicitly declared subtractive stock base')
            key = inputs[0]
            continue
        if operator == 'extrude':
            if len(inputs) != 1 or inputs[0] not in nodes or nodes[inputs[0]]['operator'] != 'profile2d':
                raise ValueError('source mapping tube requires a unique actual profile extrusion')
            section = nodes[inputs[0]]['arguments']
            section_frame = frame(section['placement'])
            start = point(section_frame['origin'])
            delta = vector(args['vector'])
            end = [start[i]+delta[i] for i in range(3)]
            across = vector(section_frame['xAxis'])
            if abs(shared._dot(shared._unit(delta, 'stock axis'), across)) > 1.e-7:
                raise ValueError('source mapping extrusion is not a straight perpendicular tube')
            return start, end, across
        raise ValueError('unsupported source mapping stock primitive ' + operator + ': ' + item['key'])


def _materialize_product_source_mappings(result, definition, design_model):
    """Persist the current explicit design mapping at the execution boundary."""
    shared = _load('shared_tube_geometry')
    sources = {item['key']: item for item in design_model['items']}
    declarations = {mapping['itemKey']: mapping['sources'] for mapping in definition['sourceMappings']}
    visible = {key for output in result['outputs'] if output['purpose'] == 'result' for key in output['items']}
    used = set()
    for stock in result['items']:
        if stock['key'] not in visible:
            continue
        properties = stock['properties']
        existing = properties.get('manufacturing.sourceMembers')
        if (isinstance(existing, list) and existing and all(isinstance(mapping, dict)
                and (isinstance(mapping.get('spans'), list) and mapping['spans']
                     or mapping.get('mappingKind') == 'identity') for mapping in existing)):
            # Preserve allocator-produced rich spans, including seam splits.
            if not isinstance(existing, list) or not existing:
                raise ValueError('current product source mapping cannot be empty')
            for mapping in existing:
                if mapping['itemKey'] not in sources or mapping['itemKey'] in used:
                    raise ValueError('current product source mapping is unknown or duplicated')
                used.add(mapping['itemKey'])
            continue
        declared = declarations.get(stock['key'])
        if not isinstance(declared, list) or len(declared) != 1 or set(declared[0]) != {'itemKey'}:
            raise ValueError('ordinary stock requires an explicit single design identity mapping: ' + stock['key'])
        source_key = declared[0]['itemKey']
        if source_key not in sources or source_key in used:
            raise ValueError('product identity source is unknown or duplicated: ' + source_key)
        source = sources[source_key]
        source_kind = source.get('properties', {}).get('partKind')
        stock_kind = properties.get('manufacturing.partKind')
        if not source_kind or source_kind != stock_kind:
            raise ValueError('product identity source and output kinds must agree: ' + source_key)
        if stock_kind != 'tube':
            if stock['key'] != source_key:
                raise ValueError('nonlinear product mapping must be an explicitly declared identity')
            properties['manufacturing.sourceMembers'] = [{'itemKey': source_key, 'mappingKind': 'identity'}]
            used.add(source_key)
            continue
        member = source['properties'].get('assemblyFrame.member')
        if isinstance(member, dict) and isinstance(member.get('sectionFrame'), dict):
            start = shared._vector(member['start'], 'design member start')
            end = shared._vector(member['end'], 'design member end')
            across = shared._unit(shared._vector(member['sectionFrame']['xAxis'], 'design member section xAxis'),
                                  'design member section xAxis')
        else:
            start, end, across = _declared_tube_extent(design_model, source)
        physical_start, physical_end, physical_across = _declared_tube_extent(result, stock)
        axis = shared._unit([physical_end[i]-physical_start[i] for i in range(3)], 'physical stock axis')
        normal = shared._unit(shared._cross(axis, physical_across), 'physical stock section normal')
        across = shared._unit(across, 'source section xAxis')
        stations = [shared._dot([p[i]-physical_start[i] for i in range(3)], axis) for p in (start,end)]
        length = math.dist(physical_start,physical_end)
        source_length = math.dist(start,end)
        source_range = [0.0, 1.0]
        if stations[0] < -1.e-7 or stations[1] > length+1.e-7:
            preparations = [entry for entry in result.get('extensions', {}).get('tubeDesigner.workpiecePreparations', [])
                if entry.get('itemKey') == stock['key'] and entry.get('templateId') == 'profile-stock-preparation']
            if len(preparations) != 1 or not isinstance(preparations[0].get('axialInterval'), dict):
                raise ValueError('source interval changes require an explicit completed stock preparation')
            interval = preparations[0]['axialInterval']
            first_station, last_station = interval['startStation'], interval['endStation']
            direction = shared._unit([end[i]-start[i] for i in range(3)], 'source member axis')
            prepared_start = [start[i]+direction[i]*first_station for i in range(3)]
            prepared_end = [start[i]+direction[i]*last_station for i in range(3)]
            if (not 0.0 <= first_station < last_station <= source_length
                    or math.dist(prepared_start,physical_start) > 1.e-7
                    or math.dist(prepared_end,physical_end) > 1.e-7):
                raise ValueError('declared source preparation interval must match the actual stock geometry')
            source_range = [first_station/source_length,last_station/source_length]
            start, end = prepared_start, prepared_end
            source_length = math.dist(start,end)
            stations = [0.0,length]
        if (length <= 1.e-8 or source_length <= 1.e-8 or stations[1] <= stations[0]
                or stations[0] < -1.e-7 or stations[1] > length+1.e-7
                or abs(stations[1]-stations[0]-source_length) > 1.e-7):
            raise ValueError('product source span must lie inside the real stock axial extent: ' + source_key)
        first = max(0.0, stations[0]); last = min(length, stations[1])
        axes = (axis, physical_across, normal)
        placement = {'origin': [-shared._dot(physical_start, direction) for direction in axes],
                     **{name: [direction[i] for direction in axes]
                        for i,name in enumerate(('xAxis','yAxis','zAxis'))}}
        properties['tubeDesigner.assemblyProcessStockLength'] = length
        properties['manufacturing.sourceMembers'] = [{'itemKey': source_key, 'spans': [{
            'start': deepcopy(start), 'end': deepcopy(end), 'stockStart': 0.0,
            'startReserve': first, 'endReserve': length-last, 'placement': placement,
            'sourceRange': source_range}]}]
        used.add(source_key)
    expected = {item['key'] for item in design_model['items'] if item.get('geometry')}
    if used != expected:
        raise ValueError('current product source mapping must cover every actual design geometry item')
    result.setdefault('extensions', {})['tubeDesigner.productStockMappingVersion'] = 1


def execute_manufacturing(definition, context=None, design_model=None):
    validate_manufacturing_model(definition)
    if definition['schemaVersion'] == 4:
        combined = compose_manufacturing_model(definition, design_model)
        execution_context = deepcopy(context or {})
        execution_context['_sharedDesignInput'] = True
        execution_context['_sharedConnections'] = deepcopy(definition['connections'])
        result = execute_manufacturing(combined, execution_context)
        _materialize_product_source_mappings(result, combined, design_model)
        result.setdefault('extensions', {})['tubeDesigner.manufacturingInputVersion'] = 4
        return result
    definition = deepcopy(definition)
    context = deepcopy(context or {})
    if any('outputs' in process['definition'] for process in definition['processes']):
        return _execute_assembly_planners(definition, context)
    _components(definition, context)
    runtime = _load('assembly_geometry_process_runtime')
    has_paths = any('manufacturingInput' in item for item in definition['items'])
    extra_items = []
    extra_document = None
    if has_paths:
        path_keys = {item['key'] for item in definition['items'] if 'manufacturingInput' in item}
        path_definition = deepcopy(definition)
        path_definition['items'] = [item for item in path_definition['items'] if item['key'] in path_keys]
        path_definition['roots'] = [item['key'] for item in path_definition['items']]
        path_definition['sourceMappings'] = [entry for entry in path_definition['sourceMappings'] if entry['itemKey'] in path_keys]
        mapped = {entry['itemKey'] for entry in path_definition['sourceMappings']}
        for key in path_keys-mapped:
            path_definition['sourceMappings'].append({'itemKey': key, 'sources': []})
        path_definition['processes'] = []
        extra_calls = []
        for call in definition['processes']:
            targets = set(call['definition']['targets'].values())
            if targets <= path_keys:
                path_definition['processes'].append(deepcopy(call))
            elif targets.isdisjoint(path_keys):
                extra_calls.append(deepcopy(call))
            else:
                raise ValueError('one assembly call cannot mix tube-path allocation and intrinsic geometry outputs')
        known = deepcopy(definition)
        known['items'] = [item for item in known['items'] if item['key'] not in path_keys]
        for item in known['items']:
            item['children'] = [key for key in item.get('children', []) if key not in path_keys]
        children = {key for item in known['items'] for key in item.get('children', [])}
        known['roots'] = [item['key'] for item in known['items'] if item['key'] not in children]
        known['sourceMappings'] = [entry for entry in known['sourceMappings'] if entry['itemKey'] not in path_keys]
        known['processes'] = extra_calls
        if known['items']:
            extra_document = expand_resource_model(execute_manufacturing(known, context))
            extra_items = extra_document['items']
        allocated = _load('assembly_material_allocation').allocate_window_manufacturing(path_definition, context)
        model = _builder(allocated['model'])
        if allocated.get('stagePlan'):
            model._document.setdefault('extensions', {})['tubeDesigner.assemblyStagePlan'] = deepcopy(allocated['stagePlan'])
        if allocated.get('assemblySource'):
            # The native assembly executor owns its real requestEnd, tooling,
            # Boolean and forming stages and freezes their combined result.
            model._document.setdefault('extensions', {})['tubeDesigner.assemblyProcessSource'] = deepcopy(allocated['assemblySource'])
            processes, plans = [], {}
        else:
            processes, plans = allocated['processes'], allocated['allocationPlans']
    else:
        model = _builder(initial_manufacturing_model(definition))
        processes, plans = definition['processes'], {}
    if extra_document:
        nodes = {node['key']: node for node in model._document['geometry']}
        for node in extra_document['geometry']:
            if node['key'] in nodes and nodes[node['key']] != node:
                raise ValueError('conflicting initial geometry resource across manufacturing inputs')
            if node['key'] not in nodes:
                model._document['geometry'].append(deepcopy(node))
                model._geometry_keys.add(node['key'])
                nodes[node['key']] = node
        combined_items = {item['key']: item for item in model._document['items'] + extra_items}
        model._document['items'] = [combined_items[item['key']] for item in definition['items']]
        for item in model._document['items']:
            item['children'] = deepcopy(next(value.get('children', []) for value in definition['items'] if value['key'] == item['key']))
        model._item_keys = set(combined_items)
        model._document['outputs'][0]['items'] = [item['key'] for item in model._document['items'] if item.get('representations', {}).get('result')]
        for name, value in extra_document.get('extensions', {}).items():
            if name == 'tubeDesigner.assemblyGeometryProcesses':
                model._document.setdefault('extensions', {})[name] = deepcopy(value)
    items = {item['key']: item for item in model._document['items']}
    initial = {key: item['representations']['result'] for key, item in items.items() if item.get('representations', {}).get('result')}
    states = {}
    state_frames, joint_planes, last_state, prepared_stocks = {}, {}, {}, {}
    if context.get('_sharedDesignInput'):
        targeted = {key for process in processes for key in process['definition']['targets'].values()}
        for key, item in items.items():
            plate = item.get('properties', {}).get('plate')
            # A complete intrinsic frame defines the real plate. Decorative
            # display faces alone do not define a manufacturing workpiece.
            if key in initial and key not in targeted and isinstance(plate, dict) \
                    and all(name in plate for name in ('center', 'xAxis', 'yAxis')):
                prepared = _prepare_shared_stock(model, key + '.prepare', None, item, IDENTITY_FRAME)
                initial[key] = prepared
                item['representations']['result'] = prepared
                model._document.setdefault('extensions', {}).setdefault('tubeDesigner.workpiecePreparations', []).append({
                    'schema': 'icax.workpiece-preparation-result', 'schemaVersion': 1,
                    'templateId': 'intrinsic-plate-preparation', 'itemKey': key,
                    'sourceComponent': 'plate', 'resultGeometry': prepared, 'status': 'prepared'})
    for process in _ordered(processes):
        call = process['definition']
        targets = call['targets']
        if set(targets) != {'stock'}:
            raise ValueError('this machining provider requires one stock output')
        item_key = targets['stock']
        binding = call['processInput']['parts']['stock']
        if call['templateId'] == 'profile-stock-preparation':
            if not context.get('_sharedDesignInput'):
                raise ValueError('profile stock preparation requires the host shared design')
            if item_key in prepared_stocks or item_key in last_state:
                raise ValueError('a shared member requires one preparation before its machining writers')
            geometry_input = binding.get('data', {}).get('initialGeometry', {})
            source_key = call['processInput']['geometry'].get('sourceProcessKey')
            if source_key is not None:
                source = next((entry['definition'] for entry in processes if entry['key'] == source_key), {})
                geometry_input = source.get('processInput', {}).get('parts', {}).get('stock', {}).get('data', {}).get('initialGeometry', {})
            placement = geometry_input.get('itemPlacement', IDENTITY_FRAME)
            output = _prepare_profile_stock(model, process, items[item_key], context['_sharedConnections'], placement, processes)
            world = output if placement == IDENTITY_FRAME else model.geometry(process['key']+'.prepared-item',
                'transform', inputs=[output], arguments={'placement': placement})
            model._document['extensions']['tubeDesigner.workpiecePreparations'][-1]['resultGeometry'] = world
            items[item_key]['representations']['result'] = world
            initial[item_key] = world
            prepared_stocks[item_key] = (output, deepcopy(placement))
            states[(process['key'], item_key)] = output
            state_frames[(process['key'], item_key)] = deepcopy(placement)
            last_state[item_key] = (output, deepcopy(placement))
            continue
        geometry_input = binding.get('data', {}).get('initialGeometry', {}) if call['processInput']['schemaVersion'] in (2, 3) else {}
        placement = geometry_input.get('itemPlacement', IDENTITY_FRAME)
        input_geometry = geometry_input.get('resource', initial[item_key])
        if context.get('_sharedDesignInput') and binding['state'] == 'initial':
            if item_key not in prepared_stocks:
                prepared = _prepare_shared_stock(model, process['key'], call, items[item_key], placement)
                if prepared is not None:
                    prepared_stocks[item_key] = (prepared, deepcopy(placement))
                    initial[item_key] = prepared if placement == IDENTITY_FRAME else model.geometry(
                        process['key'] + '.prepared-item', 'transform', inputs=[prepared],
                        arguments={'placement': placement})
            if item_key in prepared_stocks:
                input_geometry, prepared_frame = prepared_stocks[item_key]
                relative = _compose(inverse_frame(placement), prepared_frame)
                if any(abs(relative[name][i]-IDENTITY_FRAME[name][i]) > 1.e-8 for name in IDENTITY_FRAME for i in range(3)):
                    input_geometry = model.geometry(process['key']+'.prepared-state', 'transform',
                        inputs=[input_geometry], arguments={'placement': relative})
        if call['processInput']['schemaVersion'] in (2, 3) and binding['state'] != 'initial':
            prior = (binding['state']['after'], item_key)
            input_geometry = states[prior]
            relative = _compose(inverse_frame(placement), state_frames[prior])
            if any(abs(relative[name][i]-IDENTITY_FRAME[name][i]) > 1.e-8 for name in IDENTITY_FRAME for i in range(3)):
                input_geometry = model.geometry(process['key']+'.input-state', 'transform', inputs=[input_geometry], arguments={'placement': relative})
        # Read the declared snapshot, then accumulate commuting cuts on the
        # same item. Initial never silently becomes the latest observation.
        current = input_geometry
        if item_key in last_state:
            previous_output, previous_frame = last_state[item_key]
            current = previous_output
            relative = _compose(inverse_frame(placement), previous_frame)
            if any(abs(relative[name][i]-IDENTITY_FRAME[name][i]) > 1.e-8 for name in IDENTITY_FRAME for i in range(3)):
                current = model.geometry(process['key']+'.commit-state', 'transform', inputs=[current], arguments={'placement': relative})
        local = _resolved_input(call, initial, states, input_geometry, model, process['key'], placement, state_frames)
        result = runtime.evaluate(call['templateId'], local, call['parameters'])
        output = runtime.apply(model, current, result, process['key'], item_key)
        if placement == IDENTITY_FRAME:
            items[item_key]['representations']['result'] = output
        else:
            items[item_key]['representations']['result'] = model.geometry(process['key']+'.item', 'transform', inputs=[output], arguments={'placement': placement})
        states[(process['key'], item_key)] = output
        state_frames[(process['key'], item_key)] = placement
        last_state[item_key] = (output, placement)
        for requirement in result.get('materialRequirements', []):
            if requirement.get('kind') == 'joint-plane' and requirement.get('role') == 'stock':
                joint_planes.setdefault(item_key, []).append((deepcopy(requirement), deepcopy(local['parts']['stock'])))
    for item_key, records in joint_planes.items():
        unique = {tuple(record['point']): (record, stock) for record, stock in records}
        if len(unique) != 2:
            raise ValueError('a fitted two-end member requires exactly two joint planes')
        pair = list(unique.values())
        section = pair[0][1]['section']
        frame = section['placement']
        vector = section['vector']
        length = sum(value*value for value in vector)**0.5
        direction = [value/length for value in vector]
        profile = items[item_key]['properties']['tubeDesigner.profile']
        allowance = _load('assembly_structural_machining').joint_blank_extension(
            [record['inwardNormal'] for record, _ in pair], frame['xAxis'], frame['yAxis'],
            direction, profile['width'], profile['depth'])
        items[item_key]['properties']['length'] = sum((a-b)**2 for a,b in zip(pair[0][0]['point'], pair[1][0]['point']))**0.5 + allowance
    for item_key, plan in plans.items():
        item = items[item_key]
        properties = item['properties']
        cutters = runtime.materialize_resolved_tool_plan(model, plan, properties['tubeDesigner.profile'],
            properties['length'], item_key + '.assembly.tools', user_mould_root=context.get('userMouldRoot'))
        item['representations']['result'] = runtime.apply_frozen_cutters(model, item['representations']['result'],
            cutters, item_key + '.fold.cuts', item_key, plan)
    _part_list(model)
    by_key = {item['key']: item for item in model._document['items']}
    visible = []
    def collect(key):
        item = by_key[key]
        if item.get('representations', {}).get('result'):
            visible.append(key)
        for child in item.get('children', []):
            collect(child)
    for key in definition['roots']:
        collect(key)
    model._document['outputs'][0]['items'] = visible
    document = model.build()
    document['template'] = deepcopy(context.get('template', document['template']))
    extensions = document.setdefault('extensions', {})
    extensions['tubeDesigner.geometryPurpose'] = 'manufacturing'
    extensions['tubeDesigner.manufacturingPartCount'] = len(initial)
    extensions['tubeDesigner.manufacturingInputVersion'] = definition['schemaVersion']
    extensions.setdefault('tubeDesigner.assemblyStagePlan', _load('assembly_process_stages').plan_stages(processes))
    return to_resource_model(document)
