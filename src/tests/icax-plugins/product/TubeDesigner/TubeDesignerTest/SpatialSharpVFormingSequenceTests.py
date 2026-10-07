"""Real window hinges choose a seam-safe order without changing stock/cutters."""
from copy import deepcopy
import math
from time import perf_counter
import unittest

from WindowManufacturingInputTests import package, load, forbid_product_callbacks
from icax_template_sdk.manufacturing import compose_manufacturing_model


FRAME = 'outer_frame.spatial.continuous'


def dot(a, b):
    return sum(x*y for x, y in zip(a, b))


def cross(a, b):
    return [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]]


def rotate(point, axis, degrees, hinge=(0., 0., 0.)):
    value = [point[i]-hinge[i] for i in range(3)]
    cosine, sine = math.cos(math.radians(degrees)), math.sin(math.radians(degrees))
    perpendicular = cross(axis, value)
    return [hinge[i]+value[i]*cosine+perpendicular[i]*sine
            +axis[i]*dot(axis, value)*(1-cosine) for i in range(3)]


def reference_poses(folds, order, partial=None):
    """Independent point/axis Rodrigues updates, without target matrices."""
    poses = [{'origin': [0., 0., 0.], 'axes': [[1., 0., 0.], [0., 1., 0.], [0., 0., 1.]]}
             for _ in range(len(folds)+1)]
    for index in order:
        pose, fold = poses[index], folds[index]
        hinge = [pose['origin'][i]+sum(pose['axes'][j][i]*fold['hingePoint'][j] for j in range(3))
                 for i in range(3)]
        axis = [sum(pose['axes'][j][i]*fold['axis'][j] for j in range(3)) for i in range(3)]
        angle = partial if partial is not None and index == order[-1] else fold['angle']
        for target in poses[index+1:]:
            target['origin'] = rotate(target['origin'], axis, angle, hinge)
            target['axes'] = [rotate(value, axis, angle) for value in target['axes']]
    return poses


def reference_point(pose, point):
    return [pose['origin'][i]+sum(pose['axes'][j][i]*point[j] for j in range(3)) for i in range(3)]


def box(pose, interval, wall=None):
    point = [(interval[0]+interval[1])/2, 0., 0.]
    half = [(interval[1]-interval[0])/2, 19., 19.]
    if wall is not None:
        axis, sign = wall
        half[1], half[2] = 16.8, 16.8
        half[axis] = .6
        point[axis] = sign*18.4
    return reference_point(pose, point), pose['axes'], half


def margin(a, b):
    ca, aa, ha = a
    cb, ab, hb = b
    delta = [cb[i]-ca[i] for i in range(3)]
    margins = []
    for axis in [*aa, *ab, *[cross(u, v) for u in aa for v in ab]]:
        norm = math.sqrt(dot(axis, axis))
        if norm < 1.e-9:
            continue
        n = [value/norm for value in axis]
        radii = sum(half*abs(dot(n, basis)) for half, basis in zip(ha, aa))
        radii += sum(half*abs(dot(n, basis)) for half, basis in zip(hb, ab))
        margins.append(radii-abs(dot(n, delta)))
    return min(margins)


class SpatialSharpVFormingSequenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.selector = load('security_window_forming_sequence')
        _, defaults, core = package('single_face_security_window')
        cls.cases = []
        for face in ('two', 'three'):
            for side in ('left', 'right'):
                for dimensions in ((1200., 600., 1800.), (900., 350., 1100.)):
                    values = {**deepcopy(defaults), 'faceType': face, 'sidePosition': side,
                        'width': dimensions[0], 'sideWidth': dimensions[1], 'height': dimensions[2],
                        'accessDoorEnabled': face == 'three' and dimensions[0] == 1200.,
                        'frameManufacturingMode': 'spatial_v_notch',
                        'outerFrameGrooveTool': 'system:v-notch-sharp', 'foldedPostJoint': 'tabs',
                        'horizontalEndConnection': 'insert', 'verticalEndConnection': 'insert',
                        'assemblyClearance': .1}
                    before = deepcopy(values)
                    design, declaration = core.display(values), core.manufacturing(values)
                    with forbid_product_callbacks():
                        compiled = load('assembly_window_process').compile_window_assembly(
                            compose_manufacturing_model(declaration, design))
                        allocation = load('assembly_material_allocation').allocate_window_manufacturing(compiled)
                    assert values == before
                    stock = next(stock for stock in allocation['assemblySource']['stocks'] if stock['id'] == FRAME)
                    plan = allocation['allocationPlans'][FRAME]
                    folds = sorted(plan['forming'], key=lambda fold: fold['station'])
                    ranges = sorted(check['interval'] for check in plan['checks'] if check['id'] == 'material-range')
                    intervals = [[0., ranges[0][0]], *[[a[1], b[0]] for a, b in zip(ranges, ranges[1:])],
                                 [ranges[-1][1], stock['length']]]
                    spans = [{'segmentKey': str(index), 'interval': interval}
                             for index, interval in enumerate(intervals)]
                    cls.cases.append({'inputs': values, 'design': design, 'compiled': compiled,
                        'allocation': allocation, 'stock': stock, 'plan': plan, 'folds': folds,
                        'spans': spans, 'intervals': intervals})

    def test_actual_hinges_choose_clear_order_for_both_faces_sides_and_dimensions(self):
        for case in self.cases:
            values = case['inputs']
            with self.subTest(face=values['faceType'], side=values['sidePosition'], width=values['width']):
                inputs = (case['spans'], case['folds'], {'width': 38., 'depth': 38.})
                before = deepcopy(inputs)
                started = perf_counter()
                result = self.selector.choose_forming_order(*inputs, True)
                elapsed = perf_counter()-started
                self.assertEqual(inputs, before)
                n = len(case['folds'])
                expected = list(reversed(range(n)))
                expected[-2], expected[-1] = expected[-1], expected[-2]
                self.assertEqual(result['order'], [case['folds'][index]['instanceId'] for index in expected])
                check = result['motionCheck']
                self.assertEqual(check['method'], 'sampled-nonadjacent-rigid-envelopes')
                self.assertEqual(check['status'], 'pass')
                self.assertTrue(check['includesClosureSeam'])
                self.assertEqual(check['angleStepDegrees'], .5)
                self.assertEqual(check['rigidIntervals'], case['intervals'])
                self.assertIn('local notch deformation and forming-machine fixtures are not evaluated',
                              check['limitations'])
                print('forming-sequence', values['faceType'], values['sidePosition'], values['width'],
                      'folds', [index+1 for index in expected], 'seconds', round(elapsed, 4))

    def test_original_last_fold_has_real_seam_wall_collision_and_new_order_avoids_it(self):
        case = next(case for case in self.cases if case['inputs']['faceType'] == 'three'
                    and case['inputs']['sidePosition'] == 'right' and case['inputs']['width'] == 1200.)
        self.assertEqual(case['stock']['length'], 8384.)
        folds, intervals = case['folds'], case['intervals']
        original = list(reversed(range(len(folds))))
        poses = reference_poses(folds, original, partial=83.)
        # The boxes are contained within the actual R2/R1 38x38x1.2 shell,
        # excluding rounded corners and the complete V-cutter intervals.
        walls = [(axis, sign) for axis in (1, 2) for sign in (-1, 1)]
        actual_wall_overlap = max(margin(box(poses[0], intervals[0], a),
                                        box(poses[-1], intervals[-1], b)) for a in walls for b in walls)
        self.assertGreater(actual_wall_overlap, 1.)
        corrected = original[:-2]+[original[-1], original[-2]]
        for angle in [step/10 for step in range(901)]+[89.95, 89.99, 89.999]:
            with self.subTest(angle=angle):
                poses = reference_poses(folds, corrected, partial=angle)
                self.assertLessEqual(margin(box(poses[0], intervals[0]), box(poses[-1], intervals[-1])), 1.e-6)

    def test_chosen_order_refolds_all_source_endpoints_to_actual_display_without_changing_cutters(self):
        for case in self.cases:
            values = case['inputs']
            with self.subTest(face=values['faceType'], side=values['sidePosition'], width=values['width']):
                folds = case['folds']
                result = self.selector.choose_forming_order(case['spans'], folds, {'width':38., 'depth':38.}, True)
                lookup = {fold['instanceId']: index for index, fold in enumerate(folds)}
                poses = reference_poses(folds, [lookup[key] for key in result['order']])
                path_item = next(item for item in case['compiled']['items'] if item['key'] == FRAME)
                path = path_item['manufacturingInput']['path']
                vertices = {vertex['key']: vertex['point'] for vertex in path['vertices']}
                first = path['segments'][0]
                origin, end = vertices[first['from']], vertices[first['to']]
                axis = [(end[i]-origin[i])/math.dist(origin, end) for i in range(3)]
                axes = [axis, first['sectionFrame']['xAxis'], first['sectionFrame']['yAxis']]
                def world(point):
                    return [origin[i]+sum(axes[j][i]*point[j] for j in range(3)) for i in range(3)]
                item = next(item for item in case['allocation']['model']['items'] if item['key'] == FRAME)
                spans = sorted([span for member in item['properties']['manufacturing.sourceMembers']
                                for span in member['spans']], key=lambda span: span['stockStart'])
                display = {item['key']: item['properties']['assemblyFrame.member']
                           for item in case['design']['items']}
                for index, span in enumerate(spans):
                    original = display[span['itemKey']]
                    vector = [original['end'][i]-original['start'][i] for i in range(3)]
                    expected = [[original['start'][i]+fraction*vector[i] for i in range(3)]
                                for fraction in span['sourceRange']]
                    start = span['stockStart']+span['startReserve']
                    for station, expected_point in zip((start, start+math.dist(span['start'], span['end'])), expected):
                        self.assertLess(math.dist(world(reference_point(poses[index], [station, 0., 0.])),
                                                  expected_point), 1.e-7)
                self.assertLess(math.dist(reference_point(poses[0], [0., 0., 0.]),
                                          reference_point(poses[-1], [case['stock']['length'], 0., 0.])), 1.e-7)
                for index, basis in enumerate(poses[0]['axes']):
                    self.assertLess(math.dist(basis, poses[-1]['axes'][index]), 1.e-7)

    def test_invalid_actual_hinge_and_local_region_inputs_are_rejected_without_mutation(self):
        case = self.cases[0]
        for bad_kind in ('axis', 'station', 'duplicate', 'overlapping-intervals', 'closed'):
            with self.subTest(bad_kind=bad_kind):
                spans, folds = deepcopy((case['spans'], case['folds']))
                closed = True
                if bad_kind == 'axis': folds[0]['axis'] = [0., 0., 0.]
                if bad_kind == 'station': folds[0]['hingePoint'][0] += 1.
                if bad_kind == 'duplicate': folds[1]['instanceId'] = folds[0]['instanceId']
                if bad_kind == 'overlapping-intervals': spans[0]['interval'][1] = spans[1]['interval'][0]+1.
                if bad_kind == 'closed': closed = False
                before = deepcopy((spans, folds))
                with self.assertRaises(ValueError):
                    self.selector.choose_forming_order(spans, folds, {'width':38., 'depth':38.}, closed)
                self.assertEqual((spans, folds), before)


if __name__ == '__main__':
    unittest.main()
