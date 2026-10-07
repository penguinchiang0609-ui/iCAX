"""The unchanged real edge cutter owns its root arc and asymmetric offsets."""
from copy import deepcopy
import math
import unittest

from AssemblyFoldFunctionsTests import folds, process, stock
from WindowManufacturingInputTests import load


def dot(a, b):
    return sum(x*y for x, y in zip(a, b))


def point(matrix, p):
    return [sum(matrix[4*i+j]*p[j] for j in range(3))+matrix[4*i+3] for i in range(3)]


def placed(frame, p):
    return [frame['origin'][i]+sum(frame[key][i]*p[j] for j, key in enumerate(('xAxis','yAxis','zAxis')))
            for i in range(3)]


def local(frame, p):
    delta = [p[i]-frame['origin'][i] for i in range(3)]
    return [dot(delta, frame[key]) for key in ('xAxis','yAxis','zAxis')]


def polygon_contains(contour, point):
    x, z = point
    inside = False
    for edge in contour['segments']:
        assert edge['kind'] == 'line'
        (a, b), (c, d) = edge['start'], edge['end']
        if (b > z) != (d > z) and x < (c-a)*(z-b)/(d-b)+a:
            inside = not inside
    return inside


class EdgeArcFinalPoseTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.adapter = load('security_window_process_adapter')

    def evaluate(self, left=True, k=0., rotation=0., angle=90.):
        part = stock(width=38, depth=38)
        part['parameters'].update(wallThickness=1.2, cornerRadius=2., innerRadius=1.)
        data = process(rotation=rotation, angle=angle, part=part)
        parameters = {'leftArc': left, 'bendCompensation': k > 0., 'kFactor': k}
        drafts = {'node-slot': {'edge-arc-groove': parameters}}
        before = deepcopy((data, drafts))
        result = folds.evaluate_fold('node-edge-arc-integrated', data, {}, drafts)
        self.assertTrue(result['applicable'], result['reason'])
        self.assertEqual((data, drafts), before)
        self.assertEqual(result['parameters'], {})
        return data, result

    def test_real_default_and_mirror_have_correct_distinct_offsets_and_final_root_endpoint(self):
        expected_short = 37.*(math.pi/2.-2.)+18.
        for left in (True, False):
            for rotation in (0., 90., 180., 270.):
                with self.subTest(left=left, rotation=rotation):
                    _, result = self.evaluate(left, rotation=rotation)
                    fold = result['forming'][0]
                    reserves = self.adapter.corner_reserves(fold)
                    expected = (18., expected_short) if left else (expected_short, 18.)
                    self.assertAlmostEqual(reserves['incoming'], expected[0])
                    self.assertAlmostEqual(reserves['outgoing'], expected[1])
                    arc = fold['rootArc']
                    self.assertAlmostEqual(arc['radius'], 37.)
                    self.assertAlmostEqual(arc['rootArcLength'], 37.*math.pi/2.)
                    self.assertLess(math.dist(point(fold['targetTransform'], arc['flatRootEnd']),
                                              arc['formedRootEnd']), 1.e-8)
                    # Transform the outgoing nominal centreline start to the
                    # incoming one. Those raw points are not the same distance
                    # from the tool station for a one-sided retained arc.
                    transformed = local(fold['frame'], point(fold['targetTransform'],
                        placed(fold['frame'], [reserves['outgoing'], 0., 0.])))
                    self.assertLess(math.dist(transformed, [-reserves['incoming'], 0., 0.]), 1.e-8)
                    translation = local(fold['frame'], point(fold['targetTransform'],
                                                            placed(fold['frame'], [0., 0., 0.])))
                    self.assertLess(math.dist(translation, [-expected[0], 0., -expected[1]]), 1.e-8)
                    self.assertEqual(fold['kind'], 'distributed-root-arc')
                    self.assertEqual(fold['hingePointKind'], 'equivalent-final-pose-centre')
                    self.assertEqual(fold['validation'], 'not-performed')
                    self.assertEqual(fold['localFolds'], [])
                    with self.assertRaisesRegex(ValueError, '不同的前后材料边界'):
                        self.adapter.corner_reserve(fold)

    def test_existing_k_compensation_is_counted_once_without_changing_radius_or_base_offsets(self):
        compensation = math.pi/2.*.62*1.2
        for left in (True, False):
            _, plain = self.evaluate(left)
            _, compensated = self.evaluate(left, .62)
            a, b = plain['forming'][0], compensated['forming'][0]
            for key in ('incoming', 'outgoing'):
                self.assertAlmostEqual(self.adapter.corner_reserves(a)[key], self.adapter.corner_reserves(b)[key])
            self.assertAlmostEqual(b['materialLengthAddition'], compensation)
            self.assertEqual(b['rootArc']['radius'], a['rootArc']['radius'])
            ia, ib = a['rootArc']['flatRootInterval'], b['rootArc']['flatRootInterval']
            self.assertAlmostEqual(ib[0], ia[0]-compensation/2.)
            self.assertAlmostEqual(ib[1], ia[1]+compensation/2.)
            self.assertAlmostEqual(ib[1]-ib[0], 37.*math.pi/2.+compensation)
            reserves = self.adapter.corner_reserves(b)
            actual = local(b['frame'], point(b['targetTransform'], placed(b['frame'],
                           [reserves['outgoing']+compensation/2., 0., 0.])))
            self.assertLess(math.dist(actual, [-reserves['incoming']-compensation/2., 0., 0.]), 1.e-8)
            # Complete plan identities are provided by the native host. The
            # returned material amount already represents K; it is not added
            # again to that amount by the adapter.
            identifier = 'actual-edge'
            plan = {'forming': [{**deepcopy(b), 'instanceId':identifier}],
                    'operations': [{**deepcopy(compensated['operations'][0]), 'instanceId':identifier}]}
            self.assertAlmostEqual(self.adapter.bend_allowances(plan)[identifier], compensation)
            self.assertEqual(compensated['materialRequirements'][0]['lengthAddition'], 0.)
            wrong = deepcopy(plan)
            wrong['forming'][0]['materialLengthAddition'] *= 2
            with self.assertRaisesRegex(ValueError, '实际槽口参数'):
                self.adapter.bend_allowances(wrong)

    def test_actual_default_cut_keeps_distinct_rigid_material_disjoint_at_final_pose(self):
        # Classify against the actual frozen base-minus-cylinder cutter, not a
        # substituted V or an exterior mesh. Exclude only the deforming root
        # band, whose distributed plastic material is outside the rigid proof.
        for left in (True, False):
            for k in (0., .62):
                with self.subTest(left=left, k=k):
                    data, result = self.evaluate(left, k)
                    feature, fold = result['operations'][0]['requestFeature'], result['forming'][0]
                    profile, analysis = folds._section(data['parts']['stock'])
                    shell = folds._sections.closed_shell_metrics(analysis)
                    snapshot = folds._punch._evaluate(feature['toolRef'], feature['toolParameters'], {
                        'target':'part', 'lengthUnit':'mm', 'targetSection':profile,
                        'targetSectionAnalysis':analysis,
                        'bounds': {'min':[0,*shell['outside']['min']], 'max':[1000,*shell['outside']['max']]},
                        'placement':{'rotation':0.}, 'feature':{'reference':'start','station':500.,'face':'top'}})
                    self.assertEqual(snapshot['parameters'], feature['toolParameters'])
                    self.assertEqual(snapshot['ref']['digest'], feature['toolRef']['digest'])
                    nodes = {node['key']:node for node in snapshot['geometry']['model']['geometry']}
                    self.assertEqual(nodes['notch']['arguments']['operation'], 'subtract')
                    base = nodes['notch-base-profile']['arguments']['contours'][0]
                    centre = nodes['arc-cylinder-profile']['arguments']['placement']['origin']
                    arc_segment = nodes['arc-cylinder-profile']['arguments']['contours'][0]['segments'][0]
                    radius = folds._curves.arc(arc_segment['start'], arc_segment['middle'], arc_segment['end'])['radius']
                    a, b = fold['rootArc']['flatRootInterval']
                    root = fold['rootArc']['rootHeight']
                    def remaining(p):
                        # Current test samples remain on exact flat wall strips,
                        # away from R2/R1 rounded corners.
                        if not (-19. < p[1] < 19. and -19. < p[2] < 19.): return False
                        if abs(p[1]) <= 17.8 and abs(p[2]) <= 17.8: return False
                        in_base = polygon_contains(base, [p[0],p[2]])
                        in_cylinder = math.hypot(p[0]-centre[0],p[2]-centre[2]) < radius
                        return not (in_base and not in_cylinder)
                    def rigid(p, incoming):
                        if not remaining(p): return False
                        if a < p[0] < b and p[2] <= root: return False
                        return p[0] < (b if left else a) if incoming else p[0] > (b if left else a)
                    checked = 0
                    for station in [a+.17+index*.5 for index in range(math.ceil((b-a+60.)/.5))]:
                        for z in [value+.1 for value in range(-16,17,2)]:
                            for y in (-18.4,18.4):
                                original = [station,y,z]
                                if not rigid(original, False): continue
                                transformed = local(fold['frame'], point(fold['targetTransform'], placed(fold['frame'],original)))
                                self.assertFalse(rigid(transformed,True), (left,k,original,transformed))
                                checked += 1
                    self.assertGreater(checked,1000)

    def test_other_fold_angles_and_signed_stock_faces_preserve_real_arc_endpoint(self):
        for left in (True,False):
            for angle in (60.,90.,120.,-90.):
                _, result = self.evaluate(left, rotation=90., angle=angle)
                fold = result['forming'][0]
                self.assertLess(math.dist(point(fold['targetTransform'], fold['rootArc']['flatRootEnd']),
                                          fold['rootArc']['formedRootEnd']), 1.e-8)
                self.assertAlmostEqual(fold['rootArc']['angle'],abs(angle))
                expected_axis = [-value for value in fold['frame']['yAxis']]
                self.assertEqual(fold['axis'],expected_axis)


if __name__ == '__main__':
    unittest.main()
