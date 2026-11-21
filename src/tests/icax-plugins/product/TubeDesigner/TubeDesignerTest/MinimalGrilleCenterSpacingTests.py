"""Measure actual B stock centres, independent of their width and boundary rows."""
from copy import deepcopy
import unittest

from WindowCatalogueTests import package
from icax_template_sdk.profile_constraints import _profile_validation_scope


class MinimalGrilleCenterSpacingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.descriptor, cls.defaults, cls.template = package('minimal_protective_grille')

    def values(self, **changes):
        return {**self.defaults, 'layoutStyle':'planar', 'width':1200., 'height':1250.,
                'frameType':'closed_frame', 'handleEnabled':'no', **changes}

    def layout(self, values):
        before = deepcopy(values)
        with _profile_validation_scope(self.descriptor):
            state = self.template.layout(values)
        self.assertEqual(values, before, 'Pitch layout must not rewrite host parameters')
        return state

    def row_centres(self, state, zone):
        rows = [m for m in state['members'] if m.get('face','front')=='front'
                and m['role']=='inner.'+zone]
        self.assertTrue(rows, 'The requested ordinary B row zone must contain actual stock')
        for row in rows:
            self.assertAlmostEqual(row['start'][2], row['end'][2])
            self.assertGreater(row['end'][0], row['start'][0])
        centres = sorted({m['start'][2] for m in rows})
        self.assertEqual(centres, sorted(state['facts']['rowCenters'][zone]))
        return centres

    def assert_pitch(self, centres, pitch):
        self.assertGreaterEqual(len(centres), 2, 'A pitch check requires two actual rows')
        for low, high in zip(centres, centres[1:]):
            self.assertAlmostEqual(high-low, pitch, places=7)

    def annotations(self, values):
        with _profile_validation_scope(self.descriptor):
            return {a['parameter']:a for a in self.template.display(values)['annotations']}

    def test_true_defaults_use_115_centres_and_welded_flat_stock(self):
        self.assertEqual(self.defaults['barLayoutMode'], 'center_spacing')
        self.assertEqual(self.defaults['barCenterSpacing'], 115)
        self.assertEqual(self.defaults['jointMode'], 'butt_weld')
        self.assertEqual(self.defaults['returnJointMode'], 'butt_weld')
        state = self.layout(deepcopy(self.defaults))
        self.assertEqual(state['facts']['barCenterSpacing'], 115)
        for zone in ('lower','middle','upper'):
            self.assert_pitch(self.row_centres(state, zone), 115)
        with _profile_validation_scope(self.descriptor):
            declaration = self.template.manufacturing(deepcopy(self.defaults))
        self.assertTrue(declaration['connections'])
        self.assertTrue(all(c['kind']=='weld' and c['properties']['recipe']=='butt_weld'
                            for c in declaration['connections']))

    def test_edits_to_80_and_150_measure_actual_front_stock_centres_in_all_styles(self):
        for style in ('planar','five_face','balcony_closure'):
            counts = {}
            for pitch in (80.,150.):
                with self.subTest(style=style, pitch=pitch):
                    values = self.values(layoutStyle=style, barCenterSpacing=pitch)
                    state = self.layout(values)
                    centres = self.row_centres(state, 'full')
                    self.assert_pitch(centres, pitch)
                    self.assertEqual(state['facts']['barCenterSpacing'], pitch)
                    counts[pitch] = len(centres)
            self.assertGreater(counts[80.], counts[150.])

    def test_wider_18_mm_B_changes_clear_gap_but_keeps_the_115_centres(self):
        actual = []
        for width in (10.,18.):
            values = self.values(innerWidth=width)
            state = self.layout(values)
            centres = self.row_centres(state, 'full')
            self.assert_pitch(centres, 115.)
            actual.append(centres)
            rows = sorted((m for m in state['members'] if m['role']=='inner.full'), key=lambda m:m['start'][2])
            for first, second in zip(rows, rows[1:]):
                gap = second['start'][2]-second['profile'].width/2-(first['start'][2]+first['profile'].width/2)
                self.assertAlmostEqual(gap, 115.-width)
        self.assertEqual(actual[0], actual[1])

    def test_closed_frame_centres_the_unfilled_end_allowances(self):
        values = self.values(height=613.)
        state = self.layout(values)
        centres = self.row_centres(state, 'full')
        self.assert_pitch(centres, 115.)
        self.assertEqual(len(centres), 6)
        rows = [m for m in state['members'] if m['role']=='inner.full']
        width = rows[0]['profile'].width
        bottom = next(m for m in state['members'] if m['role']=='frame.bottom')
        top = next(m for m in state['members'] if m['role']=='frame.top')
        inside_bottom = bottom['start'][2]+bottom['profile'].width/2
        inside_top = top['start'][2]-top['profile'].width/2
        bottom_gap = centres[0]-width/2-inside_bottom
        top_gap = inside_top-centres[-1]-width/2
        self.assertGreaterEqual(bottom_gap, 0.)
        self.assertAlmostEqual(bottom_gap, top_gap)
        self.assertAlmostEqual(bottom_gap, 4.)

    def test_two_vertical_no_handle_uses_bottom_anchor_and_ignores_invalid_top_draft(self):
        values = self.values(frameType='two_vertical',height=500.,lastBarBottomOffset=55.)
        state = self.layout(values)
        self.assertEqual(self.row_centres(state,'full'), [55.,170.,285.,400.])
        changed = {**values,'firstBarTopOffset':-999.}
        other = self.layout(changed)
        signature = lambda s: [(m['key'],m['start'],m['end']) for m in s['members']]
        self.assertEqual(signature(state), signature(other))
        dimensions = self.annotations(changed)
        self.assertNotIn('firstBarTopOffset', dimensions)
        self.assertEqual(dimensions['lastBarBottomOffset']['end'][2],55.)
        moved = self.layout({**changed,'lastBarBottomOffset':80.})
        self.assertEqual(self.row_centres(moved,'full'), [80.,195.,310.,425.])

    def test_bottom_frame_uses_top_anchor_and_ignores_invalid_bottom_draft(self):
        values = self.values(frameType='bottom_rail',height=500.,firstBarTopOffset=55.,lastBarBottomOffset=-999.)
        state = self.layout(values)
        self.assertEqual(self.row_centres(state,'full'), [100.,215.,330.,445.])
        dimensions = self.annotations(values)
        self.assertNotIn('lastBarBottomOffset', dimensions)
        self.assertEqual(dimensions['firstBarTopOffset']['start'][2],500.)
        self.assertEqual(dimensions['firstBarTopOffset']['end'][2],445.)

    def test_handle_ordinary_zones_keep_pitch_and_boundary_rows_remain_independent(self):
        for side in ('left','right','center_double'):
            with self.subTest(side=side):
                values = self.values(frameType='two_vertical',width=1800.,handleEnabled='yes',
                                     handleSide=side,handleBottom=400.,handleHeight=300.)
                state = self.layout(values)
                ordinary = []
                for zone in ('lower','middle','upper'):
                    centres = self.row_centres(state,zone)
                    self.assert_pitch(centres,115.)
                    ordinary.extend(centres)
                self.assertEqual(self.row_centres(state,'lower_boundary'),[395.])
                self.assertEqual(self.row_centres(state,'upper_boundary'),[705.])
                self.assertNotIn(395.,ordinary)
                self.assertNotIn(705.,ordinary)
                self.assertEqual(state['facts']['clearOpenings'][0]['bottom'],400.)
                self.assertEqual(state['facts']['clearOpenings'][0]['top'],700.)

    def test_spacing_equal_to_or_less_than_B_width_is_rejected(self):
        for width in (10.,18.):
            for pitch in (0.,width-1.,width):
                with self.subTest(width=width,pitch=pitch), self.assertRaises(ValueError):
                    self.layout(self.values(height=200.,innerWidth=width,barCenterSpacing=pitch))
            state = self.layout(self.values(height=200.,innerWidth=width,barCenterSpacing=width+.5))
            self.assert_pitch(self.row_centres(state,'full'),width+.5)

    def test_center_spacing_draft_is_inactive_in_fixed_count(self):
        values = self.values(barLayoutMode='fixed_count',barCount=3)
        baseline = self.layout(values)
        invalid = {**values,'barCenterSpacing':-999.}
        changed = self.layout(invalid)
        signature = lambda s: [(m['key'],m['start'],m['end']) for m in s['members']]
        self.assertEqual(signature(baseline),signature(changed))
        self.assertNotIn('barCenterSpacing',self.annotations(invalid))


if __name__ == '__main__':
    unittest.main()
