"""Registered whole-window assembly planner, owned by the downstream process.

The product supplies stable design members. This planner owns route-specific
manufacturing identities, continuous paths and the local process calls. It never
loads a product template or asks one to regenerate a manufacturing plan.
"""
from copy import deepcopy
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import sys
from types import SimpleNamespace


def _load(name):
    path = Path(__file__).with_name(name + '.py')
    key = 'icax_window_assembly_' + name + '_' + hashlib.sha256(path.read_bytes()).hexdigest()[:16]
    if key not in sys.modules:
        spec = importlib.util.spec_from_file_location(key, path)
        module = importlib.util.module_from_spec(spec)
        sys.modules[key] = module
        spec.loader.exec_module(module)
    return sys.modules[key]


def sub(a, b): return [x-y for x, y in zip(a, b)]
def dot(a, b): return sum(x*y for x, y in zip(a, b))
def cross(a, b): return [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]]
def unit(a):
    length = math.sqrt(dot(a, a))
    if length < 1.e-8: raise ValueError('制造路径不能有退化区段')
    return [value/length for value in a]


def _profile(values):
    return _load('tube_profile_catalog').Profile(
        values.get('id', 'frozen'), values.get('packageVersion', ''), values.get('kind', 'fixed-section'),
        values.get('displayName', ''), values.get('specification', ''), values['width'], values['depth'],
        values['wallThickness'], values.get('cornerRadius', 0.), {'profileForm': 'frozen'}, None,
        deepcopy(values['contours']))


def _clean_profile(values):
    result = deepcopy(values)
    for key in ('provenance', 'manufacturing', 'manufacturingRoute', 'geometrySource', 'sourceRevision'):
        result.pop(key, None)
    return result


def _section_frame(member, start, end):
    z = unit(sub(end, start))
    x = unit(member['sectionFrame']['xAxis'])
    return {'origin': list(start), 'xAxis': x, 'yAxis': unit(cross(z, x)), 'zAxis': z}


def _call(key, template, parts, geometry, parameters=None, resources=None, drafts=None, targets=None):
    local = {'schema': 'icax.assembly-process-input', 'schemaVersion': 2,
             'parts': deepcopy(parts), 'geometry': deepcopy(geometry)}
    if resources: local['resources'] = deepcopy(resources)
    return {'key': key, 'kind': 'assembly-process', 'definition': {
        'templateId': template, 'processInput': local, 'parameters': deepcopy(parameters or {}),
        'processDrafts': deepcopy(drafts or {}), 'targets': deepcopy(targets or {}), 'dependencies': []}}


def _declare_material_dependencies(processes, owners, stock_keys):
    """Declare child dependencies from material ownership, without order fields."""
    related = {}
    for process in processes:
        definition = process['definition']
        keys = set(definition['targets'].values())
        for binding in definition['processInput']['parts'].values():
            if binding.get('scope') not in ('manufacturing', 'display'):
                continue
            identity = binding['itemKey']
            owner = owners.get(identity, identity)
            if owner not in stock_keys:
                raise ValueError('窗口工艺角色缺少明确的母材来源: '+process['key']+'/'+identity)
            keys.add(owner)
        related[process['key']] = keys
    runtime = _load('assembly_process_stages')
    declared = runtime.with_material_dependencies(processes, material_bindings=related)
    # End allocation supplies the actual branch/receiver extents observed by
    # aperture placement. Only a writer of those materials is a prerequisite;
    # a mate observed by an end call does not spread its dependencies further.
    end_allocations = {}
    for process in declared:
        definition = process['definition']
        if (definition['templateId'] in ('tube-end-joint', 'tube-post-end')
                and definition['processInput']['geometry'].get('allocation')!='initial-material-boundary'):
            for stock in definition['targets'].values():
                end_allocations.setdefault(stock, set()).add(process['key'])
        elif definition['templateId']=='orthogonal-corner':
            # The continuous A/B frame supplies a receiver, while only C's
            # end allocation changes the independent post's finished extent.
            stock = definition['targets'].get('memberC')
            if stock is not None:
                end_allocations.setdefault(stock, set()).add(process['key'])
    for process in declared:
        definition = process['definition']
        if definition['templateId'] not in ('tube-profile-aperture', 'tube-post-aperture'):
            continue
        needed = set()
        for stock in related[process['key']]:
            needed.update(end_allocations.get(stock, ()))
        definition['dependencies'].extend(sorted(needed-set(definition['dependencies'])))
    runtime.plan_stages(declared)
    return declared


_CHOICE_FIELDS = {
    'assemblyPlanningMode',
    'frameManufacturingMode', 'frameJoinType', 'frameCornerJoin', 'frameButtWrapMode',
    'doorFrameJoinType', 'doorFrameButtWrapMode', 'doorLeafFrameJoinType',
    'doorLeafFrameButtWrapMode', 'assemblyClearance', 'horizontalBranchReserve',
    'verticalBranchReserve', 'horizontalEndConnection', 'verticalEndConnection',
    'outerFrameBendKFactor', 'outerFrameFoldBridge', 'outerFrameVGrooveMaleFemale',
    'outerFrameVGrooveBottomStrategy', 'outerFrameVGrooveRoundRadius',
    'doorFrameBendKFactor', 'doorFrameFoldBridge', 'doorFrameVGrooveMaleFemale',
    'doorFrameVGrooveBottomStrategy', 'doorFrameVGrooveRoundRadius',
    'doorLeafFrameBendKFactor', 'doorLeafFrameFoldBridge', 'doorLeafFrameVGrooveMaleFemale',
    'doorLeafFrameVGrooveBottomStrategy', 'doorLeafFrameVGrooveRoundRadius',
}
_JOIN_CHOICES = {'miter_45', 'butt_90', 'v_groove_90:tool_library'}
_WRAP_CHOICES = {'side_wraps_horizontal', 'horizontal_wraps_side'}


