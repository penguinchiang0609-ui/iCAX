"""Spatial declarations and exact section contacts; native BRep is a separate gate."""
from copy import deepcopy
import math
import unittest

from WindowCatalogueTests import package
from ProductManufacturingDeclarationTests import execute_definition, forbid_processing
from icax_template_sdk import NeutralModel, to_display_model
from icax_template_sdk.profile_constraints import _profile_validation_scope


class MinimalGrilleSpatialManufacturingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.descriptor, cls.defaults, cls.template = package('minimal_protective_grille')
        cls.rules = cls.template._load(cls.template.ROOT/'manufacturing_rules.py')
        cls.geometry = cls.template._load(cls.template.ROOT/'spatial_geometry.py')
        cls.installation = cls.template._load(cls.template.ROOT/'installation_holes.py')

    def values(self, **changes):
        values = {**self.defaults, 'layoutStyle': 'five_face',
                'supportStyle': 'single', 'supportWidth': 20., 'supportDepth': 10.,
                'frameProfileType': 'rect', 'innerProfileType': 'rect', 'supportProfileType': 'rect',
                'frameWidth': 40., 'frameDepth': 30.,
                'frameCornerRadius': 2., 'frameWallThickness': 1.5,
                'innerWidth': 10., 'innerDepth': 5., 'innerCornerRadius': 1.,
                'innerWallThickness': .8, 'maleHeadWidth': 4., 'maleHeadLength': 5.,
                'maleCornerType': 'square', 'minimumFeatureWeb': .2,
                'handleEnabled': 'no', 'installHoleEnabled': 'no', **changes}
        for prefix in ('projectionVertical', 'projectionHorizontal'):
            for suffix in ('ProfileType', 'Width', 'Depth', 'WallThickness', 'CornerRadius'):
                if prefix+suffix not in changes:
                    values[prefix+suffix] = values['frame'+suffix]
        return values

    def profiles(self, values):
        with _profile_validation_scope(self.descriptor):
            state = self.template.layout({**values, 'layoutStyle': 'planar'})
        return [next(m['profile'] for m in state['members'] if m['role'].startswith(prefix))
                for prefix in ('frame.', 'inner.')]

    def member(self, key, role, profile, start, end, x, y):
        g = self.geometry
        axis = g.unit(g.sub(end, start))
        return {'key': key, 'name': key, 'role': role, 'profile': profile,
                'start': list(start), 'end': list(end), 'sectionFrame': {
                    'originAtStart': list(start), 'xAxis': list(x), 'yAxis': list(y),
                    'zAxis': axis, 'centerlineUV': [0., 0.]}}

    def fixture(self, values, slope=.1):
        g = self.geometry
        a, b = self.profiles(values)
        host = self.member('frame.top', 'frame.top', a, [0., 0., 0.], [1000., 0., 0.],
                           [0., 0., 1.], [0., -1., 0.])
        axis = g.unit([0., 1., slope])
        point = g.contact_offset(host, [400., 0., 0.], axis)
        branch = self.member('inner.top', 'inner.top', b, point, g.add(point, axis, 300),
                             [0., -axis[2], axis[1]], [1., 0., 0.])
        return {'members': [host, branch], 'connections': [{'key': 'joint',
            'hostKey': host['key'], 'branchKey': branch['key'], 'branchEnd': 'start',
            'point': point, 'normal': axis}], 'facts': {'installationHoles': {'enabled': False}}}

    def design(self, values, layout):
        model = NeutralModel(template_id='minimal-protective-grille', template_version='2.3.1',
                             package_digest='', parameters=deepcopy(values))
        builder = self.template._load(self.template.SHARED/'shared_tube_geometry.py').SharedTubeGeometry(model)
        keys = []
        for m in layout['members']:
            key, p, f = m['key'], m['profile'], m['sectionFrame']
            raw = builder.emit_tube(key, profile_arguments={'placement': {
                'origin': m['start'], 'xAxis': f['xAxis'], 'yAxis': f['yAxis']}, 'contours': p.contours()},
                extrude_arguments={'vector': self.geometry.sub(m['end'], m['start'])})
            section = model.geometry(key+'.section', 'profile2d', arguments={'contours': p.contours()})
            profile = p.properties()
            profile.update(sectionResource=section, sectionCoordinateMap=[[1, 0], [0, 1]])
            keys.append(model.item(key, key, representations={'display': raw}, properties={
                'partNumber': key, 'length': math.dist(m['start'], m['end']), 'quantity': 1,
                'partKind': 'tube', 'materialCategory': 'tube', 'sourcing': 'made',
                'tubeDesigner.profile': profile, 'assemblyFrame.member': {
                    'start': m['start'], 'end': m['end'], 'axisLength': math.dist(m['start'], m['end']),
                    'sectionFrame': f}}))
        model.output('display.default', 'display', keys)
        return to_display_model(model.build())

    def execute(self, values, layout):
        before = deepcopy(values)
        positions = [(m['key'], deepcopy(m['start']), deepcopy(m['end'])) for m in layout['members']]
        with _profile_validation_scope(self.descriptor):
            with forbid_processing():
                declaration = self.rules.manufacturing(values, layout)
            result = execute_definition(declaration, design=self.design(values, layout))
        self.assertEqual(values, before)
        self.assertEqual(positions, [(m['key'], m['start'], m['end']) for m in layout['members']])
        self.assertEqual(len(result['items']), len(layout['members']))
        return declaration, result

    def test_exact_oblique_contact_uses_wall_intersection_not_support_box(self):
        values = self.values()
        layout = self.fixture(values)
        p = layout['connections'][0]['point']
        self.assertAlmostEqual(p[1], 15.)
        self.assertAlmostEqual(p[2], 1.5)
        roots = self.geometry.ray_intersections(layout['members'][0], p,
                                               layout['connections'][0]['normal'])
        self.assertAlmostEqual(roots[1], 0.)
        self.assertAlmostEqual(-roots[0], 30*math.sqrt(1.01))

    def test_contact_tracks_real_rounded_corner_and_round_profile(self):
        values = self.values(frameWidth=40., frameDepth=40., frameCornerRadius=8.)
        layout = self.fixture(values, slope=1.)
        point = layout['connections'][0]['point']
        self.assertAlmostEqual(point[1], 12+8/math.sqrt(2))
        self.assertAlmostEqual(point[2], point[1])
        # Query the real circular profile through the product's normal resource binding.
        round_values = self.values(frameProfileType='round', frameWidth=40., frameDepth=40.)
        a, _ = self.profiles(round_values)
        self.assertEqual(a.kind, 'round')
        round_layout = self.fixture(round_values, slope=1.)
        p = round_layout['connections'][0]['point']
        self.assertAlmostEqual(math.hypot(p[1], p[2]), a.width/2)

    def test_sloping_half_and_through_heads_use_real_receiver_envelope(self):
        for mode in ('male_female_half_hole', 'male_female_through_hole'):
            for form in ('tongue', 'full_section'):
                with self.subTest(mode=mode, form=form):
                    values = self.values(jointMode=mode, maleEndForm=form)
                    layout = self.fixture(values)
                    declaration, actual = self.execute(values, layout)
                    head = next(p['definition'] for p in declaration['processes']
                                if p['definition']['templateId'] == 'structural-male-head')
                    mate, = head['processInput']['geometry']['mateEnds']
                    self.assertEqual(len(mate['envelope']['contours']), 1)
                    self.assertNotEqual(mate['envelope']['placement']['yAxis'], [0., 1., 0.])
                    self.assertLess(math.dist(head['processInput']['parts']['stock']['data']['initialGeometry']
                                     ['itemPlacement']['xAxis'], layout['connections'][0]['normal']), 1.e-12)
                    instances = actual['extensions']['tubeDesigner.assemblyGeometryProcesses']['instances']
                    self.assertTrue(all(i['result']['applicable'] for i in instances))

    def test_oblique_butt_weld_receives_exact_fit_and_not_unmachined_square_end(self):
        values = self.values(jointMode='butt_weld')
        layout = self.fixture(values)
        declaration, _ = self.execute(values, layout)
        fit, = [p['definition'] for p in declaration['processes']
                if p['definition']['templateId'] == 'structural-stock-fit']
        self.assertEqual(len(fit['processInput']['geometry']['receivers']), 1)
        self.assertEqual(len(fit['processInput']['geometry']['keepRegions']), 1)
        witness = fit['processInput']['geometry']['keepConnectedTo']
        branch = layout['members'][1]
        midpoint = [(a+b)/2 for a, b in zip(branch['start'], branch['end'])]
        self.assertAlmostEqual(math.dist(witness, midpoint), branch['profile'].width/2-branch['profile'].wall/2)

    def test_nonorthogonal_side_contact_uses_true_wall_entry(self):
        values = self.values(jointMode='male_female_half_hole')
        a, b = self.profiles(values)
        g = self.geometry
        host = self.member('frame.side', 'frame.side', a, [0, 0, 0], [0, 0, 1000],
                           [1, 0, 0], [0, 1, 0])
        axis = g.unit([0, 1, .2])
        point = g.contact_offset(host, [0, 0, 400], axis)
        self.assertAlmostEqual(point[1], 15.)
        self.assertAlmostEqual(point[2], 403.)
        branch = self.member('inner.side', 'inner.side', b, point, g.add(point, axis, 300),
                             [0, -axis[2], axis[1]], [1, 0, 0])
        layout = {'members': [host, branch], 'connections': [{'key': 'side',
            'hostKey': host['key'], 'branchKey': branch['key'], 'branchEnd': 'start',
            'point': point, 'normal': axis}], 'facts': {'installationHoles': {'enabled': False}}}
        self.execute(values, layout)

    def test_crossing_preserves_two_whole_members_and_drills_both_host_walls(self):
        values = self.values(jointMode='butt_weld')
        a, b = self.profiles(values)
        host = self.member('frame.support', 'frame.support', a, [500, 0, 0], [500, 0, 1000],
                           [1, 0, 0], [0, 1, 0])
        branch = self.member('inner.row', 'inner.full', b, [0, 0, 500], [1000, 0, 500],
                             [0, 0, 1], [0, -1, 0])
        layout = {'members': [host, branch], 'connections': [{'key': 'cross', 'kind': 'cross-through',
            'hostKey': host['key'], 'branchKey': branch['key'], 'point': [500, 0, 500]}],
            'facts': {'installationHoles': {'enabled': False}}}
        declaration, _ = self.execute(values, layout)
        preparations = [p['definition']['processInput']['geometry']['axialInterval']
                        for p in declaration['processes'] if p['key'].endswith('.prepare')]
        self.assertEqual(preparations, [{'startStation': 0., 'endStation': 1000.}]*2)
        holes = next(p['definition']['processInput']['geometry']['holes']
                     for p in declaration['processes'] if p['key'].endswith('.apertures'))
        self.assertEqual(holes[0]['vector'], [1000., 0., 0.])
        self.assertTrue(declaration['connections'][0]['properties']['continuousMembers'])

    def test_rear_installation_uses_real_y_and_rear_total_height(self):
        values = self.values(installHoleEnabled='yes', installHoleCountPerSide=3,
                             installHoleTopOffset=50., installHoleBottomOffset=50.,
                             installHoleLargeDiameter=6., installHoleSmallDiameter=4.)
        a, _ = self.profiles(values)
        members = [self.member('frame.'+side, 'frame.'+side, a,
                              [x, 400, 0], [x, 400, 1300], [1, 0, 0], [0, 1, 0])
                   for side, x in (('left', 20), ('right', 980))]
        plan = self.installation.plan(values, members)
        self.assertEqual(plan['centers'], [50., 650., 1250.])
        self.assertTrue(all(p['point'][1] == 400 for p in plan['groups']))
        declaration, _ = self.execute(values, {'members': members, 'connections': [],
                                               'facts': {'installationHoles': plan}})
        holes = [h for p in declaration['processes'] if p['key'].endswith('.apertures')
                 for h in p['definition']['processInput']['geometry']['holes']]
        self.assertEqual(len(holes), 12)
        self.assertTrue(all(h['placement']['origin'][1] == 400 for h in holes))

    def test_wrong_frames_and_surface_points_are_rejected(self):
        values = self.values(jointMode='male_female_half_hole')
        layout = self.fixture(values)
        layout['members'][0]['sectionFrame']['xAxis'] = [1, 0, 0]
        with self.assertRaisesRegex(ValueError, '坐标系'):
            self.rules.manufacturing(values, layout)
        layout = self.fixture(values)
        layout['connections'][0]['point'][1] += 1
        with self.assertRaises(ValueError):
            self.rules.manufacturing(values, layout)

    def test_through_tip_corner_preserved_on_true_square_contact_and_rejected_on_slope(self):
        for corner in ('round', 'chamfer'):
            with self.subTest(corner=corner):
                values = self.values(jointMode='male_female_through_hole', maleCornerType=corner,
                                     maleCornerSize=.5)
                declaration, _ = self.execute(values, self.fixture(values, slope=0.))
                head = next(p['definition'] for p in declaration['processes']
                            if p['definition']['templateId'] == 'structural-male-head')
                self.assertAlmostEqual(head['parameters']['length'], 30.)
                self.assertEqual(head['parameters']['corner'], corner)
                with self.assertRaisesRegex(ValueError, '斜面或曲面齐平'):
                    self.rules.manufacturing(values, self.fixture(values, slope=.1))
                half = {**values, 'jointMode': 'male_female_half_hole'}
                self.execute(half, self.fixture(half, slope=.1))

    def test_return_joint_control_is_independent_and_common_receiver_declares_native_pairs(self):
        values = self.values(width=568., height=400., projectionDepth=100., topDrop=30.,
            supportMaxSpacing=800., barLayoutMode='fixed_count', barCount=3,
            jointMode='male_female_through_hole', returnJointMode='butt_weld')
        with _profile_validation_scope(self.descriptor):
            layout = self.template.layout(values)
            declaration = self.rules.manufacturing(values, layout)
        members = {m['key']: m for m in layout['members']}
        originals = {c['key']: c for c in layout['connections']}
        return_count = 0
        for connection in declaration['connections']:
            source = originals[connection['key']]
            role = members[source['branchKey']]['role']
            if role.startswith(('inner.return_', 'inner.top_return', 'inner.bottom_return')):
                self.assertEqual(connection['properties']['recipe'], 'butt_weld')
                return_count += 1
        self.assertGreater(return_count, 0)
        pairs = [pair for c in declaration['connections']
                 for pair in c['properties'].get('nonInterferencePairs', [])]
        self.assertTrue(any(members[p['items'][0]]['role'].startswith('inner.')
                            and members[p['items'][1]]['role'].startswith('inner.return_') for p in pairs))
        self.assertTrue(all(p['maximumIntersectionVolumeMm3'] == 1.e-5 for p in pairs))

    def test_only_eight_side_corners_fit_both_actual_adjacent_receivers(self):
        values = self.values(width=568., height=400., projectionDepth=100., topDrop=30.,
            supportMaxSpacing=800., barLayoutMode='fixed_count', barCount=3,
            frameProfileType='round', frameWidth=30., innerProfileType='round', innerWidth=10.,
            jointMode='butt_weld', returnJointMode='butt_weld')
        with _profile_validation_scope(self.descriptor):
            layout = self.template.layout(values)
            declaration = self.rules.manufacturing(values, layout)
        members = {m['key']: m for m in layout['members']}
        processes = {p['key']: p['definition'] for p in declaration['processes']}
        found = set()
        for connection in layout['connections']:
            key = connection['branchKey']
            branch = members[key]
            end = connection['branchEnd']
            expected = []
            if branch['role'] in ('frame.return_left', 'frame.return_right'):
                # Skeleton boundaries now have their own stable material keys.
                # Their exact round-wall contact points still shift on a slope.
                boundary = key.rsplit('.', 1)[1]
                if boundary:
                    prefix = 'frame.' if end == 'start' else 'frame.rear_'
                    expected = [prefix+boundary+'.0001']
                    found.add((branch['role'], boundary, end))
            self.assertEqual(connection.get('fitHostKeys', []), expected)
            receivers = processes[key+'.fit.'+end]['processInput']['geometry']['receivers']
            self.assertEqual(len(receivers), 1+len(expected))
            for receiver, host_key in zip(receivers, [connection['hostKey'], *expected]):
                host = members[host_key]
                start, finish = list(host['start']), list(host['end'])
                if host_key in expected:
                    # The neighbour's round-cope shoulder reaches the far skin
                    # of this 30 mm upright, not just the nominal near skin.
                    # Its other end must not be extrapolated at this corner.
                    if branch['role'] == 'frame.return_left':
                        start[0] -= 30.
                    else:
                        finish[0] += 30.
                    self.assertEqual(len(receiver['subtractRegions']), 1)
                    self.assertEqual(receiver['subtractRegions'][0], receivers[0])
                    self.assertNotEqual(receiver['keepConnectedTo'], list(host['start']))
                    receiver = receiver['section']
                self.assertLess(math.dist(receiver['placement']['origin'], start), 1.e-8)
                self.assertEqual(receiver['contours'], [host['profile'].contours()[0]])
                self.assertLess(math.dist(receiver['vector'], self.geometry.sub(finish, start)), 1.e-8)
        self.assertEqual(len(found), 8)


if __name__ == '__main__':
    unittest.main()
