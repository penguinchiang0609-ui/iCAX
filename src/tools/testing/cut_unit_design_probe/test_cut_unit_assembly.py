import copy
import math
import unittest
from collections import Counter

from run_stage1_units import DEST, ROOT, read
from cut_unit_stage1 import analyze_case


def plane(points):
    return {'isPlanar': True, 'method': 'SyntheticExactPlane', 'plane': {'origin': points[0], 'normal': [0, 0, 1]},
            'parameterDomainCovered': True, 'positiveWeights': True, 'maximumPlaneDeviationMm': 0.,
            'certificatePoints': points}


def edge(fid, gid, a, b, role, tangents=None, samples=None):
    length = math.dist(a, b)
    tangent = [(y-x)/length for x, y in zip(a, b)]
    first, last = tangents or (tangent, tangent)
    return {'groupId': gid, 'kind': role, 'closed': False, 'straight': tangents is None,
            'reason': '', 'seam': False, 'event': False, 'start': a, 'end': b,
            'startTangent': first, 'endTangent': last, 'startOnSkin': True, 'endOnSkin': True,
            'minSkinDistance': 0., 'maxSkinDistance': 0. if role == 'Skin' else .5,
            'sourceUses': [{'edgeId': fid*10+gid, 'reversed': False,
                            'lineLike': tangents is None, 'curveType': 0 if tangents is None else 1}],
            'polyline': samples or [[x+(y-x)*i/48 for x, y in zip(a, b)] for i in range(49)]}


def arc(fid, gid, radius, first, last, role='Skin', center=(0, 0, 0)):
    def point(angle):
        return [center[0]+radius*math.cos(angle), center[1]+radius*math.sin(angle), center[2]]
    sign = 1 if last > first else -1
    tangent = lambda angle: [-sign*math.sin(angle), sign*math.cos(angle), 0]
    return edge(fid, gid, point(first), point(last), role,
                tangents=(tangent(first), tangent(last)),
                samples=[point(first+(last-first)*i/48) for i in range(49)])


def face(fid, groups, support=None):
    points = support or [group['start'] for group in groups]
    return {'faceId': fid, 'surface': 'plane', 'area': 1., 'planeEvidence': plane(points),
            'wires': [{'wireId': 1, 'outer': True, 'rawEdgeUses': len(groups),
                       'expectedEdgeUses': len(groups), 'groups': groups}]}


def rectangle(fid, left, right, bottom=0., top=1., z=0.):
    points = [[left, bottom, z], [right, bottom, z], [right, top, z], [left, top, z]]
    return face(fid, [edge(fid, i+1, points[i], points[(i+1) % 4], role)
                      for i, role in enumerate(['Skin', 'Side', 'Skin', 'Side'])])


def case(faces):
    return {'version': 4, 'file': 'synthetic.step', 'faces': faces, 'profile': []}


def two_rectangles():
    return case([rectangle(1, 0, 1), rectangle(2, 1, 2)])


