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

    def test_same_section_can_cut_real_edge_channels_without_changing_either_profile(self):
        parameters, context = self.inputs(allow=True)
        before = deepcopy((parameters, context))
        result = self.tool.generate(parameters, context)
        self.assertEqual((parameters, context), before)
        graph = {node['key']: node for node in result['model']['geometry']}
        for index, across in enumerate((18.4, -18.4)):
            placement = graph[f'socket-{index}-profile']['arguments']['placement']
            self.assertAlmostEqual(placement['origin'][0], 19.)
            self.assertAlmostEqual(placement['origin'][1], across)
            self.assertEqual(graph[f'socket-{index}']['arguments']['vector'], [0., 0., -12.02])
            self.assertLess(placement['origin'][2] - 12.02, 19. - parameters['tabLength'])
            # Cut through the receiver's near corner and side skin, never its
            # opposite wall. Width includes the actual 1.2 mm wall and gap.
            contour = graph[f'socket-{index}-profile']['arguments']['contours'][0]
            self.assertAlmostEqual(abs(contour['segments'][0]['start'][1]), .8)

    def test_standalone_closed_slots_still_reject_crossing_the_flat_wall(self):
        parameters, context = self.inputs()
        with self.assertRaisesRegex(ValueError, '平直侧壁'):
            self.tool.generate(parameters, context)

    def test_smaller_sections_retain_identical_closed_socket_geometry(self):
        parameters, context = self.inputs(size=30.)
        closed = self.tool.generate(parameters, context)
        parameters['allowSideOpening'] = True
        self.assertEqual(self.tool.generate(parameters, context), closed)

    def test_open_channels_still_require_real_host_contact_and_opposite_wall_clearance(self):
        for changes, parameter_changes, reason in (
                ({'offset': 10.}, {}, '实际管壁'),
                ({}, {'tabLength': 37.}, '对侧壁'),
                ({}, {'allowSideOpening': 'yes'}, '布尔')):
            parameters, context = self.inputs(allow=True)
            parameters.update(parameter_changes)
            context['feature'].update(changes)
            with self.assertRaisesRegex(ValueError, reason):
                self.tool.generate(parameters, context)


if __name__ == '__main__':
    unittest.main()
