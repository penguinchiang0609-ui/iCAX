"""Connections and one assembly request over the display-owned base items.

Manufacturing references stable design identities. It neither publishes a
second base model nor splits, merges or routes those items.
"""
from copy import deepcopy
import hashlib
import importlib.util
from pathlib import Path
import sys
import math
from types import SimpleNamespace


def _load(name):
    path = Path(__file__).with_name(name + '.py')
    key = 'icax_window_design_' + name + '_' + hashlib.sha256(path.read_bytes()).hexdigest()[:16]
    if key not in sys.modules:
        spec = importlib.util.spec_from_file_location(key, path)
        module = importlib.util.module_from_spec(spec)
        sys.modules[key] = module
        spec.loader.exec_module(module)
    return sys.modules[key]


_PROCESS_DEFAULTS = {
    'assemblyPlanningMode': 'builtin_rules',
    'frameManufacturingMode': 'segment_weld',
    'frameJoinType': 'miter_45',
    'frameCornerJoin': 'post_butt',
    'frameButtWrapMode': 'side_wraps_horizontal',
    'doorFrameJoinType': 'butt_90',
    'doorFrameButtWrapMode': 'side_wraps_horizontal',
    'doorLeafFrameJoinType': 'butt_90',
    'doorLeafFrameButtWrapMode': 'side_wraps_horizontal',
    'assemblyClearance': 0.0,
    'horizontalBranchReserve': 0.0,
    'verticalBranchReserve': 0.0,
    'horizontalEndConnection': 'insert',
    'verticalEndConnection': 'insert',
    'outerFrameBendKFactor': 0.0,
    'outerFrameFoldBridge': 1.0,
    'outerFrameVGrooveMaleFemale': True,
    'outerFrameVGrooveBottomStrategy': 'sharp',
    'outerFrameVGrooveRoundRadius': 2.0,
    'doorFrameBendKFactor': 0.0,
    'doorFrameFoldBridge': 1.0,
    'doorFrameVGrooveMaleFemale': True,
    'doorFrameVGrooveBottomStrategy': 'sharp',
    'doorFrameVGrooveRoundRadius': 2.0,
    'doorLeafFrameBendKFactor': 0.0,
    'doorLeafFrameFoldBridge': 1.0,
    'doorLeafFrameVGrooveMaleFemale': True,
    'doorLeafFrameVGrooveBottomStrategy': 'sharp',
    'doorLeafFrameVGrooveRoundRadius': 2.0,
}


def _external_connections(design, core):
    """Declare the original pending L/T nodes over stable display identities.

    A node's centreline contact can be outside the finished branch's extent.
    These anchors describe that pending connection, without making a blank or
    choosing a cut. Section support at the contact supplies the old allowance.
    """
    items={item['key']:item for item in design['items']}
    if not all('outer_frame.'+side+'.0001' in items for side in ('left','right','bottom','top')):
        raise ValueError('独立节点装配需要完整四边框')
    parts={}
    for key,item in items.items():
        member=item['properties']['assemblyFrame.member']
        parts[key]=SimpleNamespace(key=key,start=tuple(member['start']),end=tuple(member['end']),
                                   length=member['axisLength'])
    left,right,bottom,top=(parts['outer_frame.'+side+'.0001'] for side in ('left','right','bottom','top'))
    # Read the frozen component directly; do not ask the product to generate.
    frame_data=items[left.key]['properties']['tubeDesigner.profile']
    frame=_load('tube_profile_catalog').Profile(frame_data.get('id','frozen'),frame_data.get('packageVersion',''),
        frame_data.get('kind','fixed-section'),frame_data.get('displayName',''),frame_data.get('specification',''),
        frame_data['width'],frame_data['depth'],frame_data['wallThickness'],frame_data.get('cornerRadius',0.),
        {'profileForm':'frozen'},None,deepcopy(frame_data['contours']))
    vertical_allowance=core._stock_rules.opposing_half_extent(
        core._profile_arguments((0,0,0),(1,0,0),frame),(0,0,1),node_point=(0,0,0))
    horizontal_allowance=core._stock_rules.opposing_half_extent(
        core._profile_arguments((0,0,0),(0,0,1),frame),(1,0,0),node_point=(0,0,0))
    connections=[]
    def append(key,participants,topology,point,anchors):
        connections.append({'key':key,'kind':'assembly','items':participants,'properties':{
            'topology':topology,'centerlinePoint':list(point),'participantAnchors':anchors}})
    corners=(('bottom-left',left,'start',(1,0,0),bottom,'start',(0,0,1),left.start),
             ('bottom-right',right,'start',(-1,0,0),bottom,'end',(0,0,1),right.start),
             ('top-left',left,'end',(1,0,0),top,'start',(0,0,-1),left.end),
             ('top-right',right,'end',(-1,0,0),top,'end',(0,0,-1),right.end))
    for label,vertical,ve,vn,horizontal,he,hn,point in corners:
        append('outer_frame.corner.'+label,[vertical.key,horizontal.key],'L',point,[
            core._external_end_anchor(vertical,ve,point,core._external_face(vertical,vn),vertical_allowance),
            core._external_end_anchor(horizontal,he,point,core._external_face(horizontal,hn),horizontal_allowance)])
    for key,part in parts.items():
        is_horizontal=key.startswith('main_grid.horizontal.')
        if not (is_horizontal or key.startswith('main_grid.vertical.')): continue
        if is_horizontal:
            part.start=(left.start[0],part.start[1],part.start[2])
            part.end=(right.start[0],part.end[1],part.end[2])
        else:
            part.start=(part.start[0],part.start[1],bottom.start[2])
            part.end=(part.end[0],part.end[1],top.start[2])
        part.length=math.dist(part.start,part.end)
        for end in ('start','end'):
            first=end=='start';point=getattr(part,end)
            host=(left if first else right) if is_horizontal else (bottom if first else top)
            normal=((1,0,0) if first else (-1,0,0)) if is_horizontal else ((0,0,1) if first else (0,0,-1))
            contact=list(point);contact[0 if is_horizontal else 2]+=frame.width/2*(1 if first else -1)
            branch_anchor=core._external_end_anchor(part,end,point,
                stock_allowance=horizontal_allowance if is_horizontal else vertical_allowance)
            branch_anchor['anchor']['contactInset']=math.dist(point,contact)
            append('outer_frame.junction.'+key+'.'+end,[host.key,key],'T',point,[
                core._external_side_anchor(host,point,normal,tuple(contact)),branch_anchor])
    return connections
