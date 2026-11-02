"""Provenance binding and conservative source-coedge ownership application."""
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from source_skin_membership import apply_context, validate_context, SourceSkinMembershipError


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def match(edge=22, source='34', face='13', deviation=.003098424):
    return {'cutStepEdgeId': edge, 'sourceEdgeId': source, 'sourceFaceId': face,
            'method': 'SharedSourceCoedge', 'sourceSurfaceType': 'Cylinder',
            'certifiedSkinSupport': {'passed': True,
                'method': 'AnalyticSupportAndFiniteProfilePcurve',
                'profileStepEdgeIds': [101], 'supportToleranceMm': 1e-7},
            'maxSourceCorrespondenceMm': 9e-10, 'maxSkinDeviationMm': deviation,
            'sourceEdgeToleranceMm': .002835117, 'sourceFaceToleranceMm': 1e-7,
            'correspondenceCheck': {'samplesPerDirection': 257,
                'endpointMaxDistanceMm': 8e-10, 'continuousCertificate': False},
            'deviationCheck': {'sampleCount': 257, 'continuousCertificate': False}}


def sewn_match():
    result = match(edge=2, source='51', face='7', deviation=.00313586)
    result.update(method='SewnSourceCoedge', sourceCutEdgeId='14',
                  sourceSeamGapMm=.0031358649116721934,
                  maxSourceCorrespondenceMm=4e-13)
    result['correspondenceCheck'].update(target='OriginalSourceCutEdge',
        targetEdgeId='14', representation='OriginalSourceCurve3DThenSewnTopology')
    result['sewingEvidence'] = {
        'passed': True, 'scope': 'LocalCompleteCoedgeAdjacency',
        'historyMethod': 'Copy.ModifiedShape_then_Sewing.ModifiedSubShape_for_all_original_edges',
        'sourceCutEdgeId': '14', 'sourceSkinEdgeId': '51', 'sourceSkinFaceId': '7',
        'sourceCutRecoveredPieceIds': [8], 'sourceSkinRecoveredPieceIds': [8, 9],
        'commonRecoveredPieceIds': [8],
        'pieceSourceOwners': [{'pieceId': 8, 'sourceEdgeIds': ['14', '51'],
                              'neighborFaceIds': [4, 7], 'neighborFaceCount': 2}],
        'entireSourceCutEdgeRecovered': True, 'uniqueSourceEdgePair': True,
        'sewingToleranceMm': .0031390007765838652,
        'sourceSeamGapMm': .0031358649116721934,
        'sourceFaceCount': 34, 'restoredFaceCount': 34, 'deletedFaces': 0,
        'multipleEdges': 0, 'restoredTopologyValid': True,
        'globalClosedShells': 0, 'globalFreeEdges': 120,
        'localTolerancesMode': False, 'derivedGeometryPublished': False}
    return result


def group(edge=22, kind='Unresolved', distance=.003097795):
    return {'groupId': 1, 'kind': kind,
            'reason': 'outside-section-material' if kind == 'Unresolved' else '',
            'sourceUses': [{'edgeId': edge, 'reversed': False, 'curveType': 6}],
            'maxSkinDistance': distance, 'minSkinDistance': 0,
            'allOnSkin': distance <= .001, 'startOnSkin': True, 'endOnSkin': True,
            'polyline': [[1, 2, 3], [1, 4, 5]], 'roleSegments': [],
            'bodyStates': {'inside': 0, 'on': 1, 'outside': 46, 'unknown': 0}}


def boundary(groups=None):
    return {'version': 4, 'faces': [{'faceId': 4,
            'wires': [{'wireId': 1, 'groups': groups or [group()]}]}],
            'profile': [[[0, 2, 3], [0, 4, 5]]]}


class SourceSkinMembershipTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def context(self, matches=None):
        ref = {'sourceName': '016.STEP'}
        for stem, path_key, hash_key in [
            ('model.step', 'classifyPath', 'classifySha256'),
            ('source.icaxbrep', 'sourceNeutralPath', 'sourceNeutralSha256'),
            ('producer.exe', 'producerPath', 'producerSha256')]:
            path = self.root / stem
            path.write_bytes(('fixture ' + stem).encode())
            ref[path_key], ref[hash_key] = str(path), digest(path)
        data = {'schema': 1, 'passed': True,
                'skinEvidence': {'matches': [match()] if matches is None else matches},
                'unmatchedCutStepEdgeIds': []}
        for key in ('classifySha256', 'sourceNeutralSha256', 'producerSha256'):
            data[key] = ref[key]
        path = self.root / 'evidence.json'
        path.write_text(json.dumps(data), 'utf-8')
        ref['evidencePath'], ref['evidenceSha256'] = str(path), digest(path)
        return ref

    def test_approximate_skin_keeps_geometry_and_original_measurements(self):
        data = boundary()
        original = deepcopy(data['faces'][0]['wires'][0]['groups'][0])
        ref = self.context()
        provenance = apply_context(data, ref)
        actual = data['faces'][0]['wires'][0]['groups'][0]
        self.assertEqual(actual['kind'], 'Skin')
        self.assertEqual(actual['reason'], '')
        for key, value in original.items():
            if key not in ('kind', 'reason'):
                self.assertEqual(actual[key], value)
        evidence = actual['skinMembership']
        self.assertEqual(evidence['previousKind'], 'Unresolved')
        self.assertEqual(evidence['previousReason'], 'outside-section-material')
        self.assertEqual(evidence['accuracy'], 'Approximate')
        self.assertEqual(evidence['maxSkinDistanceMm'], .003098424)
        self.assertFalse(actual['allOnSkin'])
        self.assertFalse(evidence['continuousCertificate'])
        self.assertEqual(provenance['approximateGroupCount'], 1)
        self.assertEqual(data['skinMembershipContext'], provenance)
        self.assertEqual(validate_context(data, ref), provenance)

    def test_dense_source_peak_is_not_hidden_by_coarse_group_samples(self):
        data = boundary([group(distance=.0009, kind='Skin')])
        ref = self.context([match(deviation=.0012)])
        apply_context(data, ref)
        actual = data['faces'][0]['wires'][0]['groups'][0]
        self.assertTrue(actual['allOnSkin'])
        self.assertEqual(actual['maxSkinDistance'], .0009)
        self.assertEqual(actual['skinMembership']['accuracy'], 'Approximate')
        self.assertEqual(actual['skinMembership']['maxSkinDistanceMm'], .0012)

    def test_missing_one_source_use_leaves_entire_group_unchanged(self):
        g = group()
        g['sourceUses'].append({'edgeId': 23, 'reversed': True, 'curveType': 6})
        data = boundary([g])
        before = deepcopy(g)
        ref = self.context()
        result = apply_context(data, ref)
        self.assertEqual(data['faces'][0]['wires'][0]['groups'][0], before)
        self.assertEqual(result['appliedGroupCount'], 0)
        self.assertEqual(result['rejectedGroups'][0]['reason'], 'source-use-has-no-certified-skin-match')
        validate_context(data, ref)

    def test_boundary_input_hash_cannot_borrow_another_models_context(self):
        data = boundary()
        data['classifySha256'] = '0'*64
        before = deepcopy(data)
        with self.assertRaises(SourceSkinMembershipError):
            apply_context(data, self.context())
        self.assertEqual(data, before)

    def test_true_side_without_complete_source_skin_match_stays_side(self):
        side = group(edge=50, kind='Side', distance=2.9)
        data = boundary([side])
        ref = self.context()
        apply_context(data, ref)
        self.assertEqual(data['faces'][0]['wires'][0]['groups'][0], side)
        self.assertEqual(data['skinMembershipContext']['roleChangedGroupCount'], 0)

    def test_role_children_are_applied_individually_without_changing_parent(self):
        children = [group(1, 'Skin', 0), group(2, 'Side', 2.9), group(3, 'Skin', 0)]
        for index, child in enumerate(children, 1):
            child['segmentId'] = index
        parent = group()
        parent.update(reason='mixed-skin-contact', roleSegments=children,
                      sourceUses=[use for child in children for use in child['sourceUses']])
        data = boundary([parent])
        ref = self.context([match(1, deviation=0), match(3, source='36', deviation=0)])
        apply_context(data, ref)
        actual = data['faces'][0]['wires'][0]['groups'][0]
        self.assertEqual(actual['kind'], 'Unresolved')
        self.assertEqual(actual['reason'], 'mixed-skin-contact')
        self.assertNotIn('skinMembership', actual)
        self.assertEqual([c['kind'] for c in actual['roleSegments']], ['Skin', 'Side', 'Skin'])
        self.assertNotIn('skinMembership', actual['roleSegments'][1])
        validate_context(data, ref)

    def test_all_four_input_hashes_are_required(self):
        for key in ('evidenceSha256', 'classifySha256', 'sourceNeutralSha256', 'producerSha256'):
            with self.subTest(key=key):
                ref = self.context()
                ref[key] = '0' * 64
                data = boundary()
                before = deepcopy(data)
                with self.assertRaises(SourceSkinMembershipError):
                    apply_context(data, ref)
                self.assertEqual(data, before)

    def test_evidence_cannot_be_rebound_to_another_input(self):
        ref = self.context()
        path = Path(ref['evidencePath'])
        evidence = json.loads(path.read_text('utf-8'))
        evidence['classifySha256'] = '0' * 64
        path.write_text(json.dumps(evidence), 'utf-8')
        ref['evidenceSha256'] = digest(path)
        with self.assertRaisesRegex(SourceSkinMembershipError, 'evidence input differs'):
            apply_context(boundary(), ref)

    def test_changed_producer_after_application_rejects_validation(self):
        ref = self.context()
        data = boundary()
        apply_context(data, ref)
        Path(ref['producerPath']).write_bytes(b'a different producer')
        with self.assertRaisesRegex(SourceSkinMembershipError, 'hash changed'):
            validate_context(data, ref)

    def test_bad_or_inconsistent_native_match_does_not_override_role(self):
        candidates = []
        unproved = match()
        unproved['certifiedSkinSupport']['passed'] = False
        candidates.append([unproved])
        distant = match()
        distant['maxSourceCorrespondenceMm'] = 2e-6
        candidates.append([distant])
        candidates.append([match(), match(deviation=.006)])
        for matches in candidates:
            with self.subTest(matches=matches):
                ref = self.context(matches)
                data = boundary()
                apply_context(data, ref)
                g = data['faces'][0]['wires'][0]['groups'][0]
                self.assertEqual(g['kind'], 'Unresolved')
                self.assertNotIn('skinMembership', g)

    def test_partial_source_use_cannot_borrow_whole_edge_certificate(self):
        g = group()
        g['sourceUses'][0]['parameterRange'] = [0, .5]
        data = boundary([g])
        result = apply_context(data, self.context())
        self.assertEqual(result['appliedGroupCount'], 0)
        self.assertEqual(result['rejectedGroups'][0]['reason'], 'partial-source-use-not-certified')

    def test_application_is_idempotent_and_tampered_membership_is_rejected(self):
        ref = self.context()
        data = boundary()
        first = apply_context(data, ref)
        saved = deepcopy(data)
        self.assertEqual(apply_context(data, ref), first)
        self.assertEqual(data, saved)
        data['faces'][0]['wires'][0]['groups'][0]['skinMembership']['maxSkinDistanceMm'] = 0
        with self.assertRaisesRegex(SourceSkinMembershipError, 'replay differs'):
            validate_context(data, ref)

    def test_complete_sewn_cut_can_be_a_subset_of_the_skin_coedge(self):
        raw = sewn_match()
        data = boundary([group(edge=2, distance=.0031)])
        original = deepcopy(data['faces'][0]['wires'][0]['groups'][0])
        ref = self.context([raw])
        provenance = apply_context(data, ref)
        actual = data['faces'][0]['wires'][0]['groups'][0]
        self.assertEqual(actual['kind'], 'Skin')
        self.assertEqual(actual['maxSkinDistance'], original['maxSkinDistance'])
        self.assertFalse(actual['allOnSkin'])
        membership = actual['skinMembership']
        self.assertEqual(membership['accuracy'], 'Approximate')
        self.assertEqual(membership['maxSkinDistanceMm'], .00313586)
        self.assertEqual(membership['maxSourceSeamGapMm'], raw['sourceSeamGapMm'])
        self.assertEqual(membership['sourceMatches'][0], raw)
        self.assertLess(membership['sourceMatches'][0]['maxSourceCorrespondenceMm'], 1e-6)
        self.assertNotEqual(membership['maxSourceSeamGapMm'],
                            membership['sourceMatches'][0]['maxSourceCorrespondenceMm'])
        self.assertEqual(validate_context(data, ref), provenance)

    def test_sewn_adjacency_is_explicitly_approximate_even_if_profile_samples_fit(self):
        raw = sewn_match()
        raw['maxSkinDeviationMm'] = 1e-8
        data = boundary([group(edge=2, kind='Skin', distance=1e-8)])
        apply_context(data, self.context([raw]))
        membership = data['faces'][0]['wires'][0]['groups'][0]['skinMembership']
        self.assertEqual(membership['accuracy'], 'Approximate')
        self.assertEqual(membership['maxSkinDistanceMm'], 1e-8)
        self.assertEqual(membership['maxSourceSeamGapMm'], raw['sourceSeamGapMm'])

    def test_global_shell_closure_is_recorded_without_becoming_a_local_gate(self):
        for closed, free in [(0, 120), (2, 0)]:
            with self.subTest(closed=closed, free=free):
                raw = sewn_match()
                raw['sewingEvidence'].update(globalClosedShells=closed, globalFreeEdges=free)
                data = boundary([group(edge=2)])
                ref = self.context([raw])
                result = apply_context(data, ref)
                self.assertEqual(result['appliedGroupCount'], 1)
                saved = data['faces'][0]['wires'][0]['groups'][0]['skinMembership']['sourceMatches'][0]
                self.assertEqual(saved['sewingEvidence']['globalClosedShells'], closed)
                self.assertEqual(saved['sewingEvidence']['globalFreeEdges'], free)
                validate_context(data, ref)

    def test_incomplete_or_ambiguous_sewing_evidence_does_not_override(self):
        mutations = [
            ('partial flag', lambda m: m['sewingEvidence'].update(entireSourceCutEdgeRecovered=False)),
            ('missing common piece', lambda m: m['sewingEvidence'].update(
                sourceCutRecoveredPieceIds=[8, 10], sourceSkinRecoveredPieceIds=[8, 9, 10])),
            ('common contains extra skin piece', lambda m: m['sewingEvidence'].update(commonRecoveredPieceIds=[8, 9])),
            ('cut not covered by skin', lambda m: m['sewingEvidence'].update(sourceSkinRecoveredPieceIds=[9])),
            ('duplicate recovered cut piece', lambda m: m['sewingEvidence'].update(sourceCutRecoveredPieceIds=[8, 8])),
            ('missing piece owners', lambda m: m['sewingEvidence'].update(pieceSourceOwners=[])),
            ('third source owner', lambda m: m['sewingEvidence']['pieceSourceOwners'][0].update(sourceEdgeIds=['14', '51', '99'])),
            ('wrong source owner', lambda m: m['sewingEvidence']['pieceSourceOwners'][0].update(sourceEdgeIds=['14', '52'])),
            ('third neighbor face', lambda m: m['sewingEvidence']['pieceSourceOwners'][0].update(neighborFaceIds=[4, 7, 9], neighborFaceCount=3)),
            ('lying neighbor count', lambda m: m['sewingEvidence']['pieceSourceOwners'][0].update(neighborFaceIds=[4])),
            ('wrong piece identity', lambda m: m['sewingEvidence']['pieceSourceOwners'][0].update(pieceId=9)),
            ('topology invalid', lambda m: m['sewingEvidence'].update(restoredTopologyValid=False)),
            ('derived geometry published', lambda m: m['sewingEvidence'].update(derivedGeometryPublished=True)),
            ('face lost', lambda m: m['sewingEvidence'].update(restoredFaceCount=33)),
            ('deleted face', lambda m: m['sewingEvidence'].update(deletedFaces=1)),
            ('multiple edge', lambda m: m['sewingEvidence'].update(multipleEdges=1)),
            ('local tolerance mode', lambda m: m['sewingEvidence'].update(localTolerancesMode=True)),
            ('source pair not unique', lambda m: m['sewingEvidence'].update(uniqueSourceEdgePair=False)),
            ('no certificate', lambda m: m['sewingEvidence'].update(passed=False)),
            ('wrong scope', lambda m: m['sewingEvidence'].update(scope='GlobalClosedShell')),
            ('untracked history', lambda m: m['sewingEvidence'].update(historyMethod='unknown')),
        ]
        for name, mutate in mutations:
            with self.subTest(name=name):
                raw = sewn_match()
                mutate(raw)
                data = boundary([group(edge=2)])
                before = deepcopy(data['faces'][0]['wires'][0]['groups'][0])
                ref = self.context([raw])
                result = apply_context(data, ref)
                self.assertEqual(result['appliedGroupCount'], 0)
                self.assertEqual(data['faces'][0]['wires'][0]['groups'][0], before)
                validate_context(data, ref)

    def test_sewn_source_and_correspondence_identities_must_agree(self):
        mutations = [
            ('sewing disguised as shared coedge', lambda m: m.update(method='SharedSourceCoedge')),
            ('sewing disguised as coincident coedge', lambda m: m.update(method='CoincidentSourceCoedge')),
            ('source cut', lambda m: m.update(sourceCutEdgeId='15')),
            ('source skin', lambda m: m['sewingEvidence'].update(sourceSkinEdgeId='52')),
            ('source skin face', lambda m: m['sewingEvidence'].update(sourceSkinFaceId='8')),
            ('correspondence target edge', lambda m: m['correspondenceCheck'].update(targetEdgeId='51')),
            ('correspondence target representation', lambda m: m['correspondenceCheck'].update(representation='RebuiltFromPcurve')),
            ('correspondence target kind', lambda m: m['correspondenceCheck'].update(target='SourceSkinEdge')),
        ]
        for name, mutate in mutations:
            with self.subTest(name=name):
                raw = sewn_match()
                mutate(raw)
                data = boundary([group(edge=2)])
                result = apply_context(data, self.context([raw]))
                self.assertEqual(result['appliedGroupCount'], 0)
                self.assertNotIn('skinMembership', data['faces'][0]['wires'][0]['groups'][0])

    def test_seam_gap_cannot_be_hidden_in_strict_cut_correspondence(self):
        mutations = [
            ('seam as correspondence', lambda m: m.update(maxSourceCorrespondenceMm=m['sourceSeamGapMm'])),
            ('seam as endpoint correspondence', lambda m: m['correspondenceCheck'].update(endpointMaxDistanceMm=m['sourceSeamGapMm'])),
            ('excess seam gap', lambda m: m['sewingEvidence'].update(sewingToleranceMm=.001)),
            ('nonpositive sewing tolerance', lambda m: m['sewingEvidence'].update(sewingToleranceMm=0)),
            ('different seam measurements', lambda m: m.update(sourceSeamGapMm=.003)),
            ('nonfinite seam measurement', lambda m: m.update(sourceSeamGapMm=float('nan'))),
        ]
        for name, mutate in mutations:
            with self.subTest(name=name):
                raw = sewn_match()
                mutate(raw)
                data = boundary([group(edge=2)])
                result = apply_context(data, self.context([raw]))
                self.assertEqual(result['appliedGroupCount'], 0)
                self.assertEqual(result['correspondenceToleranceMm'], 1e-6)
                self.assertEqual(result['skinToleranceMm'], .001)


if __name__ == '__main__':
    unittest.main(verbosity=2)
