"""Continuous round wire between independent front/back rectangular strips.

This package describes real stock geometry and contact relationships only.
Supplier-specific grooves, fasteners and end anchorage are not inferred.
"""
from __future__ import annotations

from copy import deepcopy
import hashlib
import importlib.util
import math
from pathlib import Path
import sys

from icax_template_sdk import NeutralModel, to_display_model
from icax_template_sdk.manufacturing import validate_manufacturing_model

TEMPLATE_ID = 'invisible-protective-grille'
TEMPLATE_VERSION = '1.0.0'
ROOT = Path(__file__).resolve().parent
SHARED = ROOT.parents[1] / '_shared'
EPS = 1.e-7


def _load(path):
    name = 'icax_invisible_' + path.stem + '_' + hashlib.sha256(path.read_bytes()).hexdigest()[:16]
    if name not in sys.modules:
        spec = importlib.util.spec_from_file_location(name, path)
        module = importlib.util.module_from_spec(spec)
        sys.modules[name] = module
        spec.loader.exec_module(module)
    return sys.modules[name]


def _number(parameters, key, minimum=0., maximum=10000.):
    value = parameters[key]
    if (isinstance(value, bool) or not isinstance(value, (int, float))
            or not math.isfinite(value) or not minimum <= value <= maximum):
        raise ValueError(f'{key} 必须为 {minimum:g}～{maximum:g} 的有限数值')
    return float(value)


def layout(parameters):
    catalogue = _load(SHARED / 'tube_profile_catalog.py')
    wire, clamp = (catalogue.load_profile(parameters, role) for role in ('wire', 'clamp'))
    if wire.kind != 'round-bar' or wire.hollow or wire.wall != 0.:
        raise ValueError('竖向线材必须使用实心圆线截面')
    if clamp.kind != 'rect-bar' or clamp.hollow or clamp.wall != 0.:
        raise ValueError('横向压条必须使用实心矩形截面')
    width, height = (_number(parameters, key, 100.) for key in ('width', 'height'))
    diameter, band, thickness = wire.width, clamp.width, clamp.depth
    if (not all(math.isfinite(v) and v > EPS for v in (diameter, band, thickness))
            or abs(wire.depth-diameter) > EPS):
        raise ValueError('线径和压条截面尺寸无效')
    pitch = _number(parameters, 'wireCenterSpacing', 1., 2000.)
    if pitch <= diameter+EPS:
        raise ValueError('竖线中心距须大于实际线径，保留有效净间距')
    if width < diameter or height <= 2*band+EPS:
        raise ValueError('成品尺寸不足以容纳线材和上下压条')
    count_value = _number(parameters, 'clampCount', 0., 100.)
    if count_value != int(count_value):
        raise ValueError('中间夹条组数必须是整数')
    count = int(count_value)
    middle = []
    if count:
        first = _number(parameters, 'clampBottomOffset')
        last = height-_number(parameters, 'clampTopOffset') if count >= 2 else first
        if last < first:
            raise ValueError('中间夹条首末位置顺序无效')
        middle = [first+(last-first)*index/(count-1) for index in range(count)] if count > 1 else [first]
    centers = [band/2, *middle, height-band/2]
    if any(b-a <= band+EPS for a, b in zip(centers, centers[1:])):
        raise ValueError('上下及中间夹条之间须保留有效净间距，不能重叠或相贴')

    wire_count = int(math.floor((width-diameter)/pitch))+1
    if wire_count+2*len(centers) > 1500:
        raise ValueError('当前线材和压条数量过多，请增大竖线中心距')
    start_x = (width-(wire_count-1)*pitch)/2
    members = []
    for index in range(wire_count):
        x = start_x+index*pitch
        members.append(dict(key=f'wire.vertical.{index+1:04d}', name='竖向线材', role='wire.vertical',
            profileRole='wire', profile=wire, start=(x, 0., 0.), end=(x, 0., height)))
    groups = [('bottom', '下夹条', centers[0])]
    groups.extend((f'middle.{index+1:04d}', '中间夹条', z) for index, z in enumerate(middle))
    groups.append(('top', '上夹条', centers[-1]))
    contacts = []
    for group, name, z in groups:
        strip_keys = []
        for side, sign, label in (('front', -1., '正面'), ('back', 1., '背面')):
            key, y = f'clamp.{group}.{side}', sign*(diameter+thickness)/2
            strip_keys.append(key)
            members.append(dict(key=key, name=name+'（'+label+'）', role='clamp.'+group.split('.')[0],
                profileRole='clamp', profile=clamp, start=(0., y, z), end=(width, y, z)))
        # A relationship records the real two-sided tangency of each wire;
        # it neither cuts that wire nor certifies a fastening mechanism.
        for wire_member in members[:wire_count]:
            contacts.append({'key': f'contact.{group}.{wire_member["key"]}', 'kind': 'assembly',
                'items': [wire_member['key'], *strip_keys], 'properties': {
                    'recipe': 'two-strip-wire-contact',
                    'participantRoles': ['wire', 'front-strip', 'back-strip'],
                    'wireContinuous': True, 'contactBand': [z-band/2, z+band/2],
                    'contactPoints': [[wire_member['start'][0], -diameter/2, z],
                                      [wire_member['start'][0], diameter/2, z]],
                    'fasteningVerified': False}})
    return {'members': members, 'connections': contacts, 'facts': {
        'wireCount': wire_count, 'wireDiameter': diameter, 'wireCenterSpacing': pitch,
        'continuousVerticalWires': True, 'clampGroupCenters': centers,
        'middleClampCenters': middle, 'clampGroupCount': len(centers),
        'clampStripCount': 2*len(centers), 'memberCount': len(members),
        'hasSideFrame': False, 'fasteningVerified': False, 'endAnchorageModeled': False}}