def _process_resources(parameters, choices, item_keys):
    """Capture selected references and drafts, without loading any tool package."""
    roles = []
    if choices['frameManufacturingMode'] != 'segment_weld':
        roles.append('outerFrameGroove')
    for prefix, option, role in (
        ('access_door.fixed_frame.', 'doorFrameJoinType', 'doorFrameGroove'),
        ('access_door.leaf.frame.', 'doorLeafFrameJoinType', 'doorLeafFrameGroove'),
    ):
        if any(key.startswith(prefix) for key in item_keys) and choices[option] == 'v_groove_90:tool_library':
            roles.append(role)
    resources, drafts = {}, {}
    bindings = parameters.get('tubeDesignerToolBindings', {})
    for role in roles:
        binding = bindings.get(role)
        if binding is None:
            encoded = parameters.get(role + 'Tool', 'system:v-notch-sharp')
            scope, separator, identifier = encoded.partition(':')
            if not separator or not identifier:
                raise ValueError('折弯模具引用无效')
            reference, draft = {'scope': scope, 'id': identifier}, {}
        else:
            reference, draft = deepcopy(binding['ref']), deepcopy(binding.get('parameters', {}))
        # These are product decisions over this currently selected folded frame.
        # Preserve the host's tool draft; derive the machining copy only here.
        prefix = {'outerFrameGroove': 'outerFrame', 'doorFrameGroove': 'doorFrame',
                  'doorLeafFrameGroove': 'doorLeafFrame'}[role]
        factor = choices[prefix+'BendKFactor'] if reference == {'scope': 'system', 'id': 'edge-arc-groove'} else 0.
        if isinstance(factor, bool) or not isinstance(factor, (int, float)) or not math.isfinite(factor) or not 0 <= factor <= 1:
            raise ValueError('折弯系数 K 须介于 0 和 1 之间')
        if reference['scope'] == 'system' and reference['id'] in {'v-notch-sharp', 'edge-arc-groove'}:
            bridge = choices[prefix+'FoldBridge']
            if isinstance(bridge, bool) or not isinstance(bridge, (int, float)) or not math.isfinite(bridge) or not 0.1 <= bridge <= 10:
                raise ValueError('折角留底厚度须介于 0.1 和 10 mm 之间')
            draft.update(bendCompensation=factor > 0., kFactor=float(factor), bottomReference='outer')
            if reference['id'] == 'v-notch-sharp':
                if not isinstance(choices[prefix+'VGrooveMaleFemale'], bool):
                    raise ValueError('V槽公母必须是开关')
                strategy = choices[prefix+'VGrooveBottomStrategy']
                if strategy not in {'sharp', 'rounded'}:
                    raise ValueError('防盗窗 V槽底部方式无效')
                draft.update(leaveBottom=float(bridge), maleFemale=choices[prefix+'VGrooveMaleFemale'],
                             maleFemaleSize=0., bottomStrategy=strategy, asymmetric=False, segmentedBend=False)
                if strategy == 'rounded':
                    radius = choices[prefix+'VGrooveRoundRadius']
                    if isinstance(radius, bool) or not isinstance(radius, (int, float)) or not math.isfinite(radius) or radius < 0:
                        raise ValueError('槽根圆弧半径 R 不能小于 0')
                    draft['roundRadius'] = float(radius)
            else:
                draft['bridge'] = float(bridge)
        resources[role] = {'ref': reference}
        drafts[role] = {reference['id']: draft}
    return resources, drafts


