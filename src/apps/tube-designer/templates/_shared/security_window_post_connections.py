"""Local end joints for frame posts, all using the product's frame section."""
from copy import deepcopy
import importlib.util
import math
from pathlib import Path


POST_DEFAULTS = {
    'foldedPostJoint': 'weld',
    'foldedPostInsertDepth': 12.,
    'foldedPostFitGap': .2,
    'foldedPostTabWidth': 8.,
    'foldedPostTabLength': 12.,
    'foldedPostSideClearance': .2,
}


def active_route(parameters):
    face = parameters.get('faceType', 'single')
    mode = parameters.get('frameManufacturingMode', 'segment_weld')
    if face == 'single':
        return (mode == 'segment_weld' and parameters.get('frameLayout', 'four_sides') == 'four_sides'
                and parameters.get('assemblyPlanningMode', 'builtin_rules') == 'builtin_rules')
    # Spatial paths consume their two terminal posts as frame spans. Only the
    # remaining shared posts retain their own end joints.
    # The planner selection is editable only for a single face. Multi-face
    # windows always use their built-in planner; an inactive draft must not
    # select a different route in display and manufacturing.
    return ((mode in ('segment_weld', 'plane_v_notch') and face in ('two', 'three', 'five'))
            or (mode == 'spatial_v_notch' and face in ('two', 'three')))


def insertion_parameters(choices):
    joint = choices['foldedPostJoint']
    return {'joint': joint,
            'insertionDepth': choices['foldedPostInsertDepth'] if joint == 'insert' else choices['foldedPostTabLength'],
            'clearance': choices['foldedPostFitGap'] if joint == 'insert' else choices['foldedPostSideClearance'],
            'tabWidth': choices['foldedPostTabWidth'] if joint == 'tabs' else 0.}


def process_choices(parameters):
    """Keep inactive drafts in the host, without forwarding them to geometry."""
    joint = parameters.get('foldedPostJoint', 'weld') if active_route(parameters) else 'weld'
    if joint not in ('weld', 'insert', 'tabs'):
        raise ValueError('不支持的折角立柱连接方式')
    binding = parameters.get('tubeDesignerToolBindings', {}).get('outerFrameGroove')
    reference = binding['ref'] if binding is not None else None
    edge_arc = (reference == {'scope': 'system', 'id': 'edge-arc-groove'} if reference is not None
                else parameters.get('outerFrameGrooveTool') == 'system:edge-arc-groove')
    if (active_route(parameters) and parameters.get('faceType', 'single') != 'single'
            and parameters.get('frameManufacturingMode') != 'segment_weld' and edge_arc and joint == 'weld'):
        # A flat post end cannot contact both frozen legs of this root-arc
        # bend. Match the descriptor's supported existing tab joint; retain
        # the unavailable weld choice only in the host's parameter draft.
        joint = 'tabs'
    choices = {key: 0. for key in POST_DEFAULTS if key != 'foldedPostJoint'}
    choices['foldedPostJoint'] = joint
    active = (('foldedPostInsertDepth', 'foldedPostFitGap') if joint == 'insert' else
              ('foldedPostTabWidth', 'foldedPostTabLength', 'foldedPostSideClearance') if joint == 'tabs' else ())
    for key in active:
        value = parameters.get(key, POST_DEFAULTS[key])
        minimum = 0. if key.endswith(('Gap', 'Clearance')) else .1
        if (isinstance(value, bool) or not isinstance(value, (int, float))
                or not math.isfinite(value) or not minimum <= value <= 100.):
            raise ValueError('折角立柱连接参数无效: ' + key)
        choices[key] = deepcopy(value)
    return choices


def node_parameters(choices):
    joint = choices.get('foldedPostJoint', 'weld')
    result = {'abJoint': 'continuous', 'cJoint': joint}
    if joint == 'insert':
        result.update(insertDepth=choices['foldedPostInsertDepth'], fitGap=choices['foldedPostFitGap'])
    elif joint == 'tabs':
        result.update(tabWidth=choices['foldedPostTabWidth'], tabLength=choices['foldedPostTabLength'],
                      sideClearance=choices['foldedPostSideClearance'])
    return result


def shared_post_horizontal_limits(receiver, branches, nominal_depth, clearance):
    """Limit perpendicular bar ends from real section projections at a post.

    Product design calls this with zero machining clearance. The assembly
    planner calls it with its selected fit clearance, preserving both bars'
    clearanced opening envelopes without changing their actual sections.
    """
    path = Path(__file__).with_name('assembly_stock_allowance.py')
    spec = importlib.util.spec_from_file_location('icax_shared_post_projection', path)
    projection = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(projection)
    def subtract(a, b): return [x-y for x,y in zip(a,b)]
    def dot(a, b): return sum(x*y for x,y in zip(a,b))
    def unit(v):
        length = math.sqrt(dot(v,v))
        if length <= 1.e-8:
            raise ValueError('折角立柱的横杆不能是零长度')
        return [x/length for x in v]
    prepared = []
    for branch in branches:
        axis = unit(subtract(branch['end'], branch['start']))
        if abs(axis[2]) > 1.e-7:
            continue
        node = [receiver['start'][0], receiver['start'][1], branch['start'][2]]
        station = dot(subtract(node, branch['start']), axis)
        on_axis = [branch['start'][i]+station*axis[i] for i in range(3)]
        if math.dist(on_axis, node) > 1.e-7:
            continue
        end = min(('start','end'), key=lambda key: math.dist(branch[key], node))
        away = [value*(1 if end=='start' else -1) for value in axis]
        extent = projection._support_interval(receiver['section'], away, node)[1]
        distance = dot(subtract(branch[end], node), away)
        if distance < -1.e-7 or distance > extent+1.e-7:
            continue
        prepared.append((branch, axis, end, away, node, extent))
    result = {}
    for branch, axis, end, away, node, extent in prepared:
        depth = nominal_depth
        own_z = projection._support_interval(branch['section'], [0.,0.,1.], node)
        for other, other_axis, _, _, _, _ in prepared:
            if other['key']==branch['key'] or abs(dot(axis,other_axis)) > 1.e-7:
                continue
            other_z = projection._support_interval(other['section'], [0.,0.,1.], node)
            if min(own_z[1],other_z[1])-max(own_z[0],other_z[0]) <= -2*clearance+1.e-7:
                continue
            other_extent = projection._support_interval(other['section'], away, node)[1]
            depth = min(depth, extent-other_extent-2*clearance)
        if depth < nominal_depth-1.e-7:
            if depth <= receiver['wallThickness']+clearance+1.e-7:
                raise ValueError('折角立柱的正交横杆可用入榫深度不足，不能同时穿过柱壁并避开相邻杆')
            result[(branch['key'],end)] = {'depth': depth, 'node': node, 'away': away, 'extent': extent}
    return result
