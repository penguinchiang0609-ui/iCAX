"""Thin-wall tab sockets use actual stock edges, never whole-tube cavity fit."""
from copy import deepcopy
import unittest

from WindowManufacturingInputTests import load, package


class PostTabSocketFitTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        _, cls.defaults, cls.product = package('single_face_security_window')
        cls.tool = load('assembly_post_machining')._tool('paired-side-slots')

    def inputs(self, size=38., allow=False):
        host = {'contours': self.product._profile(self.defaults, 'frame').contours()}
        branch = {'contours': self.product._profile({**self.defaults,
            'frameWidth': size, 'frameDepth': size}, 'frame').contours()}
        values = {'pairCount': 2, 'tabWidth': 8., 'tabLength': 12., 'sideClearance': .2,
                  'pairRotation': 0., 'allowEndOpening': False, 'allowSideOpening': allow}
        context = {'targetSection': host,
            'feature': {'face': 'top', 'reference': 'start', 'station': 19., 'offset': 0.,
                        'section': {'profile': branch}},
            'bounds': {'min': [0., -19., -19.], 'max': [1200., 19., 19.]},
            'placement': {'rotation': 0.}}
        return values, context

    def test_same_section_uses_the_real_wall_ring_and_preserves_both_profiles(self):
        parameters,context=self.inputs()
        parameters.pop('allowSideOpening');parameters.pop('allowEndOpening')
        before=deepcopy((parameters,context))
        result=self.tool.generate(parameters,context)
        self.assertEqual((parameters,context),before)
        graph={n['key']:n for n in result['model']['geometry']}
        ring=graph['mating-ring.profile']['arguments']
        self.assertEqual(len(ring['contours']),2)
        self.assertAlmostEqual(ring['placement']['origin'][0],19.)
        self.assertAlmostEqual(graph['mating-ring']['arguments']['vector'][2],-19.02)
        self.assertEqual(graph['tool']['arguments']['operation'],'intersect')

    def test_side_and_end_crossings_do_not_require_planar_permissions(self):
        parameters,context=self.inputs()
        parameters.pop('allowSideOpening');parameters.pop('allowEndOpening')
        for station in (1.,19.,1199.):
            for offset in (0.,5.):
                context['feature'].update(station=station,offset=offset)
                result=self.tool.generate(parameters,context)
                self.assertEqual(result['outputKey'],'tool')

    def test_inserted_tip_cannot_reach_the_opposite_wall_in_single_wall_mode(self):
        parameters,context=self.inputs()
        parameters['tabLength']=37.
        with self.assertRaisesRegex(ValueError,'对侧壁'):
            self.tool.generate(parameters,context)


if __name__ == '__main__':
    unittest.main()
