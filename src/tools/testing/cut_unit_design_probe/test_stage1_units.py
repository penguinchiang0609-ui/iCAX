"""Boundary organization invariants, using current F2/F5 and targeted fixtures."""
import copy, math, unittest
from collections import Counter
from cut_unit_stage1 import CutUnitBuilder, analyze_case, reduced
from run_stage1_units import DEST, read

def source(case,face):
    return next(f for f in read(DEST/'boundaries'/f'{case:03d}.json')['faces'] if f['faceId']==face)

def signature(result):
    return sorted((c['purpose'],tuple(sorted(e['edgeId'] for e in c['sourceUses']))) for c in result['chains'])


def source_membership(group):
    return {'method':'CertifiedSourceCoedge','accuracy':'Approximate','maxSkinDistanceMm':.006271732828,
        'sourceMatches':[{'cutStepEdgeId':group['sourceUses'][0]['edgeId'],'sourceEdgeId':'503','sourceFaceId':'30',
            'method':'CoincidentSourceCoedge','sourceSurfaceType':'Cylinder',
            'maxSourceCorrespondenceMm':2.3e-12,'maxSkinDeviationMm':.006271732828,
            'sourceEdgeToleranceMm':.02,'sourceFaceToleranceMm':.021,
            'certifiedSkinSupport':{'passed':True,'method':'AnalyticSupportAndFiniteProfilePcurve',
                'profileStepEdgeIds':[101],'supportToleranceMm':1e-7},
            'correspondenceCheck':{'samplesPerDirection':257,'endpointMaxDistanceMm':1e-12,'continuousCertificate':False},
            'deviationCheck':{'sampleCount':257,'continuousCertificate':False}}]}

