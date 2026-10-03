"""Automatic between-beam posts use real end cuts and closed receiver walls."""
from copy import deepcopy
import math
import unittest

from FoldedPostConnectionTests import window, allocate, full_plan
from WindowManufacturingInputTests import load, package, execute, forbid_product_processing
from AssemblyProcessStageWindowTests import semantic_snapshot


def is_post(key):
    return key.startswith(('outer_frame.vertical.', 'outer_frame.left.', 'outer_frame.right.'))


def frame_items(model):
    return {item['key']: item for item in model['items'] if item['key'].startswith('outer_frame.')}


def end_calls(source, post):
    return [call for call in source['instances']
            if call['templateId'] == 'tube-post-end' and call['targets']['stock'] == post]


class SegmentedPostConnectionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cases = {}
        for face in ('single', 'two', 'three', 'five'):
            for joint in ('insert', 'tabs'):
                inputs = window(joint, faceType=face, frameManufacturingMode='segment_weld', sideWidth=600)
                cls.cases[(face, joint)] = (*inputs, allocate(inputs[-1]))

    def test_all_independent_posts_have_two_actual_end_cuts_and_selected_sections(self):
        for (face, joint), (values, _, design, _, allocation) in self.cases.items():
            with self.subTest(face=face, joint=joint):
                expected_count = {'single': 2, 'two': 3, 'three': 4, 'five': 4}[face]
                frames = frame_items(allocation['model'])
                posts = {key: item for key, item in frames.items() if is_post(key)}
                self.assertEqual(len(posts), expected_count)
                self.assertFalse(any('continuous' in key for key in frames))
                for key, item in posts.items():
                    props = item['properties']
                    self.assertEqual(props['tubeDesigner.profile']['width'], 30.)
                    self.assertEqual(props['tubeDesigner.profile']['depth'], 30.)
                    self.assertAlmostEqual(props['length'], 948.)
                    calls = end_calls(allocation['assemblySource'], key)
                    self.assertEqual(len(calls), 2, key)
                    self.assertEqual({call['processInput']['geometry']['end'] for call in calls},
                                     {'start', 'end'})
                for key, item in frames.items():
                    if not is_post(key):
                        self.assertEqual(item['properties']['tubeDesigner.profile']['width'], 38.)
                # The product owns the same actual post profile and intrinsic
                # centreline facts; manufacturing cannot invent another C.
                self.assertEqual({key for key in frame_items(design) if is_post(key)}, set(posts))
                self.assertEqual(values['foldedPostJoint'], joint)

    def test_full_tool_plan_is_fold_free_and_apertures_wait_for_real_end_writers(self):
        for (face, joint), (_, _, _, compiled, allocation) in self.cases.items():
            with self.subTest(face=face, joint=joint):
                calls = {call['key']: call for call in compiled['processes']}
                graph = allocation['stagePlan']['dependencies']
                self.assertFalse(any(call['definition']['templateId'] == 'node-v-notch-integrated'
                                     for call in calls.values()))
                apertures = [call for call in calls.values()
                             if call['definition']['templateId'] == 'tube-post-aperture']
                self.assertEqual(len(apertures), {'single': 4, 'two': 8, 'three': 12, 'five': 16}[face])
                for call in apertures:
                    definition = call['definition']
                    branch = definition['processInput']['parts']['branch']['itemKey']
                    host = definition['targets']['stock']
                    ends = {key for key, other in calls.items()
                            if other['definition']['templateId'] == 'tube-post-end'
                            and other['definition']['targets']['stock'] == branch}
                    self.assertEqual(len(ends), 2)
                    self.assertTrue(ends <= set(graph[call['key']]))
                    host_writers = {key for key, other in calls.items()
                                    if other['definition']['templateId'] == 'tube-end-joint'
                                    and other['definition']['targets']['stock'] == host}
                    self.assertTrue(host_writers <= set(graph[call['key']]))
                plan = full_plan(allocation)
                source_ids = {call['instanceId'] for call in allocation['assemblySource']['instances']
                              if call['templateId'] in ('tube-post-end', 'tube-post-aperture')}
                self.assertTrue(source_ids <= {op['instanceId'] for op in plan['operations']})
                for part in plan['manufacturingParts']:
                    for operation in part['request'].get('neutralOperations', []):
                        if operation['instanceId'] not in source_ids:
                            continue
                        for node in operation['geometry']:
                            if node['operator'] == 'transform':
                                placement = node['arguments'].get('placement')
                                self.assertIsInstance(placement, dict)
                                self.assertTrue({'origin', 'xAxis', 'yAxis', 'zAxis'} <= set(placement))
                positions = {key: i for i, key in enumerate(allocation['stagePlan']['orderedKeys'])}
                self.assertTrue(all(positions[dep] < positions[key] for key, deps in graph.items() for dep in deps))

    def test_terminal_display_beams_preserve_outer_dimensions_and_logical_nodes(self):
        for (face, joint), (_, _, design, _, allocation) in self.cases.items():
            with self.subTest(face=face, joint=joint):
                items = frame_items(design)
                resources = {node['key']: node for node in design['resources']}
                checked = 0
                for post, item in items.items():
                    if not is_post(post):
                        continue
                    for endpoint in ('start', 'end'):
                        point = item['properties']['assemblyFrame.member'][endpoint]
                        rails = [rail for key, rail in items.items() if not is_post(key)
                                 and min(math.dist(point, rail['properties']['assemblyFrame.member'][side])
                                         for side in ('start', 'end')) < 1.e-6]
                        if len(rails) != 1:
                            continue
                        rail = rails[0]
                        member = rail['properties']['assemblyFrame.member']
                        side = min(('start', 'end'), key=lambda end: math.dist(point, member[end]))
                        geometry = rail['geometry']
                        extrusion = resources[geometry['resource']]
                        self.assertEqual(extrusion['operator'], 'extrude')
                        pose = geometry['placement']
                        actual = list(pose['origin'])
                        if side == 'end':
                            vector = extrusion['arguments']['vector']
                            actual = [actual[i] + sum(vector[j] * pose[axis][i]
                                      for j, axis in enumerate(('xAxis', 'yAxis', 'zAxis'))) for i in range(3)]
                        self.assertAlmostEqual(math.dist(actual, point), 19.)
                        self.assertEqual(member[side], point)
                        allocations = [entry for entry in allocation['materialPlan']['initialMaterialAllocations']
                                       if entry['stockId'] == rail['key'] and entry['end'] == side]
                        self.assertEqual(len(allocations), 1)
                        self.assertEqual(allocations[0]['point'], actual)
                        checked += 1
                self.assertEqual(checked, 0 if face == 'five' else 4)

    def test_weld_layout_drafts_and_hidden_planner_do_not_change_active_geometry(self):
        # These are retained editor drafts, not additional effective layouts.
        for (face, joint), (_, _, design, _, expected) in self.cases.items():
            changes = ([{'frameJoinType': join, 'frameButtWrapMode': wrap}
                        for join in ('miter_45', 'butt_90', 'v_groove_90:tool_library')
                        for wrap in ('side_wraps_horizontal', 'horizontal_wraps_side')]
                       if face == 'single' else
                       [{'frameCornerJoin': corner, 'assemblyPlanningMode': planner}
                        for corner in ('post_butt', 'rail_miter')
                        for planner in ('builtin_rules', 'external_templates')])
            for drafts in changes:
                with self.subTest(face=face, joint=joint, drafts=drafts):
                    values, declaration, actual_design, compiled = window(
                        joint, faceType=face, frameManufacturingMode='segment_weld', sideWidth=600, **drafts)
                    original = deepcopy(values)
                    actual = allocate(compiled)
                    self.assertEqual(actual_design, design)
                    self.assertEqual(actual['model'], expected['model'])
                    self.assertEqual(actual['assemblySource'], expected['assemblySource'])
                    self.assertEqual(values, original)
                    descriptor, _, core = package('single_face_security_window')
                    output = load('security_window_product_contract')._design(
                        values, {'template': descriptor, 'geometryPurpose': 'display'}, core)
                    self.assertEqual(output['parameters'], original)

    def test_reversed_submission_and_public_worker_preserve_frozen_material_and_tools(self):
        for face in ('single', 'three', 'five'):
            with self.subTest(face=face):
                values, declaration, design, compiled, allocation = self.cases[(face, 'tabs')]
                reversed_input = deepcopy(compiled)
                reversed_input['processes'].reverse()
                actual = allocate(reversed_input)
                self.assertEqual(semantic_snapshot(actual, full_plan(actual)),
                                 semantic_snapshot(allocation, full_plan(allocation)))
                before = deepcopy((values, declaration, design, compiled))
                result = execute(declaration, design)
                self.assertEqual((values, declaration, design, compiled), before)
                self.assertEqual(result['extensions']['tubeDesigner.assemblyProcessSource'], allocation['assemblySource'])
                self.assertEqual(result['extensions']['tubeDesigner.assemblyStagePlan'], allocation['stagePlan'])

    def test_incomplete_or_external_single_frames_ignore_invalid_connection_drafts(self):
        descriptor, defaults, core = package('single_face_security_window')
        invalid = {'foldedPostWidth': 1., 'foldedPostDepth': 1., 'foldedPostWallThickness': 50.,
                   'foldedPostCornerRadius': 50., 'foldedPostInsertDepth': 0.,
                   'foldedPostTabWidth': 0., 'foldedPostTabLength': 0.,
                   'foldedPostFitGap': 100., 'foldedPostSideClearance': 100.}
        for changes in ({'frameLayout': 'left_right'}, {'frameLayout': 'top_bottom'},
                        {'assemblyPlanningMode': 'external_templates'}):
            with self.subTest(changes=changes):
                values = {**deepcopy(defaults), 'faceType': 'single', 'frameManufacturingMode': 'segment_weld',
                          'foldedPostJoint': 'insert', 'accessDoorEnabled': False, **changes}
                drafted = {**values, **invalid}
                before = deepcopy(drafted)
                with forbid_product_processing():
                    expected = core.display(values)
                    actual = core.display(drafted)
                    declaration = core.manufacturing(drafted)
                self.assertEqual(actual, expected)
                self.assertEqual(drafted, before)
                self.assertEqual(declaration['processes'][0]['definition']['parameters']['foldedPostJoint'], 'weld')
                output = load('security_window_product_contract')._design(
                    drafted, {'template': descriptor, 'geometryPurpose': 'display'}, core)
                self.assertEqual(output['parameters'], before)

    def test_imported_rectangular_post_contours_are_authoritative_in_real_tools(self):
        def rectangle(width, depth):
            return {'kind': 'polygon', 'points': [[-width/2, -depth/2], [width/2, -depth/2],
                    [width/2, depth/2], [-width/2, depth/2]]}
        section = {'schema': 'icax.imported-tube-profile', 'schemaVersion': 1,
                   'kind': 'fixed-section', 'profileForm': 'fixed', 'sectionKind': 'rect',
                   'name': 'actual-28-by-30', 'specification': 'actual-28-by-30',
                   'contentDigest': 'segmented-post-actual-28-by-30',
                   'width': 28., 'depth': 30., 'wallThickness': 1.,
                   'contours': [rectangle(28., 30.), rectangle(26., 28.)]}
        from icax_template_sdk.profile_constraints import _profile_validation_scope
        descriptor, _, _ = package('single_face_security_window')
        for offset in (0., 2.):
            actual_section = deepcopy(section)
            actual_section['contentDigest'] += '-offset-' + str(offset)
            for contour in actual_section['contours']:
                for point in contour['points']:
                    point[0] += offset
            for face in ('single', 'three'):
                for joint in ('insert', 'tabs'):
                    with self.subTest(face=face, joint=joint, offset=offset), _profile_validation_scope(descriptor):
                        values, _, design, compiled = window(joint, faceType=face, frameManufacturingMode='segment_weld',
                            sideWidth=600, infillPattern='vertical',
                            tubeDesignerProfileOverrides={'foldedPost': actual_section})
                        before = deepcopy(values)
                        allocation = allocate(compiled)
                        for key, item in frame_items(allocation['model']).items():
                            if is_post(key):
                                design_profile = frame_items(design)[key]['properties']['tubeDesigner.profile']
                                self.assertEqual(design_profile['contours'], actual_section['contours'])
                                coordinate_map = design_profile['sectionCoordinateMap']
                                mapped = [[[sum(row[j]*point[j] for j in (0, 1)) for row in coordinate_map]
                                           for point in contour['points']] for contour in actual_section['contours']]
                                expected_curves = [{'kind': 'path', 'closed': True, 'segments': [
                                    {'kind': 'line', 'start': point, 'end': points[(index+1) % len(points)]}
                                    for index, point in enumerate(points)]} for points in mapped]
                                self.assertEqual(item['properties']['tubeDesigner.profile']['contours'], expected_curves)
                                for index, dimension in enumerate(('width', 'depth')):
                                    extent = max(point[index] for point in mapped[0])-min(point[index] for point in mapped[0])
                                    self.assertEqual(item['properties']['tubeDesigner.profile'][dimension], extent)
                        self.assertTrue(full_plan(allocation)['operations'])
                        self.assertEqual(values, before)

    def test_weld_keeps_the_original_full_posts_or_between_beam_layout(self):
        for face in ('two', 'three', 'five'):
            for corner, expected_length in (('post_butt', 1000.), ('rail_miter', 924.)):
                with self.subTest(face=face, corner=corner):
                    _, _, _, compiled = window('weld', faceType=face, frameManufacturingMode='segment_weld',
                                               sideWidth=600, frameCornerJoin=corner)
                    allocation = allocate(compiled)
                    self.assertFalse(any(call['templateId'].startswith('tube-post-')
                                         for call in allocation['assemblySource']['instances']))
                    for key, item in frame_items(allocation['model']).items():
                        if is_post(key):
                            self.assertAlmostEqual(item['properties']['length'], expected_length)
                            self.assertEqual(item['properties']['tubeDesigner.profile']['width'], 38.)

    def test_tabs_crossing_a_shared_miter_seam_and_impossible_depth_are_rejected(self):
        for changes in ({'foldedPostWidth': 10.}, {'foldedPostInsertDepth': .1},
                        {'foldedPostInsertDepth': 20.}, {'frameCornerRadius': 5.},
                        {'foldedPostWidth': 38., 'foldedPostDepth': 38.}):
            joint = 'tabs' if changes.get('foldedPostWidth') == 10. else 'insert'
            expected_reason = '拼角|斜' if joint == 'tabs' else '入榫|原材|平直|内腔'
            with self.subTest(joint=joint, changes=changes), self.assertRaisesRegex(ValueError, expected_reason):
                inputs = window(joint, faceType='three', frameManufacturingMode='segment_weld',
                                sideWidth=600, infillPattern='vertical', **changes)
                full_plan(allocate(inputs[-1]))


if __name__ == '__main__':
    unittest.main()
