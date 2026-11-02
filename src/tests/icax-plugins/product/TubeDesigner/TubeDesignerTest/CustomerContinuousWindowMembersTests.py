"""Customer 29bb19: avoid welds unless tube or assembly continuity requires one."""
from copy import deepcopy
import unittest

from WindowCatalogueTests import package, public_preview
from CustomerEscapeWindowTests import opening
from ProductManufacturingDeclarationTests import execute_definition


class CustomerContinuousWindowMembersTests(unittest.TestCase):
    def setUp(self):
        self.descriptor, self.defaults, self.template = package('single_face_security_window')

    def design(self, **changes):
        values = dict(self.defaults, **changes)
        saved = deepcopy(values)
        document = self.template.display(values)
        self.assertEqual(values, saved)
        return values, document

    def test_equal_tubes_make_two_continuous_rows_without_merging_the_leaf(self):
        p, d = self.design(horizontalWidth=25, horizontalDepth=25)
        a = opening(d)
        rails = [i for i in d['items'] if i['properties'].get('tubeDesigner.escapeFrameMember') == 'through_rail']
        self.assertEqual(len(rails), 2)
        outer = {i['key']: i['properties']['assemblyFrame.member'] for i in d['items'] if i['key'].startswith('outer_frame.')}
        for item in rails:
            m = item['properties']['assemblyFrame.member']
            self.assertEqual(m['start'][0], outer['outer_frame.left.0001']['start'][0])
            self.assertEqual(m['end'][0], outer['outer_frame.right.0001']['start'][0])
            self.assertFalse(any(i['key'].startswith('main_grid.horizontal.')
                and abs(i['properties']['assemblyFrame.member']['start'][2]-m['start'][2]) < 1.e-7 for i in d['items']))
        self.assertEqual(len(a['fixedFrameConstruction']['removedGridStubKeys']), 4)
        self.assertEqual(len([i for i in d['items'] if i['key'].startswith('access_door.leaf.frame.')]), 4)
        executed = execute_definition(self.template.manufacturing(p), design=d)
        fixed = {i['key']: i['properties']['length'] for i in executed['items'] if i['key'].startswith('access_door.fixed_frame.')}
        for key, length in fixed.items():
            expected = p['width']-2*p['frameWidth'] if '.bottom.' in key or '.top.' in key else a['fixedClearHeight']
            self.assertAlmostEqual(length, expected, places=3)

    def test_default_role_profiles_do_not_create_three_pipes_in_one_row(self):
        p, d = self.design()
        a = opening(d)
        self.assertEqual(len(a['fixedFrameConstruction']['removedGridStubKeys']), 4)
        self.assertEqual(len(a['fixedFrameConstruction']['railKeys']), 2)
        executed = execute_definition(self.template.manufacturing(p), design=d)
        fixed = [i for i in executed['items'] if i['key'].startswith('access_door.fixed_frame.')]
        for item in fixed:
            expected = p['width']-2*p['frameWidth'] if '.bottom.' in item['key'] or '.top.' in item['key'] else a['fixedClearHeight']
            self.assertAlmostEqual(item['properties']['length'], expected, places=3)
        widths = {i['properties']['tubeDesigner.profile']['width'] for i in d['items'] if i['key'].startswith('main_grid.horizontal.')}
        self.assertEqual(widths, {p['horizontalWidth']})
        for rail in (i for i in d['items'] if i['key'] in a['fixedFrameConstruction']['railKeys']):
            member = rail['properties']['assemblyFrame.member']
            self.assertEqual(member['start'][0], p['frameWidth']/2)
            self.assertEqual(member['end'][0], p['width']-p['frameWidth']/2)
            self.assertEqual(rail['properties']['tubeDesigner.profile']['width'], p['doorFrameWidth'])
            self.assertFalse(any(i['key'].startswith('main_grid.horizontal.') and
                abs(i['properties']['assemblyFrame.member']['start'][2]-member['start'][2]) < 1.e-7 for i in d['items']))

    def test_empty_space_does_not_create_an_extra_full_width_rail(self):
        _, d = self.design(infillPattern='vertical', doorClearWidth=600, doorClearHeight=600)
        a = opening(d)
        self.assertEqual(a['fixedFrameConstruction']['removedGridStubKeys'], [])
        for item in d['items']:
            if item['key'].startswith(('access_door.fixed_frame.bottom.', 'access_door.fixed_frame.top.')):
                m = item['properties']['assemblyFrame.member']
                self.assertAlmostEqual(m['start'][0], a['boundaryAxes']['left'])
                self.assertAlmostEqual(m['end'][0], a['boundaryAxes']['right'])

    def test_independent_fixed_frame_forming_drafts_cannot_add_bends(self):
        p, d = self.design(horizontalWidth=25, horizontalDepth=25)
        changed = dict(p, doorFrameJoinType='v_groove_90:tool_library', doorFrameFoldBridge=-99,
                       doorFrameVGrooveRoundRadius=-99, doorFrameButtWrapMode='side_wraps_horizontal')
        self.assertEqual(self.template.display(changed), d)
        baseline = execute_definition(self.template.manufacturing(p), design=d)
        other = execute_definition(self.template.manufacturing(changed), design=d)
        self.assertEqual(other['resources'], baseline['resources'])
        self.assertEqual([i['key'] for i in other['items']], [i['key'] for i in baseline['items']])
        self.assertFalse(any(i['key'].startswith('access_door.fixed_frame.continuous.') for i in other['items']))
        public_preview('single_face_security_window', self.descriptor, changed, 'manufacturing')


if __name__ == '__main__':
    unittest.main()
