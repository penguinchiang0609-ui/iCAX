"""Current public window entry, independent route combinations and stock integrity.

This deliberately uses the current consolidated descriptor, never removed
variant packages. Completed BRep equivalence/quantity grouping is native-only
and is not claimed by these neutral-model checks.
"""
from copy import deepcopy
import itertools
import json
import math
import os
from pathlib import Path
import sys
import unittest

from WindowCatalogueTests import package
from WindowManufacturingInputTests import load, forbid_product_callbacks, forbid_product_processing
from FoldedPostConnectionTests import full_plan
from icax_template_sdk.manufacturing import compose_manufacturing_model
import icax_template_worker as worker


SCENARIOS = []
LAYOUTS = [('single', 'right'), ('two', 'left'), ('two', 'right'), ('three', 'right')]
JOIN_TYPES = ('miter_45', 'butt_90', 'v_groove_90:tool_library')
TOOLS = ('system:v-notch-sharp', 'system:edge-arc-groove')


def unit(vector):
    length = math.sqrt(sum(value * value for value in vector))
    return [value / length for value in vector]


def dot(first, second):
    return sum(a * b for a, b in zip(first, second))


def cross(first, second):
    return [first[1]*second[2]-first[2]*second[1], first[2]*second[0]-first[0]*second[2], first[0]*second[1]-first[1]*second[0]]


class SecurityWindowPublicReleaseMatrixTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.descriptor, cls.defaults, cls.product = package('single_face_security_window')

    def verify(self, label, values, complete_plan=False):
        original = deepcopy(values)
        with forbid_product_processing():
            design = self.product.display(values)
            declaration = self.product.manufacturing(values)
        self.assertEqual(values, original, label + ': product mutated host input')
        members = {item['key']: item for item in design['items']}
        self.assertEqual(len(members), len(design['items']))
        for key, item in members.items():
            member = item['properties']['assemblyFrame.member']
            start, end = member['start'], member['end']
            self.assertTrue(all(math.isfinite(value) for value in (*start, *end)), key)
            self.assertGreater(math.dist(start, end), 0, key)
            self.assertAlmostEqual(math.dist(start, end), member['axisLength'], places=6, msg=key)
            frame = member['sectionFrame']
            x, y, z = [frame[axis] for axis in ('xAxis', 'yAxis', 'zAxis')]
            for vector in (x, y, z):
                self.assertAlmostEqual(dot(vector, vector), 1, places=6, msg=key)
            self.assertAlmostEqual(dot(x, y), 0, places=6, msg=key)
            self.assertGreater(dot(cross(x, y), z), .999999, key)
            self.assertGreater(dot(z, unit([b-a for a, b in zip(start, end)])), .999999, key)
        snapshot = deepcopy((design, declaration))
        with forbid_product_callbacks():
            composed = compose_manufacturing_model(declaration, design)
            compiled = load('assembly_window_process').compile_window_assembly(composed)
            compiled_before = deepcopy(compiled)
            allocation = load('assembly_material_allocation').allocate_window_manufacturing(compiled)
            if complete_plan:
                self.assertTrue(full_plan(allocation)['operations'], label)
        self.assertEqual(snapshot, (design, declaration), label + ': downstream mutated design/declaration')
        self.assertEqual(compiled_before, compiled, label + ': allocation mutated route input')
        mapped = [source['itemKey'] for mapping in compiled['sourceMappings'] for source in mapping['sources']]
        self.assertEqual(set(mapped), set(members), label + ': missing design member mapping')
        self.assertEqual(len(mapped), len(members), label + ': duplicate design member mapping')
        stocks = allocation['model']['items']
        stock_keys = {item['key'] for item in stocks}
        self.assertEqual(len(stock_keys), len(stocks))
        for item in stocks:
            self.assertTrue(math.isfinite(item['properties']['length']))
            self.assertGreater(item['properties']['length'], 0, item['key'])
            self.assertEqual(item['properties'].get('quantity', 1), 1)
        for call in allocation['assemblySource']['instances']:
            targets = {target['stockId'] if isinstance(target, dict) else target
                       for target in call.get('targets', {}).values()}
            self.assertTrue(targets <= stock_keys, call['instanceId'])
        self.assertEqual(values, original, label + ': whole pipeline changed public input')
        SCENARIOS.append({'label': label, 'face': values['faceType'], 'side': values['sidePosition'],
                          'members': len(members), 'stocksBeforeNativeGrouping': len(stocks),
                          'completePlan': complete_plan})
        trace = os.environ.get('ICAX_WINDOW_AUDIT_TRACE')
        if trace:
            with Path(trace).open('a', encoding='utf-8') as output:
                output.write(json.dumps(SCENARIOS[-1], ensure_ascii=False) + '\n')
        return design, declaration, allocation

    def test_all_frame_routes_and_independent_fixed_leaf_process_pairs(self):
        for face, side in LAYOUTS:
            modes = ('segment_weld', 'plane_v_notch') if face == 'single' else ('segment_weld', 'plane_v_notch', 'spatial_v_notch')
            for mode in modes:
                for index, (fixed, leaf) in enumerate(itertools.product(JOIN_TYPES, repeat=2)):
                    label = f'{face}-{side}-{mode}-fixed-{fixed}-leaf-{leaf}'
                    with self.subTest(label=label):
                        values = {**deepcopy(self.defaults), 'faceType': face, 'sidePosition': side,
                                  'frameManufacturingMode': mode, 'accessDoorEnabled': True,
                                  'doorFrameJoinType': fixed, 'doorLeafFrameJoinType': leaf,
                                  'outerFrameGrooveTool': TOOLS[index % 2],
                                  'doorFrameGrooveTool': TOOLS[(index + 1) % 2],
                                  'doorLeafFrameGrooveTool': TOOLS[index % 2],
                                  'outerFrameVGrooveBottomStrategy': 'rounded',
                                  'doorFrameVGrooveBottomStrategy': 'rounded',
                                  'doorLeafFrameVGrooveBottomStrategy': 'rounded',
                                  'outerFrameVGrooveRoundRadius': 2., 'doorFrameVGrooveRoundRadius': 1.5,
                                  'doorLeafFrameVGrooveRoundRadius': 1.,
                                  'outerFrameBendKFactor': .42, 'doorFrameBendKFactor': .31,
                                  'doorLeafFrameBendKFactor': .55,
                                  'outerFrameVGrooveMaleFemale': True,
                                  'doorFrameVGrooveMaleFemale': True,
                                  'doorLeafFrameVGrooveMaleFemale': True}
                        self.verify(label, values, complete_plan=(index in (0, 4, 8)))

    def test_disabled_opening_keeps_all_hidden_process_drafts_without_effect(self):
        for face, side in LAYOUTS:
            values = {**deepcopy(self.defaults), 'faceType': face, 'sidePosition': side, 'accessDoorEnabled': False}
            baseline = self.product.display(values)
            values.update(doorFrameJoinType='v_groove_90:tool_library', doorLeafFrameJoinType='v_groove_90:tool_library',
                          doorFrameGrooveTool='unused-invalid-tool', doorLeafFrameGrooveTool='unused-invalid-tool',
                          doorFrameBendKFactor=-1., doorLeafFrameBendKFactor=-1.,
                          doorFrameVGrooveRoundRadius=-1., doorLeafFrameVGrooveRoundRadius=-1.)
            with self.subTest(face=face, side=side):
                design, _, allocation = self.verify(f'{face}-{side}-opening-disabled-drafts', values, True)
                self.assertEqual(design, baseline)
                self.assertFalse(any(key.startswith('access_door.') for key in [item['key'] for item in allocation['model']['items']]))

    def test_small_and_larger_real_sections_generate_finite_positive_stocks(self):
        for frame, horizontal, vertical, wall in ((30., 20., 16., .8), (50., 30., 25., 1.2)):
            for face, side in LAYOUTS:
                with self.subTest(section=(frame, horizontal, vertical), face=face, side=side):
                    values = {**deepcopy(self.defaults), 'faceType': face, 'sidePosition': side,
                              'accessDoorEnabled': False, 'frameWidth': frame, 'frameDepth': frame,
                              'frameWallThickness': wall, 'horizontalWidth': horizontal, 'horizontalDepth': horizontal,
                              'horizontalWallThickness': wall, 'verticalWidth': vertical, 'verticalWallThickness': wall,
                              'frameManufacturingMode': 'plane_v_notch'}
                    self.verify(f'{face}-{side}-section-{frame}-{horizontal}-{vertical}', values, True)

    def test_current_public_entry_preserves_normalized_host_input(self):
        for face, side in LAYOUTS:
            for purpose in ('display', 'manufacturing'):
                with self.subTest(face=face, side=side, purpose=purpose):
                    values = {**deepcopy(self.defaults), 'faceType': face, 'sidePosition': side, 'accessDoorEnabled': True,
                              'doorFrameJoinType': 'v_groove_90:tool_library', 'doorLeafFrameJoinType': 'butt_90',
                              'doorFrameVGrooveBottomStrategy': 'rounded', 'doorFrameVGrooveRoundRadius': 2.}
                    saved = deepcopy(values)
                    document = worker._evaluate({'template': self.descriptor, 'parameters': values,
                        'templatePath': str(Path(__file__).resolve().parents[5] / 'apps/tube-designer/templates/product/single_face_security_window/template.py'),
                        'context': {'geometryPurpose': purpose}})
                    self.assertEqual(values, saved)
                    self.assertEqual(set(values), set(self.defaults))
                    self.assertEqual(document['schema'], 'icax.' + purpose + '-model')
                    self.assertNotIn('parameters', document)
                    self.assertNotIn('template', document)
                    # The current pure declarations have no host envelope.
                    # Check the construction boundary's exact echo separately;
                    # Native acceptance verifies the SDO installs this snapshot.
                    envelope = self.product._generate_resource_document(values, {'template': self.descriptor, 'geometryPurpose': 'display'})
                    self.assertEqual(envelope['parameters'], saved)
                    self.assertIsNot(envelope['parameters'], values)

    def test_five_face_current_routes_openings_and_inactive_top_draft(self):
        for mode in ('segment_weld', 'plane_v_notch', 'spatial_v_notch'):
            for opening in (False, True):
                for fixed, leaf in itertools.product(JOIN_TYPES, repeat=2):
                    values = {**deepcopy(self.defaults), 'faceType': 'five', 'frameManufacturingMode': mode,
                              'accessDoorEnabled': opening, 'doorFrameJoinType': fixed, 'doorLeafFrameJoinType': leaf}
                    label = f'five-{mode}-opening-{opening}-fixed-{fixed}-leaf-{leaf}'
                    with self.subTest(label=label):
                        self.verify(label, values)
        for face in ('single', 'two', 'three'):
            values = {**deepcopy(self.defaults), 'faceType': face, 'accessDoorFace5': 'top'}
            baseline = {**values, 'accessDoorFace5': 'front'}
            self.assertEqual(self.product.display(values), self.product.display(baseline))
            self.assertEqual(self.product.manufacturing(values), self.product.manufacturing(baseline))
        with self.assertRaisesRegex(ValueError, '顶面'):
            self.product.display({**self.defaults, 'faceType': 'five', 'accessDoorFace5': 'top'})

    def test_unsupported_spatial_face_ignores_hidden_outer_fold_drafts(self):
        for face in ('single', 'five'):
            for tool in TOOLS:
                baseline = {**deepcopy(self.defaults), 'faceType': face,
                            'frameManufacturingMode': 'spatial_v_notch', 'outerFrameGrooveTool': tool}
                draft = {**baseline, 'outerFrameBendKFactor': -1., 'outerFrameFoldBridge': -1.,
                         'outerFrameVGrooveMaleFemale': 'invalid hidden draft',
                         'outerFrameVGrooveBottomStrategy': 'invalid hidden draft',
                         'outerFrameVGrooveRoundRadius': -1.}
                saved = deepcopy(draft)
                with self.subTest(face=face, tool=tool):
                    self.assertEqual(self.product.display(draft), self.product.display(baseline))
                    self.assertEqual(self.product.manufacturing(draft), self.product.manufacturing(baseline))
                    self.assertEqual(draft, saved)

    def test_unavailable_external_planner_draft_uses_current_builtin_route(self):
        for overrides in (
            {'faceType': 'single', 'frameLayout': 'left_right',
             'accessDoorEnabled': False, 'infillPattern': 'vertical'},
            {'faceType': 'single', 'frameLayout': 'four_sides',
             'accessDoorEnabled': True, 'infillPattern': 'vertical'},
            {'faceType': 'single', 'frameLayout': 'four_sides',
             'accessDoorEnabled': False, 'infillPattern': 'grid'},
            {'faceType': 'two', 'frameLayout': 'four_sides',
             'accessDoorEnabled': False, 'infillPattern': 'vertical'},
        ):
            draft = {**deepcopy(self.defaults), **overrides,
                     'assemblyPlanningMode': 'external_templates',
                     'frameManufacturingMode': 'plane_v_notch'}
            baseline = {**draft, 'assemblyPlanningMode': 'builtin_rules'}
            saved = deepcopy(draft)
            with self.subTest(**overrides):
                design, declaration, _ = self.verify(
                    'inactive-external-' + str(overrides), draft,
                    complete_plan=overrides['frameLayout'] == 'four_sides')
                self.assertEqual(design, self.product.display(baseline))
                self.assertEqual(declaration, self.product.manufacturing(baseline))
                self.assertEqual(draft, saved)
                envelope = self.product._generate_resource_document(draft,
                    {'template': self.descriptor, 'geometryPurpose': 'display'})
                self.assertEqual(envelope['parameters'], saved)
        for pattern in ('horizontal', 'vertical'):
            values = {**deepcopy(self.defaults), 'faceType': 'single',
                      'frameLayout': 'four_sides', 'accessDoorEnabled': False,
                      'infillPattern': pattern, 'assemblyPlanningMode': 'external_templates'}
            with self.subTest(activePattern=pattern):
                # The still-available choice must continue selecting independent
                # node templates instead of silently becoming built-in rules.
                declaration = self.product.manufacturing(values)
                self.assertEqual(declaration['processes'][0]['definition']['parameters']['assemblyPlanningMode'],
                                 'external_templates')

    def test_independent_opening_frames_on_each_current_side_and_bottom_plane(self):
        for face, side, selector, selected, expected_name in (
            ('two', 'left', 'accessDoorFace2', 'side', '左侧面'),
            ('two', 'right', 'accessDoorFace2', 'side', '右侧面'),
            ('three', 'right', 'accessDoorFace3', 'left', '左侧面'),
            ('three', 'right', 'accessDoorFace3', 'right', '右侧面'),
            ('five', 'right', 'accessDoorFace5', 'left', '左侧面'),
            ('five', 'right', 'accessDoorFace5', 'right', '右侧面'),
            ('five', 'right', 'accessDoorFace5', 'bottom', '下面')):
            with self.subTest(face=face, side=side, opening=selected):
                values = {**deepcopy(self.defaults), 'faceType': face, 'sidePosition': side,
                    selector: selected, 'doorClearWidth': 250., 'doorClearHeight': 250.,
                    'doorUOffset': 100., 'doorVOffset': 100.,
                    'doorHorizontalTopCenterOffset': 99., 'doorHorizontalBottomCenterOffset': 99.,
                    'doorFrameJoinType': 'miter_45', 'doorLeafFrameJoinType': 'v_groove_90:tool_library'}
                design, _, allocation = self.verify(f'{face}-{side}-opening-{selected}', values)
                frames = [item for item in design['items']
                          if item['key'].startswith('access_door.leaf.frame.')]
                self.assertEqual(len(frames), 4)
                self.assertEqual({item['properties']['tubeDesigner.faceName'] for item in frames}, {expected_name})
                self.assertEqual(sum(item['key'].startswith('access_door.leaf.frame.')
                                     for item in allocation['model']['items']), 1)

    def test_clear_opening_leaf_projection_gap_and_actual_grid_clearances(self):
        # Preserve the old physical opening assertions on today's public items.
        for face, side in (*LAYOUTS, ('five', 'right')):
            for frame_width, leaf_depth in ((25., 20.), (32., 24.)):
                with self.subTest(face=face, side=side, frame=frame_width, leafDepth=leaf_depth):
                    values = {**deepcopy(self.defaults), 'faceType': face, 'sidePosition': side,
                              'doorFrameWidth': frame_width, 'doorLeafFrameDepth': leaf_depth}
                    design = self.product.display(values)
                    members = {item['key']: item['properties']['assemblyFrame.member'] for item in design['items']}
                    fixed = [members['access_door.fixed_frame.' + s + '.0001']['start']
                             for s in ('left', 'right', 'bottom', 'top')]
                    self.assertAlmostEqual(math.dist(fixed[0], fixed[1]) - frame_width,
                        values['doorClearWidth'] + leaf_depth)
                    self.assertAlmostEqual(math.dist(fixed[2], fixed[3]) - frame_width,
                                           values['doorClearHeight'])
                    left, right = [members['access_door.leaf.frame.' + s + '.0001']['start']
                                   for s in ('left', 'right')]
                    axis = unit([b-a for a, b in zip(left, right)])
                    projection = lambda a, b: dot([y-x for x, y in zip(a, b)], axis)
                    self.assertAlmostEqual(projection(fixed[0], left) -
                        (frame_width + values['doorLeafFrameWidth']) / 2., values['doorGap'])
                    positions = sorted(projection(left, item['start']) for key, item in members.items()
                                       if key.startswith('access_door.leaf.vertical.'))
                    self.assertTrue(positions)
                    half_frame, half_bar = values['doorLeafFrameWidth'] / 2., values['doorVerticalWidth'] / 2.
                    gaps = [positions[0] - half_frame - half_bar,
                            projection(left, right) - half_frame - positions[-1] - half_bar]
                    gaps.extend(b-a-2*half_bar for a, b in zip(positions, positions[1:]))
                    self.assertGreaterEqual(min(gaps), -1.e-7)
                    self.assertLessEqual(max(gaps), values['doorVerticalMaximumCenterSpacing'] + 1.e-7)
                    self.assertEqual(len([r for r in self.product._generate_resource_document(values,
                        {'template': self.descriptor, 'geometryPurpose': 'display'})['relationships']
                        if r['kind'] == 'hinge']), 2)

    def test_current_material_presets_have_real_receiver_clearance_and_complete_plans(self):
        for preset in self.descriptor['extensions']['parameterPresets']['presets']:
            for face, side in (*LAYOUTS, ('five', 'right')):
                values = {**deepcopy(self.defaults), **deepcopy(preset['values']),
                          'tubeSpecificationPreset': preset['value'], 'faceType': face, 'sidePosition': side}
                with self.subTest(preset=preset['value'], face=face, side=side):
                    inner = values['doorFrameDepth'] - 2 * values['doorFrameWallThickness']
                    self.assertGreater(inner, values['horizontalDepth'] + 2 * values['assemblyClearance'])
                    design, _, _ = self.verify('material-preset-' + preset['value'] + '-' + face + '-' + side,
                                              values, True)
                    fixed = next(item for item in design['items']
                                 if item['key'].startswith('access_door.fixed_frame.'))
                    profile = fixed['properties']['tubeDesigner.profile']
                    self.assertEqual(profile['width'], values['doorFrameWidth'])
                    self.assertEqual(profile['depth'], values['doorFrameDepth'])
                    self.assertEqual(profile['wallThickness'], values['doorFrameWallThickness'])

    def test_edge_arc_folded_corner_post_uses_supported_tabs_without_mutating_draft(self):
        for face, side, mode in (
            ('two', 'left', 'plane_v_notch'), ('two', 'right', 'plane_v_notch'),
            ('two', 'left', 'spatial_v_notch'), ('two', 'right', 'spatial_v_notch'),
            ('three', 'right', 'plane_v_notch'), ('three', 'right', 'spatial_v_notch'),
            ('five', 'right', 'plane_v_notch'), ('five', 'right', 'spatial_v_notch'),
        ):
            values = {**deepcopy(self.defaults), 'faceType': face, 'sidePosition': side,
                      'frameManufacturingMode': mode, 'outerFrameGrooveTool': 'system:edge-arc-groove',
                      'foldedPostJoint': 'weld'}
            saved = deepcopy(values)
            with self.subTest(face=face, side=side, mode=mode):
                design, declaration, allocation = self.verify(f'edge-arc-default-post-{face}-{side}-{mode}', values, True)
                selected = declaration['processes'][0]['definition']['parameters']
                self.assertEqual(selected['foldedPostJoint'], 'tabs')
                self.assertEqual(selected['frameManufacturingMode'], 'plane_v_notch' if face == 'five' else mode)
                post_calls = [call for call in allocation['assemblySource']['instances']
                              if call['instanceId'].endswith('.post')]
                self.assertTrue(post_calls)
                self.assertTrue(all(call['parameters']['cJoint'] == 'tabs' for call in post_calls))
                if not (face == 'five' and mode == 'spatial_v_notch'):
                    tabs = {**values, 'foldedPostJoint': 'tabs'}
                    self.assertEqual(design, self.product.display(tabs))
                    self.assertEqual(declaration, self.product.manufacturing(tabs))
                else:
                    hidden = {**values, 'foldedPostJoint': 'tabs', 'foldedPostInsertDepth': -1.,
                              'foldedPostFitGap': -1., 'foldedPostTabWidth': -1.,
                              'foldedPostTabLength': -1., 'foldedPostSideClearance': -1.}
                    self.assertEqual(design, self.product.display(hidden))
                    self.assertEqual(declaration, self.product.manufacturing(hidden))
                self.assertEqual(values, saved)
                self.assertEqual(self.product._generate_resource_document(values,
                    {'template': self.descriptor, 'geometryPurpose': 'display'})['parameters'], saved)

        for overrides in (
            {'faceType': 'single', 'frameManufacturingMode': 'plane_v_notch', 'outerFrameGrooveTool': 'system:edge-arc-groove'},
            {'faceType': 'two', 'frameManufacturingMode': 'segment_weld', 'outerFrameGrooveTool': 'system:edge-arc-groove'},
            {'faceType': 'three', 'frameManufacturingMode': 'plane_v_notch', 'outerFrameGrooveTool': 'system:v-notch-sharp'},
            {'faceType': 'three', 'frameManufacturingMode': 'spatial_v_notch', 'outerFrameGrooveTool': 'system:v-notch-sharp',
             'outerFrameVGrooveBottomStrategy': 'rounded'},
        ):
            with self.subTest(unaffected=overrides):
                values = {**deepcopy(self.defaults), **overrides, 'foldedPostJoint': 'weld'}
                selected = self.product.manufacturing(values)['processes'][0]['definition']['parameters']
                self.assertEqual(selected['foldedPostJoint'], 'weld')

        # Applicability follows an explicitly bound resource instead of an
        # unused encoded selector; custom resources are not guessed from names.
        posts = load('security_window_post_connections')
        for encoded, bound, expected in (
            ('system:v-notch-sharp', 'edge-arc-groove', 'tabs'),
            ('system:edge-arc-groove', 'v-notch-sharp', 'weld'),
        ):
            with self.subTest(encoded=encoded, bound=bound):
                values = {**deepcopy(self.defaults), 'faceType': 'two', 'frameManufacturingMode': 'plane_v_notch',
                          'outerFrameGrooveTool': encoded,
                          'tubeDesignerToolBindings': {'outerFrameGroove': {'ref': {'scope': 'system', 'id': bound}}}}
                self.assertEqual(posts.process_choices(values)['foldedPostJoint'], expected)


if __name__ == '__main__':
    unittest.main()
