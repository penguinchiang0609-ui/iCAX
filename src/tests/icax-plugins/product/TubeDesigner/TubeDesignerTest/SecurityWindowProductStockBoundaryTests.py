"""Logical product members remain independent of their manufacturing blanks.

These are Python neutral-model regressions. They do not replace native preview,
descriptor validation, or manufacturing-plan acceptance.
"""
from copy import deepcopy
from functools import lru_cache
import importlib.util
import json
import math
from pathlib import Path
import sys
import unittest


sys.dont_write_bytecode = True
ROOT = next(path for path in Path(__file__).resolve().parents
            if (path / 'src/apps/tube-designer/templates').is_dir())
sys.path.insert(0, str(ROOT / 'src/iCAX-Engine/framework/TemplateRuntime/python'))
from icax_template_sdk import expand_resource_model
PACKAGE = ROOT / 'src/apps/tube-designer/templates/product/single_face_security_window'
SPEC = importlib.util.spec_from_file_location('security_window_product_stock_boundary', PACKAGE / 'template.py')
SUBJECT = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = SUBJECT
SPEC.loader.exec_module(SUBJECT)
DESCRIPTOR = json.loads((PACKAGE / 'template.json').read_text(encoding='utf-8'))
DEFAULTS = {field['key']: field['defaultValue'] for field in DESCRIPTOR['parameters']}
MODES = ('segment_weld', 'plane_v_notch', 'spatial_v_notch')
DESIGN_COUNTS = {'single': 4, 'two': 7, 'three': 10, 'five': 12}
STOCK_COUNTS = {'single': (4, 1, 1), 'two': (7, 5, 2),
                'three': (10, 6, 3), 'five': (12, 6, 6)}


@lru_cache(maxsize=None)
def generate(face, mode, purpose=None, opening=False, corner_join=None, join_type=None):
    values = {**deepcopy(DEFAULTS), 'faceType': face,
              'frameManufacturingMode': mode, 'accessDoorEnabled': opening}
    if corner_join is not None:
        values['frameCornerJoin'] = corner_join
    if join_type is not None:
        values['frameJoinType'] = join_type
    original = deepcopy(values)
    context = {'template': DESCRIPTOR}
    if purpose is not None:
        context['geometryPurpose'] = purpose
    raw = SUBJECT.generate(values, context)
    document = expand_resource_model(raw)
    if values != original:
        raise AssertionError('Generation changed host parameters')
    if raw['schema'] == 'icax.display-model':
        if 'parameters' in raw or document['parameters']:
            raise AssertionError('Pure display returned host parameters')
    elif document['parameters'] != original:
        raise AssertionError('Manufacturing generation returned different parameters')
    return document


def output_items(document, output_key):
    selected = next((output for output in document['outputs'] if output['key'] == output_key), None)
    # Purpose-specific documents follow the SDK/native result convention;
    # combined documents expose the two separately named output groups.
    if selected is None:
        selected = next(output for output in document['outputs'] if output['key'] == 'result')
    indexed = {item['key']: item for item in document['items']}
    missing = set(selected['items']) - set(indexed)
    if missing:
        raise AssertionError(f'{output_key} references absent items: {sorted(missing)}')
    return {key: indexed[key] for key in selected['items']}


def outer_frames(items):
    return {key.removeprefix('manufacturing.'): item for key, item in items.items()
            if key.removeprefix('manufacturing.').startswith('outer_frame.')}


def display_representation(item):
    representations = item['representations']
    return representations.get('display', representations.get('result'))


def geometry_signature(document, representation):
    """Compare actual geometry DAGs without treating generated keys as geometry."""
    indexed = {node['key']: node for node in document['geometry']}
    cache = {}
    visiting = set()

    def node_signature(key):
        if key in cache:
            return cache[key]
        if key in visiting:
            raise AssertionError(f'Cyclic geometry graph at {key}')
        visiting.add(key)
        node = indexed[key]

        def canonical(value):
            if isinstance(value, str) and value in indexed:
                return ('geometry', node_signature(value))
            if isinstance(value, list):
                return tuple(canonical(entry) for entry in value)
            if isinstance(value, dict):
                return tuple((name, canonical(entry)) for name, entry in sorted(value.items()))
            return value

        result = (node['operator'], tuple(node_signature(child) for child in node['inputs']),
                  canonical(node['arguments']))
        visiting.remove(key)
        cache[key] = result
        return result

    return node_signature(representation)