def _actual_profile(profile):
    """Apply the component's exact section frame; never rebuild its contours."""
    result=deepcopy(profile)
    mapping=result.pop('sectionCoordinateMap', [[1.,0.],[0.,1.]])
    if mapping not in ([[1.,0.],[0.,1.]], [[1.,0.],[0.,-1.]],
                       [[0.,1.],[1.,0.]], [[0.,1.],[-1.,0.]]):
        raise ValueError('invalid window tube section coordinate map')
    contours=deepcopy(result['contours'])
    if mapping[0]==[0.,1.]:
        contours=[_load('tube_profile_catalog')._swap_contour_axes(contour) for contour in contours]
    if mapping[1] in ([0.,-1.],[-1.,0.]):
        contours=[_load('security_window_machining_adapter')._reflect_contour(contour) for contour in contours]
    result['contours']=contours
    return result


def _restore_design(items, resources, connections):
    sections = {node['key']: node for node in resources}
    source = []
    for item in items:
        data = item.get('designInput', {})
        if data.get('kind') != 'tube-member':
            raise ValueError('window assembly requires stable tube-member design inputs')
        profile = deepcopy(item['properties']['tubeDesigner.profile'])
        section = sections[data['profileResource']]
        if section['operator'] != 'profile2d':
            raise ValueError('window assembly requires an actual section resource')
        profile['contours'] = deepcopy(section['arguments']['contours'])
        start, end, frame = data['start'], data['end'], data['sectionFrame']
        properties = deepcopy(item['properties'])
        properties['tubeDesigner.profile'] = profile
        properties['assemblyFrame.member'] = {
            'start': deepcopy(start), 'end': deepcopy(end), 'axisLength': math.dist(start, end),
            'sectionFrame': {'originAtStart': deepcopy(frame['origin']),
                             **{name: deepcopy(frame[name]) for name in ('xAxis', 'yAxis', 'zAxis')},
                             'centerlineUV': [0., 0.]}, 'stockState': 'design',
        }
        source.append({'key': item['key'], 'displayName': item['displayName'],
                       'children': [], 'properties': properties})
    return {'items': source, 'relationships': deepcopy(connections)}