class OrganizationTests(unittest.TestCase):
    def setUp(self):self.builder=CutUnitBuilder()

    def test_f5_cyclic_start_does_not_change_unit_or_membership(self):
        face=source(18,5);expected=self.builder.build(face);groups=face['wires'][0]['groups']
        self.assertEqual(expected['units'][0]['form'],'OpenBand')
        self.assertEqual(len(expected['chains']),4)
        for start in range(len(groups)):
            rotated=copy.deepcopy(face);rotated['wires'][0]['groups']=groups[start:]+groups[:start]
            result=self.builder.build(rotated)
            self.assertEqual(signature(result),signature(expected))
            self.assertEqual(result['units'][0]['form'],'OpenBand')
            self.assertEqual(result['newBoundaryCount'],0)

    def test_f2_preserves_distinct_primitives_without_splitting_face(self):
        result=self.builder.build(source(4,2))
        self.assertEqual((result['rawEdgeUses'],len(result['groups']),len(result['chains']),len(result['units'])),(44,16,1,1))
        self.assertEqual(sum(g['geometry']=='Line' for g in result['groups']),12)
        self.assertEqual(result['roles'],{'Skin':16})
        self.assertEqual(len(result['chains'][0]['logicalEdges']),16)

    def test_source_edge_use_partition_is_lossless(self):
        for case in [4,18,43]:
            for face in read(DEST/'boundaries'/f'{case:03d}.json')['faces']:
                result=self.builder.build(face)
                expected=Counter((w['wireId'],e['edgeId'],e['reversed']) for w in face['wires'] for g in w['groups'] if not g['event'] and not g['seam'] for e in g['sourceUses'])
                actual=Counter((c['wire'],e['edgeId'],e['reversed']) for c in result['chains'] for e in c['sourceUses'])
                self.assertEqual(expected,actual)

    def test_curved_side_is_not_rejected_or_straightened(self):
        original=source(43,1);result=self.builder.build(original)
        sides=[g for g in result['groups'] if g['role']=='Side']
        self.assertTrue(any(not g['straight'] for g in sides))
        self.assertEqual(result['units'][0]['form'],'OpenBand')
        self.assertEqual(result['units'][0]['geometryPolicy'],'PreserveExactSourceSurfaceAndTrims')

    def test_world_rotation_translation_preserves_chains(self):
        original=source(18,5);moved=copy.deepcopy(original)
        def transform(p):return [-p[1]+11,p[0]-20,p[2]+17]
        for w in moved['wires']:
            for g in w['groups']:
                g['start']=transform(g['start']);g['end']=transform(g['end']);g['polyline']=[transform(p) for p in g['polyline']]
        self.assertEqual(signature(self.builder.build(original)),signature(self.builder.build(moved)))

    def test_gap_is_not_published_as_success(self):
        f=copy.deepcopy(source(18,5));f['wires'][0]['groups'][0]['end'][0]+=.1
        result=self.builder.build(f)
        self.assertEqual(result['reason'],'boundary-chain-gap');self.assertFalse(result['units'])

    def test_more_than_two_ends_remains_pending(self):
        f=copy.deepcopy(source(18,5));f['wires'][0]['groups'][1]['kind']='Side'
        result=self.builder.build(f)
        self.assertEqual(result['reason'],'boundary-organization-ambiguous');self.assertFalse(result['units'])

    def test_internal_termination_does_not_imply_a_process(self):
        f=copy.deepcopy(source(18,5));f['wires'][0]['groups'][0]['kind']='Internal'
        result=self.builder.build(f)
        self.assertEqual(len(result['terminationConstraints']),1)
        self.assertEqual(result['terminationConstraints'][0]['state'],'RequiresAdjacentMaterialInterpretation')
        self.assertEqual(result['units'][0]['machiningState'],'NotPlanned')

    def test_two_closed_rings_preserve_interior_constraint(self):
        f=copy.deepcopy(source(4,2));inner=copy.deepcopy(f['wires'][0]);inner['wireId']=2;inner['outer']=False
        for g in inner['groups']:
            g['kind']='Internal'
            for e in g['sourceUses']:e['edgeId']+=1000
        f['wires'].append(inner);result=self.builder.build(f)
        self.assertEqual(result['units'][0]['form'],'ClosedBand')
        self.assertEqual(len(result['chains']),2)
        self.assertTrue(all(c['closed'] for c in result['chains']))
        self.assertEqual(len(result['terminationConstraints']),16)

    def test_all_internal_face_is_retained_but_not_marked(self):
        f=copy.deepcopy(source(4,2))
        for g in f['wires'][0]['groups']:g['kind']='Internal'
        result=self.builder.build(f)
        self.assertEqual(result['reason'],'no-skin-anchor');self.assertFalse(result['units'])
        self.assertEqual(result['coverageState'],'WholeFaceRetained')

    def test_collinear_geometry_keeps_parent_when_roles_differ(self):
        face=copy.deepcopy(source(24,10));parent=face['wires'][0]['groups'][0]
        parent.pop('roleSegments',None)
        self.assertEqual(len(parent['sourceUses']),3)
        children=[]
        for index,role in enumerate(['Skin','Side','Skin']):
            child=copy.deepcopy(parent);child['segmentId']=index+1;child['kind']=role;child['reason']=''
            child['sourceUses']=[parent['sourceUses'][index]]
            child['polyline']=parent['polyline'][index*49:(index+1)*49]
            child['start']=child['polyline'][0];child['end']=child['polyline'][-1]
            children.append(child)
        parent['roleSegments']=children
        result=self.builder.build(face)
        self.assertEqual(result['logicalEdgeCount'],4)
        self.assertEqual(result['boundarySegmentCount'],6)
        self.assertEqual(result['roles'],{'Skin':5,'Side':1})
        self.assertEqual(result['geometricGroups'][0]['sourceUses'],parent['sourceUses'])
        self.assertEqual(result['geometricGroups'][0]['roleSegmentKeys'],['W1:G1:S1','W1:G1:S2','W1:G1:S3'])
        parts=[g for g in result['groups'] if g.get('geometryGroupKey')=='W1:G1']
        self.assertEqual([g['role'] for g in parts],['Skin','Side','Skin'])
        self.assertEqual([use for g in parts for use in g['sourceUses']],parent['sourceUses'])
        self.assertEqual(result['newBoundaryCount'],0)
        self.assertEqual(result['coverageState'],'WholeFaceRetained')
        self.assertNotIn('boundary-unresolved',result['issues'])
        self.assertEqual(len(result['axisConstraints']),1)
        self.assertEqual(result['axisConstraints'][0]['logicalEdge'],'W1:G1:S2')

    def test_unresolved_single_topo_edge_is_not_invented_into_role_segments(self):
        face=copy.deepcopy(source(24,10));parent=face['wires'][0]['groups'][0]
        parent.pop('roleSegments',None)
        parent['sourceUses']=parent['sourceUses'][:1];parent['polyline']=parent['polyline'][:49]
        parent['end']=parent['polyline'][-1]
        face['wires'][0]['rawEdgeUses']-=2;face['wires'][0]['expectedEdgeUses']-=2
        result=self.builder.build(face)
        self.assertEqual(result['logicalEdgeCount'],result['boundarySegmentCount'])
        self.assertTrue(any(g['role']=='Unresolved' for g in result['groups']))
        self.assertIn('boundary-unresolved',result['issues'])

    def test_source_skin_evidence_preserves_approximation_and_geometry(self):
        data=copy.deepcopy(read(DEST/'boundaries/004.json'));face=source(4,2)
        data['faces']=[copy.deepcopy(face)]
        group=next(g for g in data['faces'][0]['wires'][0]['groups'] if not all(e['lineLike'] for e in g['sourceUses']))
        group['skinMembership']=source_membership(group)
        group['allOnSkin']=False;group['maxSkinDistance']=.006271732828
        context={'policy':'CertifiedSourceCoedge','geometricSkinToleranceMm':.001,'sourceFile':'synthetic-source.igs'}
        data['skinMembershipContext']=context
        before=copy.deepcopy(data)
        result=analyze_case(data)
        actual=next(g for g in result['faces'][0]['groups'] if g['key']==f"W1:G{group['groupId']}")
        self.assertEqual(result['skinMembershipContext'],context)
        self.assertEqual(actual['skinMembership'],group['skinMembership'])
        self.assertEqual(actual['role'],'Skin')
        self.assertEqual(actual['skinDistanceRangeMm'][1],.006271732828)
        self.assertGreater(actual['skinDistanceRangeMm'][1],result['tolerances']['positionMm'])
        self.assertEqual(actual['points'],reduced(group['polyline']))
        self.assertEqual(actual['sourceUses'],group['sourceUses'])
        self.assertEqual(data,before)

    def test_membership_transport_does_not_change_native_role(self):
        face=copy.deepcopy(source(4,2))
        group=next(g for g in face['wires'][0]['groups'] if not all(e['lineLike'] for e in g['sourceUses']))
        group['kind']='Unresolved';group['reason']='outside-section-material'
        group['skinMembership']=source_membership(group)
        result=self.builder.build(face)
        actual=next(g for g in result['groups'] if g['key']==f"W1:G{group['groupId']}")
        self.assertEqual(actual['skinMembership'],group['skinMembership'])
        self.assertEqual(actual['role'],'Unresolved')
        self.assertEqual(result['reason'],'boundary-unresolved')
        self.assertFalse(result['units'])

if __name__=='__main__':unittest.main(verbosity=2)
