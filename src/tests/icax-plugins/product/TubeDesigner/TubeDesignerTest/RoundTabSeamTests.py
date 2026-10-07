"""Round tab sockets may open only at the two observed ends of a closed seam."""
from copy import deepcopy
import unittest

from GridEndConnectionTests import window, load, full_plan, allocate


class RoundTabSeamTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.case = window('tabs', 'tabs', width=1200, height=1800, accessDoorEnabled=True,
            verticalMaximumCenterSpacing=120, horizontalMaximumCenterSpacing=500,
            frameManufacturingMode='plane_v_notch', outerFrameGrooveTool='system:edge-arc-groove',
            doorFrameJoinType='v_groove_90:tool_library', doorFrameGrooveTool='system:edge-arc-groove',
            doorLeafFrameJoinType='v_groove_90:tool_library', doorLeafFrameGrooveTool='system:edge-arc-groove')
        cls.runtime = load('assembly_template_runtime')

    def seam_calls(self, allocation=None):
        return [call for call in (allocation or self.case[-1])['assemblySource']['instances']
                if call['templateId']=='tube-post-aperture'
                and 'main_grid.vertical.bottom.0004.' in call['instanceId']]

    def evaluate(self, call):
        before = deepcopy(call)
        result = self.runtime.evaluate_process(call['templateId'],call['processInput'],call['parameters'],{})
        self.assertEqual(call,before)
        self.assertEqual(result['parameters'],call['parameters'])
        return result

    def test_exact_four_split_sockets_keep_actual_shells_and_clip_each_end(self):
        calls = self.seam_calls()
        self.assertEqual(len(calls),4)
        for call in calls:
            geometry = call['processInput']['geometry']
            end = 'start' if call['instanceId'].endswith('.0') else 'end'
            self.assertEqual(geometry['endOpening'],end)
            stock = call['processInput']['parts']['stock']
            interval = geometry['clipInterval']
            self.assertAlmostEqual(interval[0] if end=='start' else interval[1],
                                   0. if end=='start' else stock['length'])
            result = self.evaluate(call)
            self.assertTrue(result['applicable'],result['reason'])
            self.assertEqual(len(result['operations']),1)
            nodes = {node['key']:node for node in result['geometry']}
            self.assertEqual(nodes[result['operations'][0]['tool']]['key'],'post-aperture.cropped')
            shell = load('section_geometry').closed_shell_metrics(load('section_geometry').from_profile(
                nodes['round-socket.shell-profile']['arguments']))
            self.assertAlmostEqual(shell['contours'][0]['edges'][0]['radius'],9.6)
            self.assertAlmostEqual(shell['contours'][1]['edges'][0]['radius'],8.4)
        self.assertTrue(full_plan(self.case[-1])['operations'])

    def shifted_allocation(self, distance):
        compiled = deepcopy(self.case[-2])
        branch = 'main_grid.vertical.bottom.0004'
        item = next(item for item in compiled['items'] if item['key']==branch)
        for vertex in item['manufacturingInput']['path']['vertices']:
            vertex['point'][0] += distance
        for call in compiled['processes']:
            definition = call['definition']
            bindings = definition['processInput']['parts']
            if not any(binding.get('itemKey')==branch for binding in bindings.values()):
                continue
            geometry = definition['processInput']['geometry']
            if 'node' in geometry:
                geometry['node'][0] += distance
            if 'anchor' in geometry:
                geometry['anchor']['point'][0] += distance
        return allocate(compiled)

    def test_small_offsets_share_two_real_sockets_and_larger_offsets_skip_empty_half(self):
        for distance in (-5.,-.5,.5,5.):
            with self.subTest(distance=distance):
                allocation = self.shifted_allocation(distance)
                calls = self.seam_calls(allocation)
                self.assertEqual(len(calls),4)
                operations = 0
                for call in calls:
                    result = self.evaluate(call)
                    self.assertTrue(result['applicable'],result['reason'])
                    operations += len(result['operations'])
                    if not result['operations']:
                        self.assertEqual(result['geometry'],[])
                        self.assertEqual(result['checks'][0]['kind'],'no-contact-in-stock-interval')
                self.assertEqual(operations,4 if abs(distance)<1. else 2)
                self.assertTrue(full_plan(allocation)['operations'])

    def test_open_paths_and_missing_partner_never_acquire_end_opening(self):
        for missing_partner in (False,True):
            compiled = deepcopy(self.case[-2])
            stock = next(item for item in compiled['items'] if item['key']=='outer_frame.continuous.0001')
            if missing_partner:
                mapping = next(mapping for mapping in compiled['sourceMappings']
                    if mapping['itemKey']==stock['key'])
                receiver = next(source for source in mapping['sources']
                    if len(source['segments'])==2)
                receiver['segments'] = receiver['segments'][:1]
            else:
                stock['manufacturingInput']['path']['closed'] = False
            allocation = allocate(compiled)
            calls = [call for call in self.seam_calls(allocation)
                     if call['targets']['stock']==stock['key']]
            self.assertTrue(calls)
            for call in calls:
                self.assertNotIn('endOpening',call['processInput']['geometry'])
                result = self.evaluate(call)
                self.assertFalse(result['applicable'])
                self.assertIn('端部桥料',result['reason'])

    def test_unmarked_raw_end_and_opposite_end_still_require_bridges(self):
        call = deepcopy(self.seam_calls()[0])
        call['processInput']['geometry'].pop('endOpening')
        result = self.evaluate(call)
        self.assertFalse(result['applicable'])
        self.assertIn('端部桥料',result['reason'])
        call = deepcopy(self.seam_calls()[0])
        call['processInput']['parts']['stock']['length'] = 1.
        call['processInput']['geometry']['clipInterval'] = [0.,1.]
        result = self.evaluate(call)
        self.assertFalse(result['applicable'])
        self.assertIn('端部桥料',result['reason'])

    def test_wrong_interval_or_shared_corner_cannot_authorize_an_end(self):
        for changes in ({'endOpening':'end'},{'endOpening':True},{'clipInterval':[1.,599.]},
                        {'clipInterval':[-1.,599.]},{'clipInterval':[0.,float('nan')]},
                        {'sharedCorner':True}):
            with self.subTest(changes=changes):
                call = deepcopy(self.seam_calls()[0])
                call['processInput']['geometry'].update(changes)
                self.assertFalse(self.evaluate(call)['applicable'])

    def test_opposite_wall_fit_and_cross_section_fit_checks_stay_active(self):
        call = deepcopy(self.seam_calls()[0])
        call['parameters']['insertionDepth'] = 38.
        result = self.evaluate(call)
        self.assertFalse(result['applicable'])
        self.assertIn('对侧壁',result['reason'])
        call = deepcopy(self.seam_calls()[0])
        profile = call['processInput']['parts']['branch']['section']['profile']
        # An actual larger round shell must not bypass the receiver width check.
        _, defaults, core = __import__('WindowManufacturingInputTests').package('single_face_security_window')
        profile['contours'] = core._profile({**defaults,'verticalWidth':40.,'verticalDepth':40.},'vertical').contours()
        result = self.evaluate(call)
        self.assertFalse(result['applicable'])
        self.assertIn('真实外截面',result['reason'])


if __name__=='__main__':
    unittest.main()
