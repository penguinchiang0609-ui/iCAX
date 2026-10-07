"""Exact polygon/datum regression for both window profile resource roles."""
from copy import deepcopy
import json
import unittest

from AllProductsWindowRecheck import (CORE, DEFAULTS, SRC, OUTPUT, cases,
    check_declarations, forbid_display_callback, forbid_product_processing, source_snapshot)


class WindowGenericProfileResourceTests(unittest.TestCase):
    def build(self,name):
        fixture=next(case for case in cases() if case['name']==name)
        values={**DEFAULTS,**fixture['changes']};saved=deepcopy(values)
        with forbid_product_processing():
            display=CORE.display(values)
            with forbid_display_callback(): raw=CORE.manufacturing(values)
        self.assertEqual(values,saved)
        check_declarations(display,raw)
        return values,display

    def test_original_polygon_components_keep_every_vertex_and_datum(self):
        for name in ('asymmetric-door-left-segment_weld','shifted-asymmetric-datum-segment_weld',
                     'sharp-rect-zero-radius-segment_weld'):
            with self.subTest(name=name):
                values,display=self.build(name)
                original=values['tubeDesignerProfileOverrides']['frame']['contours']
                resources={node['key']:node for node in display['resources']}
                frames=[item for item in display['items'] if item['key'].startswith('outer_frame.')]
                self.assertTrue(frames)
                for item in frames:
                    profile=item['properties']['tubeDesigner.profile']
                    self.assertEqual(profile['contours'],original)
                    converted=resources[profile['sectionResource']]['arguments']['contours']
                    for source,curve in zip(original,converted):
                        self.assertTrue(curve['closed'])
                        self.assertEqual(len(curve['segments']),len(source['points']))
                        self.assertEqual([edge['start'] for edge in curve['segments']],source['points'])
                        self.assertEqual([edge['end'] for edge in curve['segments']],source['points'][1:]+source['points'][:1])
                        self.assertEqual({edge['kind'] for edge in curve['segments']},{'line'})

    def test_render_and_intrinsic_profiles_use_only_generic_curves(self):
        for name in ('asymmetric-door-left-plane_v_notch','shifted-asymmetric-datum-segment_weld',
                     'sharp-rect-zero-radius-segment_weld'):
            with self.subTest(name=name):
                unused,display=self.build(name)
                profiles=[node for node in display['resources'] if node['operator']=='profile2d']
                self.assertTrue(profiles)
                for profile in profiles:
                    self.assertTrue(all(curve['kind']=='path' for curve in profile['arguments']['contours']),profile['key'])

    def test_existing_path_example_is_exactly_unchanged(self):
        folder=SRC.parent/'doc/examples/product-manufacturing'
        values=json.loads((folder/'v4-v-notch-window.host-input.json').read_text(encoding='utf-8'))['parameters']
        original=deepcopy(values)
        with forbid_product_processing(): display=CORE.display(values)
        self.assertEqual(values,original)
        self.assertEqual(display,json.loads((folder/'v4-v-notch-window.display.actual.json').read_text(encoding='utf-8')))


if __name__=='__main__':
    before=source_snapshot()
    result=unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(WindowGenericProfileResourceTests))
    after=source_snapshot()
    report={'schema':'icax.window-generic-profile-resource-source-tests','phase':4,
            'testsRun':result.testsRun,'passed':result.wasSuccessful() and before==after,
            'failures':len(result.failures),'errors':len(result.errors),'sourceHashes':before,
            'sourceHashesUnchangedDuringRun':before==after,'nativeVerified':False,
            'checks':['all render and section profile2d resources use generic curves',
                      'original polygon vertices winding closing edge and datum are unchanged',
                      'original component facts and host inputs are unchanged',
                      'existing generic path document is unchanged']}
    (OUTPUT/'all-products-recheck-window-generic-profile-resource-source-tests.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    raise SystemExit(0 if report['passed'] else 1)