def subtract(left, right):
    return tuple(a - b for a, b in zip(left, right))


def dot(left, right):
    return sum(a * b for a, b in zip(left, right))


def unit(vector):
    length = math.sqrt(dot(vector, vector))
    if length <= 1e-9:
        raise AssertionError('A logical straight member has zero length')
    return tuple(value / length for value in vector)


class ProductStockBoundaryTests(unittest.TestCase):
    def test_display_is_independent_of_insertion_clearance_and_mould_drafts(self):
        for face in DESIGN_COUNTS:
            original={**deepcopy(DEFAULTS),'faceType':face,'frameManufacturingMode':'spatial_v_notch'}
            reference=expand_resource_model(SUBJECT.generate(original,{'template':DESCRIPTOR,'geometryPurpose':'display'}))
            changed={**deepcopy(original),'horizontalBranchReserve':0,'verticalBranchReserve':999,
                     'assemblyClearance':999,'outerFrameGrooveTool':'user:missing-tool'}
            snapshot=deepcopy(changed)
            result=expand_resource_model(SUBJECT.generate(changed,{'template':DESCRIPTOR,'geometryPurpose':'display'}))
            self.assertEqual(snapshot,changed)
            self.assertEqual({},result['parameters'])
            self.assertEqual(reference['geometry'],result['geometry'])
            self.assertEqual(reference['items'],result['items'])
            self.assertEqual(reference['relationships'],result['relationships'])

    def test_external_node_mode_also_separates_raw_stocks_from_finished_members(self):
        values={**deepcopy(DEFAULTS),'assemblyPlanningMode':'external_templates','infillPattern':'horizontal',
                'accessDoorEnabled':False}
        original=deepcopy(values)
        display=expand_resource_model(SUBJECT.generate(values,{'template':DESCRIPTOR,'geometryPurpose':'display'}))
        stock=expand_resource_model(SUBJECT.generate(values,{'template':DESCRIPTOR,'geometryPurpose':'manufacturing'}))
        self.assertEqual(original,values)
        self.assertEqual({},display['parameters'])
        self.assertEqual(original,stock['parameters'])
        self.assertEqual({item['key'] for item in display['items']},
                         {source['itemKey'] for item in stock['items']
                          for source in item['properties']['manufacturing.sourceMembers']})
        for item in display['items']:
            self.assert_logical_member(item)
            self.assertNotIn('stockState',item['properties']['assemblyFrame.member'])
            self.assertNotIn('manufacturing.sourceMembers',item['properties'])
        for item in stock['items']:
            self.assertNotIn('assemblyFrame.member',item['properties'])

    def assert_vector_close(self, actual, expected):
        self.assertEqual(3, len(actual))
        for a, b in zip(actual, expected):
            self.assertAlmostEqual(a, b, places=6)

    def assert_logical_member(self, item):
        member = item['properties']['assemblyFrame.member']
        self.assertEqual(3, len(member['start']))
        self.assertEqual(3, len(member['end']))
        direction = unit(subtract(member['end'], member['start']))
        section = member['sectionFrame']
        axes = [section[key] for key in ('xAxis', 'yAxis', 'zAxis')]
        for axis in axes:
            self.assertAlmostEqual(1, dot(axis, axis), places=6)
        for first, second in ((0, 1), (0, 2), (1, 2)):
            self.assertAlmostEqual(0, dot(axes[first], axes[second]), places=6)
        self.assertAlmostEqual(1, abs(dot(section['zAxis'], direction)), places=6)
        return member

    def test_all_faces_keep_the_same_product_members_and_geometry_across_stock_modes(self):
        for face, expected_count in DESIGN_COUNTS.items():
            reference = generate(face, MODES[0], 'display')
            expected = outer_frames(output_items(reference, 'display.default'))
            self.assertEqual(expected_count, len(expected))
            for mode in MODES:
                with self.subTest(face=face, mode=mode):
                    document = generate(face, mode, 'display')
                    frames = outer_frames(output_items(document, 'display.default'))
                    self.assertEqual(set(expected), set(frames))
                    self.assertEqual(set(expected), set(outer_frames({i['key']: i for i in document['items']})))
                    self.assertFalse(any('continuous' in key or '.plane.' in key for key in frames))
                    for key, item in frames.items():
                        member = self.assert_logical_member(item)
                        self.assertEqual(expected[key]['properties']['assemblyFrame.member'], member)
                        self.assertEqual(
                            geometry_signature(reference, display_representation(expected[key])),
                            geometry_signature(document, display_representation(item)))

    def test_product_geometry_does_not_depend_on_frame_end_join_drafts(self):
        for face in DESIGN_COUNTS:
            for mode in MODES:
                with self.subTest(face=face, mode=mode):
                    a = generate(face, mode, 'display', corner_join='rail_miter', join_type='butt_90')
                    b = generate(face, mode, 'display', corner_join='post_butt', join_type='miter_45')
                    first = outer_frames(output_items(a, 'display.default'))
                    second = outer_frames(output_items(b, 'display.default'))
                    self.assertEqual(set(first), set(second))
                    for key in first:
                        self.assertEqual(first[key]['properties']['assemblyFrame.member'],
                                         second[key]['properties']['assemblyFrame.member'])
                        self.assertEqual(geometry_signature(a, display_representation(first[key])),
                                         geometry_signature(b, display_representation(second[key])))

    def test_combined_outputs_separate_product_members_from_manufacturing_blanks(self):
        for face, counts in STOCK_COUNTS.items():
            for mode, stock_count in zip(MODES, counts):
                with self.subTest(face=face, mode=mode):
                    document = generate(face, mode)
                    self.assertEqual({'display.default', 'export.manufacturing'},
                                     {output['key'] for output in document['outputs']})
                    design = output_items(document, 'display.default')
                    stock = output_items(document, 'export.manufacturing')
                    self.assertFalse(set(design) & set(stock))
                    for item in design.values():
                        self.assertNotIn('manufacturing.sourceMembers',item['properties'])
                        self.assertNotIn('tubeDesigner.cornerProcess',item['properties'])
                        self.assertIn('assemblyFrame.member',item['properties'])
                    self.assertEqual(DESIGN_COUNTS[face], len(outer_frames(design)))
                    self.assertEqual(stock_count, len(outer_frames(stock)))
                    if mode != 'segment_weld':
                        continuous = [key for key in stock if 'continuous' in key or '.plane.' in key]
                        self.assertTrue(continuous)
                        self.assertFalse(set(continuous) & set(design))
                    keys = {item['key'] for item in document['items']}
                    for relationship in document['relationships']:
                        self.assertTrue(set(relationship['items']) <= keys, relationship)

    def test_manufacturing_only_keeps_stock_counts_and_geometry_references(self):
        for face, counts in STOCK_COUNTS.items():
            for mode, stock_count in zip(MODES, counts):
                with self.subTest(face=face, mode=mode):
                    document = generate(face, mode, 'manufacturing')
                    stock = output_items(document, 'export.manufacturing')
                    self.assertEqual(stock_count, len(outer_frames(stock)))
                    self.assertEqual(set(stock), {item['key'] for item in document['items']})
                    geometry = {node['key'] for node in document['geometry']}
                    for item in stock.values():
                        self.assertTrue(set(item['representations'].values()) <= geometry)

    def test_stock_mapping_covers_design_members_with_real_source_spans(self):
        for face in DESIGN_COUNTS:
            for mode in MODES:
                with self.subTest(face=face, mode=mode):
                    document = generate(face, mode)
                    design = outer_frames(output_items(document, 'display.default'))
                    stock = outer_frames(output_items(document, 'export.manufacturing'))
                    mapped = set()
                    for item in stock.values():
                        sources = item['properties']['manufacturing.sourceMembers']
                        self.assertTrue(sources)
                        for source in sources:
                            member_key = source['itemKey']
                            self.assertIn(member_key, design)
                            mapped.add(member_key)
                            member = design[member_key]['properties']['assemblyFrame.member']
                            direction = unit(subtract(member['end'], member['start']))
                            self.assertTrue(source['spans'])
                            for span in source['spans']:
                                self.assertEqual(member_key, span['itemKey'])
                                span_direction = unit(subtract(span['end'], span['start']))
                                self.assertAlmostEqual(1, abs(dot(direction, span_direction)), places=6)
                                for endpoint in ('start','end'):
                                    delta=subtract(span[endpoint],member['start'])
                                    station=dot(delta,direction)
                                    self.assertGreaterEqual(station,-1e-7)
                                    self.assertLessEqual(station,member['axisLength']+1e-7)
                                    self.assert_vector_close(delta,[station*value for value in direction])
                                    if 'source'+endpoint.title()+'Station' in span:
                                        self.assertAlmostEqual(station,span['source'+endpoint.title()+'Station'],places=7)
                                self.assertGreaterEqual(span['stockStart'], -1e-7)
                                self.assertGreaterEqual(span['startReserve'], -1e-7)
                                self.assertGreaterEqual(span['endReserve'], -1e-7)
                                placement = span['placement']
                                for key in ('origin', 'xAxis', 'yAxis', 'zAxis'):
                                    self.assertEqual(3, len(placement[key]))
                                    self.assertTrue(all(math.isfinite(value) for value in placement[key]))
                    self.assertEqual(set(design), mapped)

    def test_three_face_spatial_blank_keeps_eight_sources_and_split_seam_mapping(self):
        document = generate('three', 'spatial_v_notch')
        stock = outer_frames(output_items(document, 'export.manufacturing'))
        continuous = stock['outer_frame.spatial.continuous']
        sources = continuous['properties']['manufacturing.sourceMembers']
        self.assertEqual(8, len(sources))
        self.assertEqual(9, sum(len(source['spans']) for source in sources))
        self.assertEqual(1, sum(len(source['spans']) == 2 for source in sources))
        standalone_posts = set(stock) - {'outer_frame.spatial.continuous'}
        self.assertEqual({'outer_frame.vertical.0002', 'outer_frame.vertical.0003'}, standalone_posts)
        expected_members = {f'outer_frame.{edge}.{index:04d}'
                            for edge in ('top', 'bottom') for index in range(1, 4)}
        expected_members.update({'outer_frame.vertical.0001', 'outer_frame.vertical.0004'})
        self.assertEqual(expected_members, {source['itemKey'] for source in sources})

    def test_combined_process_records_reference_namespaced_geometry_and_no_secondary_cuts(self):
        for face in ('single', 'three'):
            with self.subTest(face=face):
                document=generate(face,'spatial_v_notch')
                geometry={node['key'] for node in document['geometry']}
                roots=[root for item in output_items(document,'export.manufacturing').values()
                       for root in item['properties'].get('tubeDesigner.assemblyProcessSecondaryCutters',[])]
                self.assertEqual([], roots)
                records=document['extensions']['tubeDesigner.assemblyGeometryProcesses']['instances']
                self.assertTrue(records)
                runtime=SUBJECT._frame_geometry._machining._load('assembly_geometry_process_runtime')
                for record in records:
                    for field in ('targetGeometry','resultGeometry'):
                        self.assertTrue(record[field].startswith('stock.geometry.'))
                        self.assertIn(record[field],geometry)
                    self.assertTrue(set(record['result'].get('externalGeometry',[])) <= geometry)
                    self.assertEqual(record['identity'],runtime._result_identity(record['stockId'],record['result']))
                    self.assertEqual(record['executionSignature'],runtime._execution_signature(record['targetGeometry'],record['result']))
                    self.assertIn('sourceIdentity',record)

    def test_five_face_closed_frames_process_both_ends_of_all_four_posts(self):
        for mode in ('plane_v_notch','spatial_v_notch'):
            with self.subTest(mode=mode):
                document=generate('five',mode,'manufacturing')
                source=document['extensions']['tubeDesigner.assemblyProcessSource']
                posts={stock['id'] for stock in source['stocks'] if stock['id'].startswith('outer_frame.vertical.')}
                self.assertEqual({f'outer_frame.vertical.{index:04d}' for index in range(1,5)},posts)
                calls=[instance for instance in source['instances'] if instance['templateId']=='orthogonal-corner']
                self.assertEqual(8,len(calls))
                for post in posts:
                    ends={instance['processInput']['parts']['memberC']['anchor']['end']
                          for instance in calls if instance['targets']['memberC']==post}
                    self.assertEqual({'start','end'},ends)

    def test_three_face_four_nodes_have_three_independent_orthogonal_members(self):
        expected_keys = {f'outer_frame.corner.{level}.{index:04d}'
                         for level in ('top', 'bottom') for index in (1, 2)}
        reference = None
        for mode in MODES:
            with self.subTest(mode=mode):
                document = generate('three', mode, 'display')
                design = output_items(document, 'display.default')
                nodes = {node['key']: node for node in document['relationships']
                         if node['key'] in expected_keys}
                self.assertEqual(expected_keys, set(nodes))
                if reference is None:
                    reference = nodes
                else:
                    self.assertEqual(reference, nodes)
                for key, node in nodes.items():
                    self.assertEqual('assembly', node['kind'])
                    self.assertEqual('orthogonal-corner', node['properties']['topology'])
                    self.assertEqual(3, len(set(node['items'])))
                    level = key.split('.')[2]
                    self.assertTrue(all(member.startswith(f'outer_frame.{level}.') for member in node['items'][:2]))
                    self.assertTrue(node['items'][2].startswith('outer_frame.vertical.'))
                    point = node['properties']['centerlinePoint']
                    self.assert_vector_close(node['properties']['origin'], point)
                    axes = []
                    for member_key in node['items']:
                        self.assertIn(member_key, design)
                        member = self.assert_logical_member(design[member_key])
                        self.assertTrue(any(math.dist(point, member[endpoint]) < 1e-6
                                            for endpoint in ('start', 'end')), member_key)
                        axes.append(unit(subtract(member['end'], member['start'])))
                    for first, second in ((0, 1), (0, 2), (1, 2)):
                        self.assertAlmostEqual(0, dot(axes[first], axes[second]), places=6)

    def test_opening_frames_and_hinges_keep_logical_identity_in_every_face_and_mode(self):
        for face in DESIGN_COUNTS:
            expected = None
            for mode in MODES:
                with self.subTest(face=face, mode=mode):
                    document = generate(face, mode, opening=True)
                    display = generate(face, mode, 'display', opening=True)
                    self.assertNotIn('tubeDesigner.manufacturingPartCount', display['extensions'])
                    self.assertEqual(set(output_items(document, 'display.default')),
                                     set(output_items(display, 'display.default')))
                    design = output_items(document, 'display.default')
                    fixed = {key: item for key, item in design.items()
                             if key.startswith('access_door.fixed_frame.')}
                    leaf = {key: item for key, item in design.items()
                            if key.startswith('access_door.leaf.frame.')}
                    self.assertEqual(4, len(fixed))
                    self.assertEqual(4, len(leaf))
                    frame_keys = set(fixed) | set(leaf)
                    self.assertFalse(any('continuous' in key for key in frame_keys))
                    for item in [*fixed.values(), *leaf.values()]:
                        self.assert_logical_member(item)
                    hinges = [relation for relation in document['relationships']
                              if relation['kind'] == 'hinge']
                    self.assertTrue(hinges)
                    for hinge in hinges:
                        self.assertEqual(2, len(hinge['items']))
                        self.assertTrue(set(hinge['items']) <= frame_keys)
                        self.assertEqual(1, sum(key in fixed for key in hinge['items']))
                        self.assertEqual(1, sum(key in leaf for key in hinge['items']))
                    snapshot = (frame_keys, {key: item['properties']['assemblyFrame.member']
                                             for key, item in {**fixed, **leaf}.items()}, hinges)
                    if expected is None:
                        expected = snapshot
                    else:
                        self.assertEqual(expected, snapshot)


if __name__ == '__main__':
    unittest.main()