def plan(call, inputs, resources, context=None):
    """Plan one complete window using the registered v3 process contract."""
    from icax_template_sdk.manufacturing import is_manufacturing_declaration, validate_assembly_resource_ref
    if is_manufacturing_declaration():
        raise RuntimeError('产品声明阶段禁止调用整体装配工艺规划')
    if call['templateId'] != 'security-window-assembly' or set(inputs) != {'members'}:
        raise ValueError('invalid registered security-window-assembly input roles')
    if set(call['outputs']) != {'manufacturing'}:
        raise ValueError('security-window-assembly has one manufacturing-set output role')
    members = inputs['members']
    if members.get('kind') != 'design-set' or not members.get('items'):
        raise ValueError('security-window-assembly requires a design-set input')
    local = call['processInput']
    if local.get('schemaVersion') != 3 or set(local) - {'schema', 'schemaVersion', 'parts', 'geometry', 'resources'}:
        raise ValueError('invalid whole-window assembly process input')
    geometry = local['geometry']
    if (not {'layout', 'connections'} <= set(geometry)
            or set(geometry) - {'layout', 'connections', 'mergeIdenticalParts'}
            or set(geometry['layout']) != {'faceType', 'accessDoorEnabled'}
            or 'mergeIdenticalParts' in geometry and not isinstance(geometry['mergeIdenticalParts'], bool)):
        raise ValueError('unknown whole-window assembly design facts')
    layout = geometry['layout']
    if layout['faceType'] not in {'single', 'two', 'three', 'five'} or not isinstance(layout['accessDoorEnabled'], bool):
        raise ValueError('invalid window assembly design layout')
    selected = deepcopy(call['parameters'])
    post_runtime = _load('security_window_post_connections')
    if set(selected) != _CHOICE_FIELDS | set(post_runtime.POST_DEFAULTS):
        raise ValueError('window assembly requires its explicit local choices and rejects unknown parameters')
    if selected['assemblyPlanningMode'] not in {'builtin_rules','external_templates'}:
        raise ValueError('unsupported window assembly planning mode')
    if selected['frameManufacturingMode'] not in {'segment_weld', 'plane_v_notch', 'spatial_v_notch'}:
        raise ValueError('unsupported window assembly route')
    if selected['frameCornerJoin'] not in {'post_butt', 'rail_miter'}:
        raise ValueError('unsupported window frame corner connection')
    for key in ('frameJoinType', 'doorFrameJoinType', 'doorLeafFrameJoinType'):
        if selected[key] not in _JOIN_CHOICES:
            raise ValueError('unsupported window assembly connection: ' + key)
    for key in ('frameButtWrapMode', 'doorFrameButtWrapMode', 'doorLeafFrameButtWrapMode'):
        if selected[key] not in _WRAP_CHOICES:
            raise ValueError('unsupported window assembly wrap mode: ' + key)
    for key in ('horizontalEndConnection', 'verticalEndConnection'):
        if selected[key] not in {'insert', 'weld', 'tabs'}:
            raise ValueError('unsupported grid end connection: ' + key)
    for key in ('assemblyClearance', 'horizontalBranchReserve', 'verticalBranchReserve'):
        value = selected[key]
        if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or value < 0.:
            raise ValueError('invalid local window assembly dimension: ' + key)
    selected.update(deepcopy(layout))
    keys = [item['key'] for item in members['items']]
    if len(keys) != len(set(keys)):
        raise ValueError('window assembly design member identities must be independent')
    roles = []
    if selected['assemblyPlanningMode']=='builtin_rules' and selected['frameManufacturingMode'] != 'segment_weld':
        roles.append('outerFrameGroove')
    for prefix, option, role in (
        ('access_door.fixed_frame.', 'doorFrameJoinType', 'doorFrameGroove'),
        ('access_door.leaf.frame.', 'doorLeafFrameJoinType', 'doorLeafFrameGroove'),
    ):
        if any(key.startswith(prefix) for key in keys) and selected[option] == 'v_groove_90:tool_library':
            roles.append(role)
    slots, drafts = local.get('resources', {}), call['processDrafts']
    if set(slots) != set(roles) or set(drafts) != set(roles):
        raise ValueError('window assembly resource slots and drafts must exactly match the selected roles')
    bindings = {}
    for role in roles:
        slot = slots[role]
        if not isinstance(slot, dict) or set(slot) != {'ref'}:
            raise ValueError('invalid selected window assembly resource slot')
        reference = slot['ref']
        validate_assembly_resource_ref(reference)
        if reference['scope'] not in {'system', 'user'}:
            raise ValueError('invalid selected window assembly tool reference')
        role_drafts = drafts[role]
        if not isinstance(role_drafts, dict) or set(role_drafts) != {reference['id']} or not isinstance(role_drafts[reference['id']], dict):
            raise ValueError('window assembly requires the explicitly selected resource draft')
        bindings[role] = {'ref': deepcopy(reference), 'parameters': deepcopy(role_drafts[reference['id']])}
    selected['tubeDesignerToolBindings'] = bindings
    # Determine applicability from the actual frozen selected resource, then
    # forward only the active joint's values to geometry.
    selected.update(post_runtime.process_choices(selected))
    for relation in geometry['connections']:
        if (not isinstance(relation, dict) or set(relation) != {'key', 'kind', 'items', 'properties'}
                or not set(relation['items']) <= set(keys)
                or set(relation['properties']) - {'topology', 'centerlinePoint', 'origin', 'participantAnchors'}):
            raise ValueError('window assembly connections must reference the declared design members')
    design = _restore_design(members['items'], resources, geometry['connections'])
    return {'definition': _compile_route(selected, design), 'sourceItemKeys': keys}


def compile_window_assembly(document, process=None):
    """Independent entry for inspecting the registered planner's private input."""
    from icax_template_sdk.manufacturing import validate_manufacturing_model
    validate_manufacturing_model(document)
    process = document['processes'][0] if process is None else process
    call = process['definition']
    binding = call['processInput']['parts']['members']
    if binding.get('scope') != 'design':
        raise ValueError('window planner inspection requires explicit design inputs')
    by_key = {item['key']: item for item in document['items']}
    inputs = {'members': {'kind': 'design-set', 'items': [deepcopy(by_key[key]) for key in binding['itemKeys']],
                         'sourceMappings': deepcopy(document['sourceMappings'])}}
    return plan(call, inputs, document['resources'])['definition']


