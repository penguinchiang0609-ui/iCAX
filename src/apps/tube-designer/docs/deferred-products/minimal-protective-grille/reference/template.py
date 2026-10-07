"""Extreme-minimal protective grille product entry point."""
from __future__ import annotations

from copy import deepcopy
import hashlib
import importlib.util
import json
from pathlib import Path
import sys

from icax_template_sdk import to_resource_model, display_context, to_display_model
from icax_template_sdk import manufacturing_context, manufacturing_declaration, to_manufacturing_model


TEMPLATE_ID = "minimal-protective-grille"
TEMPLATE_VERSION = "1.2.1"


SHARED = Path(__file__).resolve().parents[2] / "_shared" / "minimal_protective_grille.py"
MODULE = "icax_minimal_protective_grille_" + hashlib.sha256(SHARED.read_bytes()).hexdigest()[:16]
if MODULE not in sys.modules:
    spec = importlib.util.spec_from_file_location(MODULE, SHARED)
    if spec is None or spec.loader is None:
        raise RuntimeError("无法加载极简防护网共享规则")
    module = importlib.util.module_from_spec(spec)
    sys.modules[MODULE] = module
    spec.loader.exec_module(module)


def _rectangular_mirror_center(contours):
    """Return the real V symmetry datum for a supplied, line-edged tube."""
    center = None
    for contour in contours:
        segments = contour.get("segments", [])
        if (contour.get("kind") != "path" or contour.get("closed") is not True
                or len(segments) != 4 or any(
                    segment.get("kind") != "line"
                    or ((segment["start"][0] == segment["end"][0])
                        == (segment["start"][1] == segment["end"][1]))
                    for segment in segments)):
            raise ValueError("防护网提供截面的横向原管暂只支持镜像对称矩形轮廓")
        points = {tuple(point) for segment in segments
                  for point in (segment["start"], segment["end"])}
        xs, ys = sorted({point[0] for point in points}), sorted({point[1] for point in points})
        if len(xs) != 2 or len(ys) != 2 or points != {
                (x, y) for x in xs for y in ys}:
            raise ValueError("防护网提供截面的横向原管暂只支持镜像对称矩形轮廓")
        current = (ys[0] + ys[1]) / 2
        if center is not None and abs(center - current) > 1.0e-8:
            raise ValueError("防护网提供截面的内外轮廓没有共同镜像基准")
        center = current
    return center


def _declare_external_section_frames(document):
    geometry = {node["key"]: node for node in document["geometry"]}
    swap_axes = sys.modules[MODULE]._catalog._swap_contour_axes
    for item in document["items"]:
        properties = item["properties"]
        member = properties["assemblyFrame.member"]
        solid = geometry[item["representations"]["result"]]
        if solid["operator"] != "transform":
            raise ValueError("防护网原管缺少可核对的截面放置")
        extrusion = geometry[solid["inputs"][0]]
        profile = geometry[extrusion["inputs"][0]]
        if extrusion["operator"] != "extrude" or profile["operator"] != "profile2d":
            raise ValueError("防护网原管缺少可核对的截面轮廓")
        local = profile["arguments"]["placement"]
        if local != {"origin": [0.0, 0.0, 0.0], "xAxis": [1.0, 0.0, 0.0],
                     "yAxis": [0.0, 1.0, 0.0]}:
            raise ValueError("防护网原管截面局部放置不受支持")
        placement = solid["arguments"]["placement"]
        start, end = member["start"], member["end"]
        axis = [(end[i] - start[i]) / member["axisLength"] for i in range(3)]
        x_axis, y_axis = placement["xAxis"], placement["yAxis"]
        canonical = properties["tubeDesigner.profile"]["contours"]
        emitted = profile["arguments"]["contours"]
        if emitted != canonical:
            if emitted != [swap_axes(contour) for contour in canonical]:
                raise ValueError("防护网原管截面与提供的管型轮廓不一致")
            x_axis, y_axis = y_axis, x_axis
        cross = [x_axis[1] * y_axis[2] - x_axis[2] * y_axis[1],
                 x_axis[2] * y_axis[0] - x_axis[0] * y_axis[2],
                 x_axis[0] * y_axis[1] - x_axis[1] * y_axis[0]]
        alignment = sum(cross[i] * axis[i] for i in range(3))
        mirror_offset = 0.0
        old_y_axis = y_axis
        if alignment < -1.0 + 1.0e-8:
            if properties["tubeDesigner.profile"].get("geometrySource") == "providedBoundary":
                mirror_offset = 2 * _rectangular_mirror_center(canonical)
            y_axis = [-value for value in y_axis]
        elif alignment < 1.0 - 1.0e-8:
            raise ValueError("防护网原管截面与构件轴向不一致")
        origin = [placement["origin"][i] + mirror_offset * old_y_axis[i]
                  - axis[i] * member["stockInterval"]["startStation"]
                  for i in range(3)]
        delta = [start[i] - origin[i] for i in range(3)]
        if abs(axis[2]) > 1.0 - 1.0e-8:
            top = [-1.0, 0.0, 0.0]
        elif abs(axis[0]) > 1.0 - 1.0e-8:
            top = [0.0, 0.0, 1.0]
        else:
            raise ValueError("防护网原管连接面方向不受支持")
        member["sectionFrame"] = {
            "originAtStart": origin, "xAxis": x_axis, "yAxis": y_axis,
            "centerlineUV": [sum(delta[i] * x_axis[i] for i in range(3)),
                             sum(delta[i] * y_axis[i] for i in range(3))],
            "faceNormals": {"top": top, "bottom": [-value for value in top],
                            "right": [0.0, 1.0, 0.0], "left": [0.0, -1.0, 0.0]},
        }