def build_window_manufacturing(parameters, context, core, design=None):
    """Return v4 connections and processes, referring to the shared base model."""
    if design is None:
        design = _load('security_window_product_contract')._design(parameters, context, core)
    else:
        design = deepcopy(design)
    choices = {name: deepcopy(parameters.get(name, default)) for name, default in _PROCESS_DEFAULTS.items()}
    item_keys = [item['key'] for item in design['items']]
    external_available = (
        parameters.get('faceType', 'single') == 'single'
        and parameters.get('frameLayout', 'four_sides') == 'four_sides'
        and not parameters.get('accessDoorEnabled', True)
        and parameters.get('infillPattern', 'grid') in ('horizontal', 'vertical'))
    if not external_available:
        # Match the current descriptor's unavailableChoiceFallback. Retain the
        # host's selection as a draft while this geometry uses built-in rules.
        choices['assemblyPlanningMode'] = 'builtin_rules'
    closed_outer = all(any(key.startswith('outer_frame.' + side + '.') for key in item_keys)
                       for side in ('left', 'right', 'bottom', 'top'))
    if choices['assemblyPlanningMode'] == 'external_templates' or (
            parameters.get('faceType', 'single') == 'single' and not closed_outer):
        choices['frameManufacturingMode'] = 'segment_weld'
    if (choices['frameManufacturingMode'] == 'spatial_v_notch'
            and parameters.get('faceType', 'single') in ('single', 'five')):
        # These faces derive the supported planar route from a retained
        # spatial draft. Their spatial-only controls are hidden by the product
        # descriptor, so those drafts cannot change or reject that route.
        for name in ('outerFrameBendKFactor', 'outerFrameFoldBridge',
                     'outerFrameVGrooveMaleFemale', 'outerFrameVGrooveBottomStrategy',
                     'outerFrameVGrooveRoundRadius'):
            choices[name] = deepcopy(_PROCESS_DEFAULTS[name])
        choices['frameManufacturingMode'] = 'plane_v_notch'
        if parameters.get('faceType') == 'five':
            # The retained spatial choice hides all independent-post controls
            # on this face. Its supported planar route uses defaults rather
            # than consuming those inactive drafts.
            choices.update(deepcopy(_load('security_window_post_connections').POST_DEFAULTS))
    if choices['assemblyPlanningMode']=='external_templates':
        # These local dimensions belong to processing, which this branch has
        # not selected. The host retains their original inactive drafts.
        for name in ('assemblyClearance','horizontalBranchReserve','verticalBranchReserve'):
            choices[name]=0.0
        for name in ('horizontalEndConnection','verticalEndConnection'):
            choices[name]='insert'
    else:
        for prefix,name in (('horizontal','horizontalBranchReserve'),('vertical','verticalBranchReserve')):
            active = any(key.startswith(('main_grid.'+prefix+'.','side_grid.'+prefix+'.',
                                        'cap_grid.'+prefix+'.')) for key in item_keys)
            if not active:
                choices[prefix+'EndConnection']='insert'
            if not active or choices[prefix+'EndConnection'] != 'insert':
                choices[name]=0.0
    choices.update(_load('security_window_post_connections').process_choices({**parameters, **choices}))
    # Material selection is a product decision, never an insertion permission.
    # The selected connection validates its actual tube/ear/receiver geometry
    # downstream; tabs do not insert the post's complete outer contour.
    resources_for_process, drafts = _process_resources(parameters, choices, item_keys)
    connections = []
    for relation in design.get('relationships', []):
        facts = {name: deepcopy(value) for name, value in relation.get('properties', {}).items()
                 if name in ('topology', 'centerlinePoint', 'origin', 'participantAnchors')}
        connections.append({'key': relation['key'], 'kind': relation['kind'],
                            'items': deepcopy(relation['items']), 'properties': facts})
    if choices['assemblyPlanningMode']=='external_templates':
        connections=_external_connections(design,core)
    process_input = {
        'schema': 'icax.assembly-process-input', 'schemaVersion': 3,
        'parts': {'members': {'scope': 'design', 'itemKeys': item_keys}},
        'geometry': {'mergeIdenticalParts': True,
                     'layout': {'faceType': parameters.get('faceType', 'single'),
                                'accessDoorEnabled': bool(parameters.get('accessDoorEnabled', False))}},
    }
    if resources_for_process:
        process_input['resources'] = resources_for_process
    process = {'key': 'window.assembly', 'kind': 'assembly-process', 'definition': {
        'templateId': 'security-window-assembly', 'processInput': process_input,
        'parameters': choices, 'processDrafts': drafts,
        'outputs': {'manufacturing': {'kind': 'manufacturing-set', 'key': 'window.manufacturing'}},
        'dependencies': [],
    }}
    return {'schema': 'icax.manufacturing-model', 'schemaVersion': 4,
            'connections': connections, 'processes': [process]}