def _compile_route(parameters, design):
    """Compile a private v2 allocation input using only the declared design."""
    design = deepcopy(design)
    source = {item['key']: item for item in design['items']}
    members = {key: item['properties']['assemblyFrame.member'] for key, item in source.items()}
    original_profiles = {key: _clean_profile(item['properties']['tubeDesigner.profile']) for key, item in source.items()}
    profiles = {key: _actual_profile(profile) for key, profile in original_profiles.items()}
    resources, resource_keys = [], {}
    def section(key):
        contours = profiles[key]['contours']
        identity = json.dumps(contours, sort_keys=True, separators=(',', ':'))
        if identity not in resource_keys:
            name = 'section.' + hashlib.sha256(identity.encode()).hexdigest()[:24]
            resource_keys[identity] = name
            resources.append({'key': name, 'operator': 'profile2d', 'inputs': [],
                              'arguments': {'contours': deepcopy(contours)}})
        return resource_keys[identity]
    objects, mappings, processes, owners = {}, {}, [], {}
    def add_object(key, label, spans, closed=False, tool_role=None):
        first = spans[0][0]
        if tool_role:
            if any(original_profiles[original]['contours'] != original_profiles[first]['contours']
                   for original, _, _ in spans):
                raise ValueError('连续折弯的一根原管不能使用不同管材；请统一该路径的成品管材或选择分段拼接')
            _load('security_window_frame_geometry')._validate_bending_section(_profile(original_profiles[first]))
            if (tool_role=='outerFrameGroove' and parameters.get('faceType','single')!='single'
                    and parameters.get('frameManufacturingMode')=='spatial_v_notch'
                    and abs(original_profiles[first]['width']-original_profiles[first]['depth'])>1.e-6):
                raise ValueError('空间连续外框目前需要宽深相等的闭口方管；矩形管可选平面折弯')
        profile = deepcopy(profiles[first]); profile['sectionResource'] = section(first)
        vertices, segments, mapping = [], [], {}
        for index, (original, start, end) in enumerate(spans):
            vertex = 'vertex.' + str(index)
            vertices.append({'key': vertex, 'point': list(start)})
            next_vertex = 'vertex.' + str((index+1) % len(spans)) if closed else 'vertex.' + str(index+1)
            segment = 'segment.' + str(index)
            segments.append({'key': segment, 'from': vertex, 'to': next_vertex,
                             'sectionFrame': _section_frame(members[original], start, end)})
            m = members[original]; direction = sub(m['end'], m['start']); squared = dot(direction, direction)
            interval = [dot(sub(point, m['start']), direction)/squared for point in (start, end)]
            if any(value < -1.e-6 or value > 1+1.e-6 for value in interval):
                raise ValueError('制造路径来源超出成品构件: ' + original)
            mapping.setdefault(original, []).append({'segmentKey': segment,
                'sourceRange': [min(1., max(0., value)) for value in interval]})
            owners[original] = key
        if not closed: vertices.append({'key': 'vertex.' + str(len(spans)), 'point': list(spans[-1][2])})
        properties = {name: deepcopy(source[first]['properties'][name]) for name in (
            'partNumber', 'group', 'manufacturing.material', 'manufacturing.materialGrade',
            'manufacturing.materialCategory', 'manufacturing.categoryKey', 'manufacturing.categoryName',
            'material', 'materialGrade')
                      if name in source[first]['properties']}
        if tool_role:
            category = 'outer_frame.continuous' if key.startswith('outer_frame.') else key.rsplit('.', 1)[0]
            properties.update({'manufacturing.categoryKey': category,
                               'manufacturing.categoryName': label})
        elif (parameters.get('faceType','single') in ('two','three')
              and parameters.get('frameManufacturingMode','segment_weld')=='spatial_v_notch'
              and key.startswith('outer_frame.vertical.')):
            properties.update({'manufacturing.categoryKey':'outer_frame.shared',
                               'manufacturing.categoryName':'共享连接杆'})
        properties.update(quantity=1, **{'manufacturing.partKind': 'tube', 'manufacturing.sourcing': 'made',
                                       'tubeDesigner.profile': profile,
                                       'tubeDesigner.sourceMemberProfiles': {original:deepcopy(profiles[original])
                                                                           for original in mapping}})
        path = {'vertices': vertices, 'segments': segments, 'closed': closed, 'startVertex': 'vertex.0'}
        if closed: path['closure'] = 'straight-mid-edge-seam'
        input_data = {'kind': 'tube-path', 'profileResource': section(first), 'path': path}
        objects[key] = {'key': key, 'displayName': label, 'children': [], 'properties': properties,
                        'manufacturingInput': input_data}
        mappings[key] = {'itemKey': key, 'sources': [{'itemKey': original, 'segments': entries}
                                                   for original, entries in mapping.items()]}
        if not tool_role: return
        binding = parameters.get('tubeDesignerToolBindings', {}).get(tool_role)
        if binding is None:
            encoded = parameters.get(tool_role + 'Tool', 'system:v-notch-sharp')
            scope, separator, identifier = encoded.partition(':')
            if not separator: raise ValueError('折弯模具引用无效')
            reference, draft = {'scope': scope, 'id': identifier}, {}
        else:
            reference, draft = deepcopy(binding['ref']), deepcopy(binding.get('parameters', {}))
        templates = {'v-notch-sharp': 'node-v-notch-integrated', 'edge-arc-groove': 'node-edge-arc-integrated',
            'embedded-arc-notch': 'node-embedded-arc-integrated', 'segmented-bend': 'segmented-bend',
            'flexible-slit-bend': 'flexible-slit-bend-integrated'}
        for index in range(len(segments)-1):
            incoming, outgoing = segments[index:index+2]
            a, b = incoming['sectionFrame']['zAxis'], outgoing['sectionFrame']['zAxis']
            cosine = max(-1., min(1., dot(a, b)))
            if abs(cosine-1.) < 1.e-7: continue
            vertex = incoming['to']
            processes.append(_call(key+'.fold.'+vertex, templates.get(reference['id'], 'node-v-notch-integrated'),
                {'stock': {'scope': 'manufacturing', 'itemKey': key, 'state': 'initial'}},
                {'anchor': {'kind': 'path-corner', 'vertexKey': vertex,
                            'incomingSegment': incoming['key'], 'outgoingSegment': outgoing['key']},
                 'angle': math.degrees(math.acos(cosine)), 'axisDirection': unit(cross(a, b))},
                resources={'node-slot': {'ref': reference}}, drafts={'node-slot': {reference['id']: draft}},
                targets={'stock': key}))
        # Shop order is chosen after resolving the actual cutters and hinges.
    face = parameters.get('faceType', 'single')
    mode = parameters.get('frameManufacturingMode', 'segment_weld')
    post_runtime = _load('security_window_post_connections')
    segment_post_insertion = (mode == 'segment_weld'
                              and parameters.get('foldedPostJoint', 'weld') in ('insert', 'tabs')
                              and post_runtime.active_route(parameters))
    if segment_post_insertion:
        # This is a local layout decision from the selected interface, never
        # a rewrite of the product's retained corner/wrap drafts.
        if face != 'single':
            parameters['frameCornerJoin'] = 'rail_miter'
        else:
            if not all(any(key.startswith('outer_frame.' + side + '.') for key in members)
                       for side in ('left', 'right', 'bottom', 'top')):
                raise ValueError('拼接外框立柱插接需要完整四边框')
            parameters['frameJoinType'] = 'butt_90'
            parameters['frameButtWrapMode'] = 'horizontal_wraps_side'
    if parameters.get('assemblyPlanningMode')=='external_templates':
        if face!='single' or parameters.get('accessDoorEnabled',False):
            raise ValueError('独立节点装配目前仅支持无开启口单面窗')
        if mode!='segment_weld':
            raise ValueError('独立节点装配未选择连续折弯路线')
        if any(key.startswith('main_grid.horizontal.') for key in members) and any(
                key.startswith('main_grid.vertical.') for key in members):
            raise ValueError('独立节点装配目前不支持横竖交叉格栅')
        if not all(any(key.startswith('outer_frame.'+side+'.') for key in members)
                   for side in ('left','right','bottom','top')):
            raise ValueError('独立节点装配需要完整四边框')
    consumed = set()
    def rectangle(prefix, role):
        sides = {side: next((key for key in source if key.startswith(prefix+'.'+side+'.')), None)
                 for side in ('left', 'right', 'bottom', 'top')}
        if not all(sides.values()): return
        left, right, bottom, top = (members[sides[side]] for side in ('left', 'right', 'bottom', 'top'))
        # Logical frame members already meet at the centreline vertices.
        bl, br = bottom['start'], bottom['end']; tl, tr = top['start'], top['end']
        midpoint = [(a+b)/2 for a,b in zip(bl,br)]
        spans = [(sides['bottom'], midpoint, br), (sides['right'], br, tr),
                 (sides['top'], tr, tl), (sides['left'], tl, bl), (sides['bottom'], bl, midpoint)]
        key = prefix+'.continuous.0001'
        label = source[sides['bottom']]['displayName'].removesuffix('下边')
        add_object(key, label, spans, True, role)
        consumed.update(sides.values())
    if face == 'single' and mode != 'segment_weld': rectangle('outer_frame', 'outerFrameGroove')
    if face != 'single' and mode != 'segment_weld':
        parts = []
        top_keys = sorted(key for key in source if key.startswith('outer_frame.top.'))
        for key, member in members.items():
            if not key.startswith('outer_frame.'): continue
            index = int(key.rsplit('.',1)[1])
            parts.append(SimpleNamespace(key=key, name=source[key]['displayName'], profile=_profile(original_profiles[key]),
                start=tuple(member['start']), end=tuple(member['end']), face_index=(4 if index==1 else 5)
                if key.startswith('outer_frame.back.') else index))
        points = [tuple(members[top_keys[0]]['start'][:2]), *[tuple(members[key]['end'][:2]) for key in top_keys]]
        height = members[top_keys[0]]['start'][2] + original_profiles[top_keys[0]]['width']/2
        paths = _load('security_window_frame_paths').plan(parts, points,
            {'frameManufacturingMode': mode, 'faceType': face, 'height': height}, face+'-face')
        for path in paths:
            spans = [(span.reference.key, list(span.start), list(span.end)) for span in path.spans]
            add_object(path.key, path.name, spans, path.closed, 'outerFrameGroove')
            consumed.update(span.reference.key for span in path.spans)
    if parameters.get('accessDoorEnabled', False):
        for prefix, option, role in (('access_door.fixed_frame', 'doorFrameJoinType', 'doorFrameGroove'),
                                      ('access_door.leaf.frame', 'doorLeafFrameJoinType', 'doorLeafFrameGroove')):
            if parameters.get(option) == 'v_groove_90:tool_library': rectangle(prefix, role)
    for key, member in members.items():
        if key not in consumed:
            add_object(key, source[key]['displayName'], [(key, member['start'], member['end'])])
    if face != 'single' and mode != 'segment_weld':
        for relation in design.get('relationships', []):
            if relation.get('properties', {}).get('topology') != 'orthogonal-corner': continue
            a,b,c = relation['items']
            if owners[a] == a or owners[b] == b: continue
            parts = {role:{'scope':'manufacturing','itemKey':owners[key],'state':'initial',
                           'data':{'sourceMember':key}}
                     for role,key in (('memberA',a),('memberB',b))}
            parts['memberC']={'scope':'manufacturing','itemKey':owners[c],'state':'initial'}
            processes.append(_call(relation['key']+'.post', 'orthogonal-corner', parts,
                {'designNode':deepcopy(relation['properties']['centerlinePoint'])},
                _load('security_window_post_connections').node_parameters(parameters),
                targets={'memberA':owners[a],'memberB':owners[b],'memberC':owners[c]}))
    horizontal_limits = {}
    post_runtime = _load('security_window_post_connections')
    if post_runtime.active_route(parameters) and parameters.get('foldedPostJoint','weld') != 'weld':
        def real_section(key):
            member = members[key]
            frame = member['sectionFrame']
            return {'placement': {'origin': list(frame['originAtStart']), 'xAxis': list(frame['xAxis']),
                                  'yAxis': list(frame['yAxis'])}, 'contours': deepcopy(profiles[key]['contours'])}
        branches = [{'key': key, 'start': member['start'], 'end': member['end'],
                     'section': real_section(key)} for key,member in members.items()
                    if key.startswith('main_grid.horizontal.')]
        posts = sorted(key for key in members if key.startswith('outer_frame.vertical.'))
        shared_posts = posts if face=='five' or segment_post_insertion else posts[1:-1]
        for post in shared_posts:
            horizontal_limits.update(post_runtime.shared_post_horizontal_limits(
                {'start': members[post]['start'], 'end': members[post]['end'],
                 'wallThickness': profiles[post]['wallThickness'], 'section': real_section(post)},
                branches, float(parameters.get('horizontalBranchReserve',0.)),
                float(parameters.get('assemblyClearance',0.))))
    def end_call(key, end, mate, mode, depth=0.):
        if owners[key] != key: return
        if mode=='insert' and (key,end) in horizontal_limits:
            depth=min(depth,horizontal_limits[(key,end)]['depth'])
        point = members[key][end]
        processes.append(_call(key+'.end.'+end, 'tube-end-joint',
            {'stock': {'scope': 'manufacturing', 'itemKey': key, 'state': 'initial'},
             'mate': {'scope': 'display', 'itemKey': mate}},
            {'mode': mode, 'anchor': {'kind': 'member-end', 'end': end, 'point': deepcopy(point)}},
            {'insertionDepth': depth, 'clearance': float(parameters.get('assemblyClearance', 0.)) if mode=='insert' else 0.}, targets={'stock': key}))
    def material_boundary(key, original, end, mate, mode, extent_policy=None):
        # These are design references. The downstream end function determines
        # the material endpoint; no length, reserve or cutting plane is solved here.
        geometry={'mode':mode, 'allocation':'initial-material-boundary',
                  'anchor':{'kind':'member-end','end':end,'point':deepcopy(members[original][end])}}
        if extent_policy is not None: geometry['extentPolicy']=extent_policy
        processes.append(_call(key+'.material-boundary.'+end, 'tube-end-joint',
            {'stock':{'scope':'manufacturing','itemKey':key,'state':'initial',
                      'data':{'sourceMember':original}},
             'mate':{'scope':'display','itemKey':mate}},
            geometry,
            {}, targets={'stock':key}))
    def compiled():
        declared = _declare_material_dependencies(processes, owners, set(objects))
        return {'schema':'icax.manufacturing-model','schemaVersion':2,
            'coordinateSystem':'right-handed-x-width-y-depth-z-height','lengthUnit':'mm','resources':resources,
            'items':list(objects.values()),'roots':list(objects),'sourceMappings':list(mappings.values()),'processes':declared}
    post_ends = set()
    terminal_receivers = set()
    if segment_post_insertion:
        posts = sorted(key for key in members if key.startswith('outer_frame.vertical.')) if face != 'single' else [
            key for key in members if key.startswith(('outer_frame.left.', 'outer_frame.right.'))]
        # Every structurally independent post receives the selected end joint.
        # A post using the frame's material still needs the same machining;
        # its material-choice marker cannot make an active joint disappear.
        values = post_runtime.insertion_parameters(parameters)
        for post in posts:
            for end in ('start', 'end'):
                point = members[post][end]
                receivers = sorted(key for key in members
                    if key.startswith(('outer_frame.top.', 'outer_frame.bottom.', 'outer_frame.back.'))
                    and min(math.dist(point, members[key][side]) for side in ('start', 'end')) < 1.e-6)
                if not 1 <= len(receivers) <= 2:
                    raise ValueError('立柱端插接需要一个真实承接梁或两根45度拼角梁')
                processes.append(_call(post + '.post-end.' + end, 'tube-post-end',
                    {'stock': {'scope': 'manufacturing', 'itemKey': post, 'state': 'initial'},
                     'mate': {'scope': 'display', 'itemKey': receivers[0]}},
                    {'anchor': {'kind': 'member-end', 'end': end, 'point': deepcopy(point)}},
                    values, targets={'stock': post}))
                post_ends.add((post, end))
                for receiver in receivers:
                    processes.append(_call(receiver + '.post-aperture.' + post + '.' + end, 'tube-post-aperture',
                        {'stock': {'scope': 'manufacturing', 'itemKey': receiver, 'state': 'initial'},
                         'branch': {'scope': 'display', 'itemKey': post}},
                        {'node': deepcopy(point), 'branchEnd': end, 'sharedCorner': len(receivers) == 2},
                        values, targets={'stock': receiver}))
                    if len(receivers) == 1:
                        receiver_end = min(('start', 'end'), key=lambda side: math.dist(members[receiver][side], point))
                        terminal_receivers.add((receiver, receiver_end))
                        material_boundary(receiver, receiver, receiver_end, post, 'wrap', 'nominal-member-section')
    if parameters.get('assemblyPlanningMode','builtin_rules')=='external_templates':
        frame_keys=[key for key in members if key.startswith('outer_frame.')]
        for key,member in members.items():
            objects[key]['properties']['tubeDesigner.assemblyPlanning']={'stockState':'uncut','ready':False}
            if key.startswith('outer_frame.'):
                # The factory's pending set keeps its established broad
                # classification; the shared design category stays intrinsic.
                objects[key]['properties']['manufacturing.categoryKey']='outer_frame'
                objects[key]['properties']['manufacturing.categoryName']='外框'
            axis=unit(sub(member['end'],member['start']))
            for end in ('start','end'):
                point=member[end]; candidates=[]
                for mate in frame_keys:
                    if mate==key: continue
                    m=members[mate]; direction=unit(sub(m['end'],m['start']))
                    if abs(dot(axis,direction))>1.e-7: continue
                    station=dot(sub(point,m['start']),direction)
                    if not -1.e-6<=station<=m['axisLength']+1.e-6: continue
                    nearest=[m['start'][i]+station*direction[i] for i in range(3)]
                    candidates.append((math.dist(point,nearest),mate))
                if not candidates: raise ValueError('独立节点装配缺少管端设计接收构件: '+key)
                material_boundary(key,key,end,min(candidates)[1],'wrap')
        return compiled()
    if face != 'single':
        selected = _load('security_window_frame_paths').mode(parameters)
        posts = sorted(key for key in members if key.startswith('outer_frame.vertical.'))
        full_posts = (posts if selected=='segment_weld' and parameters.get('frameCornerJoin','post_butt')=='post_butt'
                      else [posts[0],posts[-1]] if selected=='plane_v_notch' and face!='five' else [])
        for post in full_posts:
            for end, side in (('start','bottom'),('end','top')):
                point=members[post][end]
                mate=min((key for key in members if key.startswith('outer_frame.'+side+'.')),
                         key=lambda key:min(math.dist(point,members[key][e]) for e in ('start','end')))
                material_boundary(post,post,end,mate,'wrap','nominal-member-section')
        if selected=='plane_v_notch' and face!='five':
            for key,item in objects.items():
                if not key.startswith('outer_frame.plane.'): continue
                entries=mappings[key]['sources']
                first=next(entry['itemKey'] for entry in entries
                           if any(segment['segmentKey']=='segment.0' for segment in entry['segments']))
                last_segment='segment.'+str(len(item['manufacturingInput']['path']['segments'])-1)
                last=next(entry['itemKey'] for entry in entries
                          if any(segment['segmentKey']==last_segment for segment in entry['segments']))
                for end,original in (('start',first),('end',last)):
                    point=members[original][end]
                    mate=min(posts,key=lambda post: min(math.dist(point,members[post][e]) for e in ('start','end')))
                    material_boundary(key,original,end,mate,'butt')
    frame_keys = [key for key in members if key.startswith(('outer_frame.', 'access_door.fixed_frame.', 'access_door.leaf.frame.'))]
    for key in frame_keys:
        if owners[key] != key: continue
        join = parameters.get('frameJoinType', 'miter_45') if face=='single' else (
            'miter_45' if parameters.get('frameCornerJoin')=='rail_miter' else 'butt_90')
        if key.startswith('outer_frame.') and face=='single' and join=='v_groove_90:tool_library':
            join='miter_45'
        wrap = parameters.get('frameButtWrapMode', 'side_wraps_horizontal')
        if key.startswith('access_door.fixed_frame.'):
            join, wrap = parameters.get('doorFrameJoinType','butt_90'), parameters.get('doorFrameButtWrapMode','side_wraps_horizontal')
        if key.startswith('access_door.leaf.frame.'):
            join, wrap = parameters.get('doorLeafFrameJoinType','butt_90'), parameters.get('doorLeafFrameButtWrapMode','side_wraps_horizontal')
        for end in ('start','end'):
            if (key, end) in post_ends or (key, end) in terminal_receivers:
                continue
            point = members[key][end]; axis = unit(sub(members[key]['end'], members[key]['start']))
            candidates = [(min(math.dist(point,members[other][e]) for e in ('start','end')), other)
                for other in frame_keys if other!=key and source[other]['properties'].get('group')==source[key]['properties'].get('group')
                and abs(dot(axis, unit(sub(members[other]['end'],members[other]['start'])))) < 1.e-7]
            if not candidates: continue
            distance, mate = min(candidates)
            if distance > 1.e-5: continue
            horizontal = ('.bottom.' in key or '.top.' in key) if key.startswith('access_door.') else abs(axis[2]) < .5
            if face!='single' and key.startswith('outer_frame.'):
                if key.startswith('outer_frame.vertical.'):
                    if join=='miter_45' and mode=='segment_weld': end_call(key,end,mate,'butt')
                    continue
                other_rails=[value for value in candidates if not value[1].startswith('outer_frame.vertical.') and value[0]<1.e-5]
                if join=='miter_45' and other_rails:
                    end_call(key,end,min(other_rails)[1],'miter')
                else:
                    posts=[value for value in candidates if value[1].startswith('outer_frame.vertical.')]
                    if posts: end_call(key,end,min(posts)[1],'wrap' if join=='miter_45' else 'butt')
            elif join=='miter_45': end_call(key,end,mate,'miter')
            elif horizontal == (wrap=='side_wraps_horizontal'): end_call(key,end,mate,'butt')
            else: material_boundary(key,key,end,mate,'wrap')
    closed_endpoint_pairs = set()
    for key in members:
        if key in frame_keys: continue
        axis = unit(sub(members[key]['end'],members[key]['start']))
        for end in ('start','end'):
            point=members[key][end]; candidates=[]
            for other in frame_keys:
                if key.startswith('access_door.leaf.') != other.startswith('access_door.leaf.frame.'): continue
                m=members[other]; direction=unit(sub(m['end'],m['start']))
                if abs(dot(axis,direction))>1.e-7: continue
                station=dot(sub(point,m['start']),direction)
                if not -1.e-6<=station<=math.dist(m['start'],m['end'])+1.e-6: continue
                closest=[m['start'][i]+station*direction[i] for i in range(3)]
                distance=math.dist(point,closest)
                if distance<=max(profiles[other]['width'],profiles[other]['depth'])/2+1.e-6: candidates.append((distance,other))
            if candidates:
                _, mate=min(candidates)
                is_grid = key.startswith(('main_grid.', 'side_grid.', 'cap_grid.'))
                category = source[key]['properties'].get('manufacturing.categoryKey', '')
                horizontal = category.endswith('horizontal')
                if is_grid:
                    selected = parameters['horizontalEndConnection' if horizontal else 'verticalEndConnection']
                    if selected == 'tabs':
                        values = {'joint':'tabs', 'insertionDepth':12. if horizontal else 8.,
                                  'tabWidth':8. if horizontal else 6.,
                                  'clearance':float(parameters.get('assemblyClearance',0.))}
                        # Declare enough real raw material independently of the
                        # frozen display end. The end tool alone retains its ears.
                        material_boundary(key,key,end,mate,'wrap')
                        processes.append(_call(key+'.grid-tab-end.'+end, 'tube-post-end',
                            {'stock':{'scope':'manufacturing','itemKey':key,'state':'initial'},
                             'mate':{'scope':'display','itemKey':mate}},
                            {'anchor':{'kind':'member-end','end':end,'point':deepcopy(point)}},
                            values,targets={'stock':key}))
                        target=owners[mate]
                        processes.append(_call(target+'.grid-tab-aperture.'+key+'.'+end, 'tube-post-aperture',
                            {'stock':{'scope':'manufacturing','itemKey':target,'state':'initial'},
                             'branch':{'scope':'display','itemKey':key}},
                            {'node':deepcopy(point),'branchEnd':end,'sharedCorner':False,'receiverSource':mate},
                            values,targets={'stock':target}))
                        closed_endpoint_pairs.add(frozenset((key,mate)))
                        continue
                    mode_,depth=('butt',0.) if selected=='weld' else (
                        'insert',parameters.get('horizontalBranchReserve' if horizontal else 'verticalBranchReserve',0.))
                    if selected=='weld': closed_endpoint_pairs.add(frozenset((key,mate)))
                elif key.startswith('access_door.leaf.horizontal.') or mate.startswith('access_door.fixed_frame.'):
                    mode_,depth='butt',0.
                    closed_endpoint_pairs.add(frozenset((key,mate)))
                elif key.startswith('access_door.leaf.vertical.'):
                    # The leaf owns this connection. A main-grid end choice
                    # cannot change its independently designed insertion.
                    mode_,depth='insert','mid-section'
                else:
                    # The cap grid's vertical members lie in a horizontal face;
                    # insertion allowance follows the member role, not world Z.
                    reserve = 'horizontalBranchReserve' if category.endswith('horizontal') else 'verticalBranchReserve'
                    mode_,depth='insert',parameters.get(reserve,0.)
                end_call(key,end,mate,mode_,depth)
    # Contact observations are pure design data; receiving the section remains a downstream function.
    keys=list(members)
    for index,a in enumerate(keys):
        for b in keys[index+1:]:
            if frozenset((a,b)) in closed_endpoint_pairs: continue
            if a in frame_keys and b in frame_keys: continue
            if a.startswith('access_door.leaf.') != b.startswith('access_door.leaf.'): continue
            ma,mb=members[a],members[b]; u,v=unit(sub(ma['end'],ma['start'])),unit(sub(mb['end'],mb['start']))
            if abs(dot(u,v))>1.e-7: continue
            delta=sub(mb['start'],ma['start']); sa=dot(delta,u); sb=-dot(delta,v)
            pa=[ma['start'][i]+sa*u[i] for i in range(3)]; pb=[mb['start'][i]+sb*v[i] for i in range(3)]
            if math.dist(pa,pb)>1.e-6: continue
            margins=[max(profiles[key]['width'],profiles[key]['depth'])/2 for key in (a,b)]
            # The other member's section can reach this member's finite end.
            # A branch ending inside a receiver's wall need not reach its axis.
            if not -margins[1]+1.e-7<sa<math.dist(ma['start'],ma['end'])+margins[1]-1.e-7: continue
            if not -margins[0]+1.e-7<sb<math.dist(mb['start'],mb['end'])+margins[0]-1.e-7: continue
            receiver,branch=(a,b) if margins[0]>=margins[1] else (b,a)
            if receiver not in frame_keys and branch in frame_keys: receiver,branch=branch,receiver
            target=owners[receiver]
            processes.append(_call(target+'.aperture.'+receiver+'.'+branch, 'tube-profile-aperture',
                {'stock': {'scope':'manufacturing','itemKey':target,'state':'initial'},
                 'branch': {'scope':'display','itemKey':branch}},
                {'receiverSource':receiver}, {'clearance':float(parameters.get('assemblyClearance',0.)),'checkFit':True},
                targets={'stock':target}))
    return compiled()