class AssemblyTests(unittest.TestCase):
    def test_coplanar_rectangles_merge_and_keep_end_sides(self):
        result = analyze_case(two_rectangles())
        self.assertEqual(len(result['assembledUnits']), 1)
        unit = result['assembledUnits'][0]
        self.assertEqual(unit['sourceRegion']['faceIds'], [1, 2])
        self.assertEqual(unit['form'], 'OpenBand')
        self.assertEqual((len(unit['boundaries']), len(unit['endChainIds']), len(unit['cutChainIds'])), (4, 2, 2))
        self.assertEqual(len(unit['axisConstraints']), 2)
        self.assertTrue(unit['coplanarity']['passed'])
        decision = result['mergeDecisions'][0]
        self.assertTrue(decision['accepted'])
        self.assertTrue(all(endpoint['passed'] for endpoint in decision['tangency']['endpoints']))
        self.assertEqual(len(unit['internalSeams']), 1)

    def test_every_source_use_remains_in_boundary_or_redundant_seam(self):
        result = analyze_case(two_rectangles()); unit = result['assembledUnits'][0]
        before = Counter((f['faceId'], use['edgeId']) for f in result['faces'] for g in f['groups'] for use in g['sourceUses'])
        after = Counter((use['sourceFaceId'], use['edgeId']) for chain in unit['boundaries'] for use in chain['sourceUses'])
        after.update((side['sourceFaceId'], use['edgeId']) for seam in unit['internalSeams'] for side in seam['sides'] for use in side['sourceUses'])
        self.assertEqual(before, after)
        self.assertEqual(sum(before.values()), 8)

    def test_only_one_cut_boundary_tangent_is_insufficient(self):
        data = two_rectangles()
        points = [[1, 0, 0], [2, 0, 0], [2, 1.2, 0], [1, 1, 0]]
        data['faces'][1] = face(2, [edge(2, i+1, points[i], points[(i+1) % 4], role)
                                  for i, role in enumerate(['Skin', 'Side', 'Skin', 'Side'])])
        result = analyze_case(data)
        self.assertFalse(result['assembledUnits'])
        proof = result['mergeDecisions'][0]['tangency']
        self.assertEqual(sum(point['passed'] for point in proof['endpoints']), 1)
        self.assertEqual(result['mergeDecisions'][0]['reason'], 'cut-boundary-not-g1')
        self.assertEqual(sum(len(f['units'][0]['axisConstraints']) for f in result['faces']), 4)

    def test_collinear_foldback_is_not_g1_continuation(self):
        data = case([rectangle(1, 0, 1), rectangle(2, 0, 1)])
        result = analyze_case(data)
        self.assertFalse(result['assembledUnits'])
        proofs = [decision['tangency'] for decision in result['mergeDecisions']]
        self.assertTrue(proofs)
        self.assertTrue(all(not proof['passed'] for proof in proofs))
        self.assertTrue(any(point['angleRadians'] > 3 for proof in proofs for point in proof['endpoints']))

    def test_nonplanar_or_missing_surface_evidence_keeps_side_constraints(self):
        for evidence in [None, {'isPlanar': False, 'reason': 'cylinder'}]:
            data = two_rectangles(); data['faces'][1]['planeEvidence'] = evidence
            result = analyze_case(data)
            self.assertFalse(result['assembledUnits'])
            self.assertTrue(all(len(f['axisConstraints']) == 2 for f in result['faces']))

    def test_whole_component_plane_prevents_adjacent_tolerance_drift(self):
        data = case([rectangle(1, 0, 1), rectangle(2, 1, 2, z=.75e-7), rectangle(3, 2, 3, z=1.5e-7)])
        result = analyze_case(data)
        self.assertEqual([u['sourceRegion']['faceIds'] for u in result['assembledUnits']], [[1, 2]])
        rejection = next(d for d in result['mergeDecisions'] if d['sourceFaceIds'] == [2, 3])
        self.assertFalse(rejection['accepted'])
        self.assertEqual(rejection['coplanarity']['sourceFaceIds'], [1, 2, 3])
        self.assertGreater(rejection['coplanarity']['maxPlaneDistanceMm'], 1e-7)

    def test_full_support_points_not_only_boundary_samples_prove_plane(self):
        data = two_rectangles()
        data['faces'][1]['planeEvidence']['certificatePoints'].append([1.5, .5, 1e-4])
        self.assertFalse(analyze_case(data)['assembledUnits'])

    def test_missing_exact_endpoint_tangent_does_not_use_polyline_guess(self):
        data = two_rectangles(); data['faces'][1]['wires'][0]['groups'][0]['startTangent'] = None
        result = analyze_case(data)
        self.assertFalse(result['assembledUnits'])
        self.assertEqual(result['mergeDecisions'][0]['reason'], 'cut-boundary-tangent-unavailable')

    def test_partial_side_overlap_is_not_full_side_correspondence(self):
        data = case([rectangle(1, 0, 1), rectangle(2, 1, 2, .2, .8)])
        self.assertFalse(analyze_case(data)['assembledUnits'])

    def test_multiple_side_owners_are_not_consumed(self):
        data = two_rectangles(); duplicate = copy.deepcopy(data['faces'][0]); duplicate['faceId'] = 99
        data['faces'].append(duplicate); result = analyze_case(data)
        self.assertFalse(result['assembledUnits'])
        self.assertIn('ambiguous-multiple-neighbors', [d['reason'] for d in result['mergeDecisions']])

    def test_line_and_tangent_arcs_remain_distinct_logical_primitives(self):
        bottom = arc(2, 1, 1, -math.pi/2, 0, center=(1, 1, 0))
        right = edge(2, 2, [2, 1, 0], [2, 2, 0], 'Side')
        top = arc(2, 3, 1, 0, -math.pi/2, center=(1, 2, 0))
        left = edge(2, 4, [1, 1, 0], [1, 0, 0], 'Side')
        support = [[1, 0, 0], [2, 0, 0], [2, 2, 0], [1, 2, 0]]
        result = analyze_case(case([rectangle(1, 0, 1), face(2, [bottom, right, top, left], support)]))
        self.assertEqual(len(result['assembledUnits']), 1)
        unit = result['assembledUnits'][0]
        self.assertEqual(unit['form'], 'OpenBand')
        self.assertEqual(sum(len(c['logicalEdges']) for c in unit['boundaries'] if c['purpose'] == 'CutBoundary'), 4)
        self.assertEqual(sum(g['geometry'] == 'Circle' for f in result['faces'] for g in f['groups']), 2)
        self.assertEqual(sum(len(f['groups']) for f in result['faces']), 8)

    def test_two_planar_semidisks_form_a_bar_end_region(self):
        support = [[-1, -1, 0], [1, -1, 0], [1, 1, 0], [-1, 1, 0]]
        first = face(1, [arc(1, 1, 1, -math.pi/2, math.pi/2), edge(1, 2, [0, 1, 0], [0, -1, 0], 'Side')], support)
        second = face(2, [edge(2, 1, [0, -1, 0], [0, 1, 0], 'Side'), arc(2, 2, 1, math.pi/2, 3*math.pi/2)], support)
        result = analyze_case(case([first, second])); unit = result['assembledUnits'][0]
        self.assertEqual(unit['form'], 'UnpairedRegion')
        self.assertEqual((len(unit['boundaries']), unit['eulerCharacteristic']), (1, 1))
        self.assertFalse(unit['endChainIds']); self.assertFalse(unit['axisConstraints'])

    def test_two_planar_half_annuli_form_a_closed_band(self):
        first = face(1, [arc(1, 1, 2, 0, math.pi), edge(1, 2, [-2, 0, 0], [-1, 0, 0], 'Side'),
                         arc(1, 3, 1, math.pi, 0), edge(1, 4, [1, 0, 0], [2, 0, 0], 'Side')],
                     [[-2, 0, 0], [2, 0, 0], [2, 2, 0], [-2, 2, 0]])
        second = face(2, [arc(2, 1, 2, math.pi, 2*math.pi), edge(2, 2, [2, 0, 0], [1, 0, 0], 'Side'),
                          arc(2, 3, 1, 0, -math.pi), edge(2, 4, [-1, 0, 0], [-2, 0, 0], 'Side')],
                      [[-2, -2, 0], [2, -2, 0], [2, 0, 0], [-2, 0, 0]])
        result = analyze_case(case([first, second])); unit = result['assembledUnits'][0]
        self.assertEqual(unit['form'], 'ClosedBand')
        self.assertEqual((len(unit['boundaries']), len(unit['internalSeams']), unit['eulerCharacteristic']), (2, 2, 0))
        self.assertTrue(all(c['closed'] and c['roles'] == ['Skin'] for c in unit['boundaries']))

    def test_rotation_translation_and_face_order_keep_the_same_partition(self):
        data = two_rectangles(); data['faces'].reverse()
        vector = lambda p: [-p[2], p[0], p[1]]
        point = lambda p: [x+y for x, y in zip(vector(p), [50, -70, 18])]
        for item in data['faces']:
            evidence = item['planeEvidence']; evidence['plane']['origin'] = point(evidence['plane']['origin'])
            evidence['plane']['normal'] = vector(evidence['plane']['normal'])
            evidence['certificatePoints'] = [point(p) for p in evidence['certificatePoints']]
            groups = item['wires'][0]['groups']; item['wires'][0]['groups'] = groups[1:]+groups[:1]
            for group in groups:
                group['start'] = point(group['start']); group['end'] = point(group['end'])
                group['polyline'] = [point(p) for p in group['polyline']]
                group['startTangent'] = vector(group['startTangent']); group['endTangent'] = vector(group['endTangent'])
        unit = analyze_case(data)['assembledUnits'][0]
        self.assertEqual(unit['sourceRegion']['faceIds'], [1, 2])
        self.assertEqual(unit['form'], 'OpenBand'); self.assertEqual(len(unit['axisConstraints']), 2)

    def test_real_case003_non_coplanar_sliver_is_no_longer_consumed(self):
        data = read(DEST/'boundaries/003.json')
        if not all('planeEvidence' in f for f in data['faces']):
            evidence_path = ROOT/'output/tests/cut-unit-merge-evidence-checks/003.json'
            evidence = {f['faceId']: f for f in read(evidence_path)['faces']}
            for item in data['faces']:
                item['planeEvidence'] = evidence[item['faceId']]
        result = analyze_case(data)
        self.assertFalse(result['assembledUnits'])
        decision = next(d for d in result['mergeDecisions'] if d['sourceFaceIds'] == [1, 3])
        self.assertFalse(decision['coplanarity']['passed'])
        self.assertEqual(decision['reason'], 'units-not-coplanar')
        self.assertTrue(result['faces'][0]['axisConstraints'])
        self.assertTrue(result['faces'][2]['axisConstraints'])


if __name__ == '__main__':
    unittest.main(verbosity=2)
