"""Balcony return geometry and the existing single-support tube layout."""
from copy import deepcopy
import math
import unittest

from MinimalProtectiveGrilleManufacturingTests import package


class MinimalGrilleBalconyLayoutTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.descriptor, cls.defaults, cls.template = package('minimal_protective_grille')

    def values(self, **changes):
        values = {**self.defaults, 'layoutStyle': 'balcony_closure', 'handleEnabled': 'no',
                'supportStyle': 'single', 'frameProfileType': 'rect', 'innerProfileType': 'rect',
                'supportProfileType': 'rect', 'supportDepth': 10,
                'width': 1800, 'height': 600, 'barLayoutMode': 'fixed_count', 'barCount': 3,
                'balconyTopDepth': 120, 'balconyBottomDepth': 220, 'balconyTopDrop': 0, **changes}
        for suffix in ('ProfileType', 'Width', 'Depth', 'WallThickness', 'CornerRadius'):
            if 'projectionHorizontal'+suffix not in changes:
                values['projectionHorizontal'+suffix] = values['frame'+suffix]
        return values

    def layout(self, **changes):
        p = self.values(**changes)
        before = deepcopy(p)
        state = self.template.layout(p)
        self.assertEqual(p, before)
        self.assertEqual(state['facts']['layoutStyle'], 'balcony_closure')
        return state

    def test_open_installation_side_has_only_actual_top_and_bottom_rear_rails(self):
        state = self.layout()
        members = {m['key']: m for m in state['members']}
        self.assertNotIn('frame.rear_left.0001', members)
        self.assertNotIn('frame.rear_right.0001', members)
        for side in ('left', 'right'):
            boundary = [m for m in members.values() if m['role'] == 'frame.return_'+side]
            self.assertEqual(len(boundary), 2)
        for key in ('frame.rear_top.0001', 'frame.rear_bottom.0001'):
            self.assertEqual(members[key]['start'][0], 0)
            self.assertEqual(members[key]['end'][0], 1800)
        self.assertEqual(members['frame.left.0001']['role'], 'frame.left')
        self.assertEqual(members['frame.left.0001']['start'][1], 0)

    def test_independent_return_depths_do_not_move_the_other_face(self):
        base = self.layout()
        changed = self.layout(balconyTopDepth=350, balconyTopDrop=35)
        def bottom(state):
            return [(m['key'], m['start'], m['end']) for m in state['members']
                    if m['face'] == 'bottom' or m['key'] == 'frame.rear_bottom.0001'
                    or m['key'] in ('frame.return_left.bottom', 'frame.return_right.bottom')]
        self.assertEqual(bottom(base), bottom(changed))
        self.assertEqual(base['facts']['bottomProjectionDepth'], 220)
        for m in (m for m in changed['members'] if m['face'] == 'top'):
            delta = [b-a for a,b in zip(m['start'],m['end'])]
            self.assertAlmostEqual(delta[2]/delta[1], 35/changed['facts']['mountingPlaneY'])

    def test_middle_supports_do_not_split_front_horizontal_stocks(self):
        sparse = self.layout(supportMaxSpacing=800)
        dense = self.layout(supportMaxSpacing=500)
        def rows(state):
            return [(m['key'],m['start'],m['end']) for m in state['members'] if m['role']=='inner.full']
        self.assertEqual(rows(sparse), rows(dense))
        self.assertEqual(len(rows(sparse)), 3)
        self.assertGreater(len(dense['facts']['supportPositions']), len(sparse['facts']['supportPositions']))
        self.assertTrue(any(c.get('kind') == 'cross-through' for c in dense['connections']))

    def test_mounting_holes_reference_real_front_uprights(self):
        state = self.layout(installHoleEnabled='yes', installHoleTopOffset=70, installHoleBottomOffset=80)
        members = {m['key']:m for m in state['members']}
        for group in state['facts']['installationHoles']['groups']:
            self.assertIn(group['memberKey'], ('frame.left.0001','frame.right.0001'))
            self.assertEqual(group['point'][1], 0)
            self.assertEqual(members[group['memberKey']]['face'], 'front')
        self.assertEqual(state['facts']['installationHoles']['centers'][0], 80)
        self.assertEqual(state['facts']['installationHoles']['centers'][-1], 530)

    def test_inactive_five_face_dimensions_do_not_affect_balcony(self):
        first = self.layout()
        other = self.layout(projectionDepth=1, topDrop=1000, frameType='two_vertical')
        self.assertEqual([(m['key'],m['start'],m['end']) for m in first['members']],
                         [(m['key'],m['start'],m['end']) for m in other['members']])

    def test_open_slot_mounting_dimensions_use_whole_upright_extents(self):
        p = self.values(height=1250, handleEnabled='yes', handleSide='left', handleClosure='open_slot',
                        barLayoutMode='maximum_gap', installHoleEnabled='yes',
                        installHoleCountPerSide=2, installHoleTopOffset=120, installHoleBottomOffset=130)
        display = self.template.display(p)
        dimensions = {d['parameter']:d for d in display['annotations']}
        self.assertEqual(dimensions['installHoleTopOffset']['start'][2],1250)
        self.assertEqual(dimensions['installHoleTopOffset']['end'][2],1130)
        self.assertEqual(dimensions['installHoleBottomOffset']['start'][2],0)
        self.assertEqual(dimensions['installHoleBottomOffset']['end'][2],130)
        # Even when both holes lie on the upper segment, the offset's datum
        # stays the original upright's bottom, not the handle-slot boundary.
        shifted = self.template.display({**p, 'installHoleBottomOffset':900})
        dims = {d['parameter']:d for d in shifted['annotations']}
        self.assertEqual(dims['installHoleBottomOffset']['start'][2],0)
        self.assertEqual(dims['installHoleBottomOffset']['end'][2],900)

    def test_all_endpoint_contacts_are_on_actual_receiving_surfaces(self):
        state = self.layout(frameWidth=30, frameDepth=20, frameCornerRadius=3,
                            balconyTopDepth=200, balconyBottomDepth=300, balconyTopDrop=30)
        members = {m['key']:m for m in state['members']}
        geometry = self.template._load(self.template.ROOT/'spatial_geometry.py')
        for contact in state['connections']:
            if contact.get('kind') == 'cross-through':
                continue
            roots = geometry.ray_intersections(members[contact['hostKey']], contact['point'], contact['normal'])
            self.assertAlmostEqual(roots[1], 0, places=6)
            self.assertLess(roots[0], 0)


if __name__ == '__main__':
    unittest.main()