def display(parameter_values):
    state = layout(parameter_values)
    model = NeutralModel(template_id=TEMPLATE_ID, template_version=TEMPLATE_VERSION,
                         package_digest='', parameters=deepcopy(parameter_values))
    builder = _load(SHARED / 'shared_tube_geometry.py').SharedTubeGeometry(model)
    keys = []
    for index, member in enumerate(state['members'], 1):
        key, profile, role = member['key'], member['profile'], member['profileRole']
        start, end = member['start'], member['end']
        x, y, axis = (([1., 0., 0.], [0., 1., 0.], [0., 0., 1.]) if role == 'wire'
                      else ([0., 0., 1.], [0., -1., 0.], [1., 0., 0.]))
        raw = builder.emit_tube(key, profile_arguments={
            'placement': {'origin': list(start), 'xAxis': x, 'yAxis': y}, 'contours': profile.contours()},
            extrude_arguments={'vector': [b-a for a, b in zip(start, end)]})
        section = model.geometry(key+'.section', 'profile2d', arguments={'contours': profile.contours()})
        properties = profile.properties()
        properties.update(sectionResource=section, sectionCoordinateMap=[[1, 0], [0, 1]])
        length = math.dist(start, end)
        keys.append(model.item(key, member['name'], representations={'display': raw}, properties={
            'partNumber': f'{parameter_values["productCode"]}-{index:03d}', 'length': length,
            'quantity': 1, 'partKind': 'tube', 'materialCategory': 'tube', 'sourcing': 'made',
            'categoryKey': 'invisible-grille.'+role,
            'categoryName': '竖向线材' if role == 'wire' else '横向压条',
            'tubeDesigner.profile': properties,
            'tubeDesigner.productRule': {'product': TEMPLATE_ID, 'role': member['role'], 'profileRole': role},
            'tubeDesigner.invisibleGrille': deepcopy(state['facts']),
            'assemblyFrame.member': {'start': list(start), 'end': list(end), 'axisLength': length,
                'sectionFrame': {'originAtStart': list(start), 'xAxis': x, 'yAxis': y, 'zAxis': axis,
                                 'centerlineUV': [0., 0.]}}}))
    model.output('display.default', 'display', keys)
    document = model.build()
    # Overall dimensions and parameter labels are declared by the descriptor.
    # Offset dimensions have real anchors on the generated middle strip centers.
    annotations = []
    middle = state['facts']['middleClampCenters']
    if middle:
        width, height = float(parameter_values['width']), float(parameter_values['height'])
        dimensions = [('clampBottomOffset', 0., middle[0])]
        if len(middle) > 1:
            dimensions.append(('clampTopOffset', height, middle[-1]))
        for key, boundary, z in dimensions:
            annotations.append({'id': TEMPLATE_ID+'.'+key, 'parameter': key, 'kind': 'linear',
                'start': [width, 0., boundary], 'end': [width, 0., z], 'offset': [80., 0., 0.],
                'generatedValue': parameter_values[key]})
    document.setdefault('extensions', {})['tubeDesigner.specificationAnnotations'] = annotations
    return to_display_model(document)


def manufacturing(parameter_values):
    state = layout(parameter_values)
    processes = []
    for index, member in enumerate(state['members'], 1):
        key = member['key']
        length = math.dist(member['start'], member['end'])
        processes.append({'key': key+'.prepare', 'kind': 'assembly-process', 'definition': {
            'templateId': 'profile-stock-preparation',
            'processInput': {'schema': 'icax.assembly-process-input', 'schemaVersion': 2,
                'parts': {'stock': {'scope': 'manufacturing', 'itemKey': key, 'state': 'initial'}},
                'geometry': {'axialInterval': {'startStation': 0., 'endStation': length},
                             'direction': 'forward', 'status': 'prepared', 'ready': True}},
            'parameters': {'requestedPartNumber': f'{parameter_values["productCode"]}-{index:03d}',
                           'reportingLengthPrecision': 3},
            'processDrafts': {}, 'targets': {'stock': key}, 'dependencies': []}})
    result = {'schema': 'icax.manufacturing-model', 'schemaVersion': 4,
              'connections': deepcopy(state['connections']), 'processes': processes}
    validate_manufacturing_model(result)
    return result