def _generate_resource_document(parameters, context):
    document = sys.modules[MODULE].generate(parameters, context)
    if parameters.get("assemblyPlanningMode") == "external_templates":
        _declare_external_section_frames(document)
    return to_resource_model(document)


def display(parameter_values):
    """Generate display data from the values owned by the product instance."""
    design_values = deepcopy(parameter_values)
    design_values['assemblyPlanningMode'] = 'external_templates'
    document = sys.modules[MODULE].generate(design_values, display_context(__file__))
    geometry = {node['key']:node for node in document['geometry']}
    sections = {}
    for item in document['items']:
        properties = item['properties']
        solid = geometry[item['representations']['result']]
        extrusion = geometry[solid['inputs'][0]]
        profile = properties['tubeDesigner.profile']
        emitted = geometry[extrusion['inputs'][0]]['arguments']['contours']
        canonical = profile['contours']
        if emitted == canonical:
            profile['sectionResource'] = extrusion['inputs'][0]
            profile['sectionCoordinateMap'] = [[1,0],[0,1]]
        else:
            swap_axes = sys.modules[MODULE]._catalog._swap_contour_axes
            if emitted != [swap_axes(contour) for contour in canonical]:
                raise ValueError('shared grille section is not the intrinsic profile coordinate basis')
            encoded = json.dumps(canonical, sort_keys=True, separators=(',', ':'), allow_nan=False)
            if encoded not in sections:
                section_key = 'shared.design.profile.' + hashlib.sha256(encoded.encode('utf-8')).hexdigest()
                document['geometry'].append({'key':section_key, 'operator':'profile2d', 'inputs':[],
                                            'arguments':{'contours':deepcopy(canonical)}})
                sections[encoded] = section_key
            profile['sectionResource'] = sections[encoded]
            profile['sectionCoordinateMap'] = [[0,1],[1,0]]
        placement = solid['arguments']['placement']
        x, y = placement['xAxis'], placement['yAxis']
        properties['assemblyFrame.member']['sectionFrame'] = {
            'originAtStart': deepcopy(placement['origin']), 'xAxis': deepcopy(x), 'yAxis': deepcopy(y),
            'zAxis': [x[1]*y[2]-x[2]*y[1], x[2]*y[0]-x[0]*y[2], x[0]*y[1]-x[1]*y[0]],
            'centerlineUV': [0.0, 0.0]}
        properties['tubeDesigner.productRule'] = {
            key:value for key,value in properties['tubeDesigner.productRule'].items()
            if key in ('product', 'role', 'layoutIndex', 'centerHeight')}
    return to_display_model(document)




def manufacturing(parameter_values):
    """Return manufacturing declarations from the values owned by the host."""
    with manufacturing_declaration():
        document = _generate_resource_document(parameter_values, manufacturing_context(__file__))
        result = to_manufacturing_model(document)
        requested_numbers = {item['key']:item['properties']['partNumber'] for item in document['items']}
        shared = sys.modules[MODULE]
        design_values = deepcopy(parameter_values)
        design_values['assemblyPlanningMode'] = 'external_templates'
        designed = {item.part.key:item.part for item in shared._layout(design_values)['parts']}
        selected = shared._layout(parameter_values)
        external = selected['external']
        preparations = []
        for item in selected['parts']:
            part, member = item.part, designed[item.part.key]
            key = part.key
            if external:
                connections, connected = [], set()
                for connection in result['connections']:
                    for participant in connection.get('properties', {}).get('participantAnchors', []):
                        anchor = participant.get('anchor', {})
                        if participant.get('itemKey') == key and anchor.get('kind') == 'end':
                            connections.append(connection['key'])
                            connected.add(anchor['end'])
                requirements = {'connectionKeys': connections,
                    'freeEnds': [name for name in ('start', 'end') if name not in connected],
                    'status': 'await-external', 'ready': False}
            else:
                axis = [(member.end[i]-member.start[i])/member.length for i in range(3)]
                first,last = (sum((point[i]-member.start[i])*axis[i] for i in range(3))
                              for point in (part.start, part.end))
                requirements = {'axialInterval': {'startStation':first,'endStation':last},
                    'direction': 'forward' if last > first else 'reverse', 'status':'prepared', 'ready':True}
            prep_key = key + '.prepare'
            preparations.append({'key':prep_key, 'kind':'assembly-process', 'definition': {
                'templateId':'profile-stock-preparation',
                'processInput': {'schema':'icax.assembly-process-input', 'schemaVersion':2,
                    'parts': {'stock': {'scope':'manufacturing','itemKey':key,'state':'initial'}},
                    'geometry':requirements},
                'parameters':{'requestedPartNumber':requested_numbers[key], 'reportingLengthPrecision':3},
                'processDrafts':{},'targets':{'stock':key},'dependencies':[]}})
            calls = [process['definition'] for process in result['processes']
                     if key in process['definition'].get('targets', {}).values()]
            if calls:
                first_call = calls[0]
                first_call['dependencies'].append(prep_key)
                first_call['processInput']['parts']['stock']['state'] = {'after':prep_key}
                facts = first_call['processInput']['parts']['stock'].get('data', {})
                if (all(name in facts for name in ('start','end'))
                        or all(name in facts for name in ('matrix','length'))
                        or first_call['templateId'] == 'structural-male-head'):
                    requirements.clear()
                    first_process = next(process for process in result['processes'] if process['definition'] is first_call)
                    requirements.update(sourceProcessKey=first_process['key'],status='prepared',ready=True)
        result['processes'] = preparations + result['processes']
        return result
