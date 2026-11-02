"""Reject lost Side constraints or removal evidence that does not satisfy the rule."""
import copy
import unittest

from validate_stage1_units import audit_assembly_rules


def source_group(key, role, edge):
    return {'key': key, 'role': role, 'geometry': 'Line', 'sourceUses': [{'edgeId': edge, 'reversed': False}],
            'start': [edge, 0, 0], 'end': [edge, 1, 0], 'points': [[edge, 0, 0], [edge, 1, 0]]}


def open_band():
    members = [{'faceId': 1, 'groups': [source_group('left', 'Side', 1), source_group('join', 'Side', 2)]},
               {'faceId': 2, 'groups': [source_group('join', 'Side', 3), source_group('right', 'Side', 4)]}]
    sides = [{'sourceFaceId': 1, 'logicalEdge': 'join'}, {'sourceFaceId': 2, 'logicalEdge': 'join'}]
    proof = {'passed': True, 'sourceFaceIds': [1, 2], 'positionToleranceMm': 1e-7,
             'normalToleranceRadians': 1e-7, 'maxPlaneDistanceMm': 0., 'maximumNormalAngleRadians': 0.,
             'referencePlane': {'origin': [0, 0, 0], 'normal': [0, 0, 1]}}
    for face in members:
        face['planeEvidence'] = {'isPlanar': True, 'plane': copy.deepcopy(proof['referencePlane']),
                                 'method': 'SyntheticAnalyticPlane', 'parameterDomainCovered': True, 'positiveWeights': True,
                                 'certificatePoints': [[0, 0, 0], [2, 0, 0], [2, 1, 0], [0, 1, 0]]}
    decision = {'kind': 'SideElimination', 'sourceFaceIds': [1, 2], 'accepted': True, 'sides': sides,
                'coplanarity': copy.deepcopy(proof),
                'tangency': {'passed': True, 'angleToleranceRadians': .001,
                             'endpoints': [{'sourceEndpoint': endpoint, 'passed': True,
                                            'angleRadians': 0., 'gapMm': 0.,
                                            'firstTangent': [1, 0, 0], 'secondTangent': [-1, 0, 0],
                                            'tangentDirectionSemantics': 'AwayFromJointIntoEachFace_OppositeMeansG1'}
                                           for endpoint in ['start', 'end']]}}
    constraints = []
    for face, key in [(members[0], 'left'), (members[1], 'right')]:
        group = next(group for group in face['groups'] if group['key'] == key)
        constraints.append(dict(group, sourceFaceId=face['faceId'], logicalEdge=key,
                                state='RetainedSideDirectionConstraint', candidateDirection=[0, 1, 0],
                                directionSemantics='UnorientedGeometricConstraint_NotMachinePose'))
    boundaries = [{'id': i, 'purpose': 'CutBoundary' if i % 2 else 'EndBoundary',
                   'roles': ['Skin'] if i % 2 else ['Side'], 'closed': False} for i in range(1, 5)]
    unit = {'mergeRule': 'CoplanarUnitsWithTwoSidedG1SideElimination', 'form': 'OpenBand',
            'coplanarity': proof, 'boundaries': boundaries, 'endChainIds': [2, 4], 'cutChainIds': [1, 3],
            'chainIds': [1, 2, 3, 4], 'eulerCharacteristic': 1,
            'internalSeams': [{'logicalEdges': ['F1/join', 'F2/join']}], 'axisConstraints': constraints}
    return {'mergeDecisions': [decision]}, unit, members


class RemovalReviewTests(unittest.TestCase):
    def test_open_band_retains_both_external_sides(self):
        audit_assembly_rules(*open_band())

    def test_non_coplanar_removal_is_rejected_even_when_accepted_flag_true(self):
        data, unit, members = open_band()
        data['mergeDecisions'][0]['coplanarity']['passed'] = False
        with self.assertRaises(AssertionError):
            audit_assembly_rules(data, unit, members)

    def test_only_one_tangent_endpoint_does_not_allow_side_removal(self):
        data, unit, members = open_band()
        data['mergeDecisions'][0]['tangency']['endpoints'][1]['passed'] = False
        with self.assertRaises(AssertionError):
            audit_assembly_rules(data, unit, members)

    def test_tangent_flag_does_not_hide_excessive_angle(self):
        data, unit, members = open_band()
        data['mergeDecisions'][0]['tangency']['endpoints'][0]['angleRadians'] = .01
        with self.assertRaises(AssertionError):
            audit_assembly_rules(data, unit, members)

    def test_folded_return_is_rejected_even_when_angle_field_says_zero(self):
        data, unit, members = open_band()
        data['mergeDecisions'][0]['tangency']['endpoints'][0]['secondTangent'] = [1, 0, 0]
        with self.assertRaises(AssertionError):
            audit_assembly_rules(data, unit, members)

    def test_external_side_direction_constraint_cannot_disappear(self):
        data, unit, members = open_band()
        unit['axisConstraints'].pop()
        with self.assertRaises(AssertionError):
            audit_assembly_rules(data, unit, members)

    def test_side_geometry_and_source_uses_are_preserved(self):
        data, unit, members = open_band()
        unit['axisConstraints'][0]['end'] = [99, 99, 99]
        with self.assertRaises(AssertionError):
            audit_assembly_rules(data, unit, members)

    def test_side_direction_constraint_keeps_source_chord_direction(self):
        data, unit, members = open_band()
        unit['axisConstraints'][0]['candidateDirection'] = [1, 0, 0]
        with self.assertRaises(AssertionError):
            audit_assembly_rules(data, unit, members)

    def test_whole_component_plane_is_required(self):
        data, unit, members = open_band()
        unit['coplanarity']['maxPlaneDistanceMm'] = 2e-7
        with self.assertRaises(AssertionError):
            audit_assembly_rules(data, unit, members)

    def test_native_certificates_are_checked_independently_of_passed_flag(self):
        data, unit, members = open_band()
        members[1]['planeEvidence']['certificatePoints'][0][2] = 2e-7
        with self.assertRaises(AssertionError):
            audit_assembly_rules(data, unit, members)

    def test_single_closed_skin_loop_is_valid_unpaired_region(self):
        data, unit, members = open_band()
        for face in members:
            face['groups'] = [group for group in face['groups'] if group['key'] == 'join']
        unit.update(form='UnpairedRegion', boundaries=[{'id': 1, 'purpose': 'CutBoundary',
                                                       'roles': ['Skin'], 'closed': True}],
                    chainIds=[1], cutChainIds=[1], endChainIds=[], eulerCharacteristic=1, axisConstraints=[])
        audit_assembly_rules(data, unit, members)


if __name__ == '__main__':
    unittest.main(verbosity=2)
