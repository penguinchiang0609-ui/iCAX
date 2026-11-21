"""Three physical drying stocks, finite actual receivers, unchanged framework."""
from copy import deepcopy
import math
import unittest

from WindowCatalogueTests import package
from ProductManufacturingDeclarationTests import execute_definition
from icax_template_sdk.profile_constraints import _profile_validation_scope


class MinimalGrilleDryingRodTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.descriptor, cls.defaults, cls.template = package('minimal_protective_grille')

    def state(self, **changes):
        values = {**self.defaults, 'layoutStyle': 'five_face', **changes}
        before = deepcopy(values)
        with _profile_validation_scope(self.descriptor):
            state = self.template.layout(values)
        self.assertEqual(before, values)
        return values, state

    def test_disabled_ignores_invalid_inactive_material_and_position_drafts(self):
        _, a = self.state()
        _, b = self.state(dryingProfileType='unsupported', dryingWidth=-1,
                          dryingRodRearOffset=-1, dryingRodDrop=-1)
        self.assertEqual([(m['key'], m['start'], m['end']) for m in a['members']],
                         [(m['key'], m['start'], m['end']) for m in b['members']])
        self.assertFalse(b['facts']['dryingRod']['enabled'])

    def test_enabled_adds_only_three_stocks_and_preserves_every_original_member(self):
        for style in ('five_face', 'balcony_closure'):
            _, a = self.state(layoutStyle=style)
            _, b = self.state(layoutStyle=style, dryingRodEnabled='yes')
            stable = lambda state: [(m['key'], m['start'], m['end'], m['profile'].properties())
                                    for m in state['members'] if m.get('profileRole') != 'drying']
            self.assertEqual(stable(a), stable(b))
            drying = [m for m in b['members'] if m.get('profileRole') == 'drying']
            self.assertEqual(len(drying), 3)
            self.assertEqual(len(b['members'])-len(a['members']), 3)
            if style == 'balcony_closure':
                self.assertFalse(any(m['key'].startswith('frame.rear_left') for m in b['members']))

    def test_default_five_face_arms_stop_at_first_side_bar_instead_of_crossing_it(self):
        _, state = self.state(dryingRodEnabled='yes')
        facts = state['facts']['dryingRod']
        self.assertEqual((facts['y'], facts['z']), (72.5, 1170.))
        for contact in facts['topContacts']:
            self.assertTrue(contact['hostKey'].startswith('inner.return_'))
            self.assertAlmostEqual(contact['point'][2], 1187.5)

    def test_balcony_positions_reference_upper_depth_and_real_sloped_receivers(self):
        p, state = self.state(layoutStyle='balcony_closure', dryingRodEnabled='yes',
                              balconyTopDepth=160, balconyBottomDepth=240, balconyTopDrop=30,
                              dryingRodRearOffset=40, dryingRodDrop=100)
        facts = state['facts']['dryingRod']
        self.assertEqual((facts['y'], facts['z']), (117.5, 1150.))
        geometry = self.template._load(self.template.ROOT/'spatial_geometry.py')
        by_key = {m['key']:m for m in state['members']}
        for contact in facts['topContacts']:
            self.assertTrue(contact['hostKey'].startswith('frame.return_'))
            roots = geometry.ray_intersections(by_key[contact['hostKey']], contact['point'], (0.,0.,-1.))
            self.assertAlmostEqual(roots[1], 0., places=6)
            self.assertLess(roots[0], 0.)

    def test_invalid_envelope_and_no_space_for_arm_are_rejected(self):
        for changes in ({'dryingRodRearOffset':0}, {'dryingRodRearOffset':100},
                        {'dryingRodDrop':1250}, {'dryingRodDrop':55}):
            with self.assertRaisesRegex(ValueError, '晾衣杆'):
                self.state(dryingRodEnabled='yes', **changes)

    def test_actual_shared_manufacturing_preserves_three_continuous_stock_sources(self):
        p, state = self.state(dryingRodEnabled='yes')
        before = deepcopy(p)
        with _profile_validation_scope(self.descriptor):
            design = self.template.display(p)
            declaration = self.template.manufacturing(p)
            actual = execute_definition(declaration, design=design)
        self.assertEqual(before, p)
        keys = {m['key'] for m in state['members'] if m.get('profileRole') == 'drying'}
        self.assertEqual(len([m for m in actual['items'] if m['key'] in keys]), 3)
        joints = [c for c in declaration['connections'] if any(k in keys for k in c['items'])]
        self.assertEqual(len(joints), 4)
        self.assertTrue(all(c['kind']=='weld' and c['properties']['recipe']=='butt_weld' for c in joints))
        self.assertTrue(any(c['properties'].get('nonInterferencePairs') for c in joints))
        for item in design['items']:
            if item['key'] in keys:
                self.assertEqual(item['properties']['categoryKey'], 'minimal-grille.drying')


if __name__ == '__main__':
    unittest.main()
