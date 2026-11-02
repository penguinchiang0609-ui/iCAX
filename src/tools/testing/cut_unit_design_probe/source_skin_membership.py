"""Apply source-coedge skin ownership without replacing measured geometry.

The native evidence producer proves skin support and correspondence to complete
Classify edges. This module binds that evidence to its input files, changes only
leaf boundary roles, and keeps approximation measurements explicit. It never
writes a BREP, changes a curve, or increases the fixed profile-distance tolerance.
"""
from copy import deepcopy
import hashlib
import json
import math
from pathlib import Path


CORRESPONDENCE_TOLERANCE_MM = 1e-6
SKIN_TOLERANCE_MM = .001
SUPPORT_TOLERANCE_MM = 1e-7
METHOD = 'CertifiedSourceCoedge'
_ROLES = {'Skin', 'Side', 'Internal', 'Unresolved'}
_SOURCE_METHODS = {'SharedSourceCoedge', 'CoincidentSourceCoedge', 'SewnSourceCoedge'}
_SEWING_HISTORY_METHOD = 'Copy.ModifiedShape_then_Sewing.ModifiedSubShape_for_all_original_edges'
_HASH_FILES = (('evidencePath', 'evidenceSha256'),
               ('classifyPath', 'classifySha256'),
               ('sourceNeutralPath', 'sourceNeutralSha256'),
               ('producerPath', 'producerSha256'))


class SourceSkinMembershipError(ValueError):
    """The bound evidence or the replayed application did not validate."""


def _require(condition, message):
    if not condition:
        raise SourceSkinMembershipError(message)


def _number(value, name):
    _require(isinstance(value, (int, float)) and not isinstance(value, bool)
             and math.isfinite(value) and value >= 0, 'invalid ' + name)
    return float(value)


def _identifier(value, name):
    _require(not isinstance(value, bool) and isinstance(value, (int, str)),
             'invalid ' + name)
    if isinstance(value, str):
        _require(value.isascii() and value.isdecimal(), 'invalid ' + name)
    _require(int(value) > 0, 'invalid ' + name)
    return str(int(value))


def _count(value, name):
    _require(isinstance(value, int) and not isinstance(value, bool) and value >= 0,
             'invalid ' + name)
    return value


def _unique_identifiers(values, name):
    _require(isinstance(values, list) and bool(values), 'missing ' + name)
    result = [_identifier(value, name) for value in values]
    _require(len(result) == len(set(result)), 'duplicate ' + name)
    return set(result)


def _sewing_issue(match):
    """Require complete local recovered adjacency, independently of seam size.

    The strict cut-to-original-cut correspondence remains checked separately.
    The nonzero cut-to-skin seam gap is never used as that correspondence or
    as a replacement for the fixed geometric profile tolerance.
    """
    sewing = match.get('sewingEvidence')
    if not isinstance(sewing, dict) or sewing.get('passed') is not True:
        return 'sewing-adjacency-not-certified'
    if sewing.get('scope') != 'LocalCompleteCoedgeAdjacency':
        return 'unsupported-sewing-scope'
    if sewing.get('historyMethod') != _SEWING_HISTORY_METHOD:
        return 'unsupported-sewing-history'
    source_cut = _identifier(match.get('sourceCutEdgeId'), 'sourceCutEdgeId')
    source_skin = _identifier(match.get('sourceEdgeId'), 'sourceEdgeId')
    source_face = _identifier(match.get('sourceFaceId'), 'sourceFaceId')
    if source_cut == source_skin:
        return 'sewing-source-edges-not-distinct'
    if (_identifier(sewing.get('sourceCutEdgeId'), 'sewing sourceCutEdgeId') != source_cut
            or _identifier(sewing.get('sourceSkinEdgeId'), 'sewing sourceSkinEdgeId') != source_skin
            or _identifier(sewing.get('sourceSkinFaceId'), 'sewing sourceSkinFaceId') != source_face):
        return 'sewing-source-identity-differs'
    correspondence = match.get('correspondenceCheck')
    if (not isinstance(correspondence, dict)
            or correspondence.get('target') != 'OriginalSourceCutEdge'
            or _identifier(correspondence.get('targetEdgeId'), 'correspondence targetEdgeId') != source_cut
            or correspondence.get('representation') != 'OriginalSourceCurve3DThenSewnTopology'):
        return 'sewing-cut-correspondence-target-differs'
    if sewing.get('entireSourceCutEdgeRecovered') is not True:
        return 'source-cut-edge-not-completely-recovered'
    if sewing.get('uniqueSourceEdgePair') is not True:
        return 'sewing-source-edge-pair-not-unique'
    cut_pieces = _unique_identifiers(sewing.get('sourceCutRecoveredPieceIds'), 'sourceCutRecoveredPieceIds')
    skin_pieces = _unique_identifiers(sewing.get('sourceSkinRecoveredPieceIds'), 'sourceSkinRecoveredPieceIds')
    common_pieces = _unique_identifiers(sewing.get('commonRecoveredPieceIds'), 'commonRecoveredPieceIds')
    # The skin coedge may extend beyond this cut coedge. Its extra pieces are
    # allowed, but every complete cut piece must be covered by the same skin.
    if cut_pieces != common_pieces or not cut_pieces <= skin_pieces:
        return 'sewing-common-pieces-do-not-cover-complete-cut-edge'
    owners = sewing.get('pieceSourceOwners')
    if not isinstance(owners, list) or len(owners) != len(common_pieces):
        return 'sewing-piece-owner-coverage-incomplete'
    seen = set()
    for owner in owners:
        if not isinstance(owner, dict):
            return 'invalid-sewing-piece-owner'
        piece = _identifier(owner.get('pieceId'), 'pieceId')
        if piece not in common_pieces or piece in seen:
            return 'sewing-piece-owner-identity-differs'
        seen.add(piece)
        if _unique_identifiers(owner.get('sourceEdgeIds'), 'piece sourceEdgeIds') != {source_cut, source_skin}:
            return 'sewing-piece-source-owners-not-unique-pair'
        neighbors = _unique_identifiers(owner.get('neighborFaceIds'), 'neighborFaceIds')
        if len(neighbors) != 2 or _count(owner.get('neighborFaceCount'), 'neighborFaceCount') != 2:
            return 'sewing-piece-does-not-have-two-neighbor-faces'
    if seen != common_pieces:
        return 'sewing-piece-owner-coverage-incomplete'
    tolerance = _number(sewing.get('sewingToleranceMm'), 'sewingToleranceMm')
    gap = _number(sewing.get('sourceSeamGapMm'), 'sewing sourceSeamGapMm')
    if tolerance <= 0 or gap > tolerance:
        return 'source-seam-gap-exceeds-sewing-tolerance'
    if _number(match.get('sourceSeamGapMm'), 'sourceSeamGapMm') != gap:
        return 'sewing-source-seam-measurement-differs'
    source_faces = _count(sewing.get('sourceFaceCount'), 'sourceFaceCount')
    restored_faces = _count(sewing.get('restoredFaceCount'), 'restoredFaceCount')
    if source_faces <= 0 or restored_faces != source_faces:
        return 'sewing-source-face-coverage-changed'
    if (_count(sewing.get('deletedFaces'), 'deletedFaces') != 0
            or _count(sewing.get('multipleEdges'), 'multipleEdges') != 0):
        return 'sewing-deleted-or-multiple-topology'
    if sewing.get('restoredTopologyValid') is not True:
        return 'sewing-restored-topology-invalid'
    if sewing.get('localTolerancesMode') is not False:
        return 'sewing-local-tolerances-mode-not-disabled'
    if sewing.get('derivedGeometryPublished') is not False:
        return 'sewing-derived-geometry-published'
    # Global closure is diagnostic only: this contract is a locally complete
    # adjacency proof, and must not claim that an open source model is a solid.
    _count(sewing.get('globalClosedShells'), 'globalClosedShells')
    _count(sewing.get('globalFreeEdges'), 'globalFreeEdges')
    return None


def _load_context(context_ref):
    _require(isinstance(context_ref, dict), 'context_ref must be an object')
    provenance = {}
    evidence_bytes = None
    for path_key, hash_key in _HASH_FILES:
        _require(isinstance(context_ref.get(path_key), (str, Path)),
                 'missing ' + path_key)
        path = Path(context_ref[path_key])
        _require(path.is_absolute() and path.is_file(), 'missing absolute file: ' + path_key)
        expected = context_ref.get(hash_key)
        _require(isinstance(expected, str) and len(expected) == 64
                 and all(c in '0123456789abcdef' for c in expected.lower()),
                 'invalid ' + hash_key)
        content = path.read_bytes()
        actual = hashlib.sha256(content).hexdigest()
        _require(actual == expected.lower(), 'hash changed: ' + path_key)
        provenance[path_key] = str(path.resolve())
        provenance[hash_key] = actual
        if path_key == 'evidencePath':
            evidence_bytes = content
    if 'sourceName' in context_ref:
        _require(isinstance(context_ref['sourceName'], str), 'invalid sourceName')
        provenance['sourceName'] = context_ref['sourceName']
    try:
        evidence = json.loads(evidence_bytes)
    except (ValueError, UnicodeError) as exc:
        raise SourceSkinMembershipError('invalid evidence JSON') from exc
    _require(isinstance(evidence, dict) and evidence.get('schema') == 1
             and evidence.get('passed') is True, 'native evidence did not complete')
    # These hashes are inserted by the producer wrapper before evidenceSha256
    # is calculated. External file references alone cannot bind a native result
    # to the files the result was actually generated against.
    for key in ('classifySha256', 'sourceNeutralSha256', 'producerSha256'):
        _require(evidence.get(key) == provenance[key], 'evidence input differs: ' + key)
    skin = evidence.get('skinEvidence')
    _require(isinstance(skin, dict) and isinstance(skin.get('matches'), list),
             'missing skinEvidence.matches')
    provenance.update(schema=1, method=METHOD,
                      correspondenceToleranceMm=CORRESPONDENCE_TOLERANCE_MM,
                      skinToleranceMm=SKIN_TOLERANCE_MM,
                      deviationSemantics='SampledMaximum_NotContinuousUpperBound',
                      continuousCertificate=False)
    return evidence, provenance


def _groups(boundary):
    _require(isinstance(boundary, dict) and isinstance(boundary.get('faces'), list),
             'boundary.faces must be a list')
    seen = set()
    for face in boundary['faces']:
        for wire in face['wires']:
            for parent in wire['groups']:
                parts = parent.get('roleSegments') or []
                if parts:
                    _require([use for part in parts for use in part['sourceUses']]
                             == parent['sourceUses'], 'role segment coverage changed')
                for group in parts or [parent]:
                    location = {'faceId': face['faceId'], 'wireId': wire['wireId'],
                                'groupId': parent['groupId']}
                    if parts:
                        _require(group['groupId'] == parent['groupId'],
                                 'role segment parent changed')
                        location['segmentId'] = group['segmentId']
                    identity = tuple(location.values())
                    _require(identity not in seen, 'duplicate boundary group identity')
                    seen.add(identity)
                    yield location, group


def _all_groups(boundary):
    for face in boundary['faces']:
        for wire in face['wires']:
            for parent in wire['groups']:
                yield parent
                yield from parent.get('roleSegments') or []


def _restore_memberships(boundary):
    for group in _all_groups(boundary):
        membership = group.pop('skinMembership', None)
        if membership is not None:
            _require(isinstance(membership, dict)
                     and membership.get('method') == METHOD
                     and membership.get('previousKind') in _ROLES
                     and isinstance(membership.get('previousReason'), str),
                     'invalid saved source skin membership')
            group['kind'] = membership['previousKind']
            group['reason'] = membership['previousReason']
    boundary.pop('skinMembershipContext', None)


def _match_issue(match):
    """Reject unproved or contradictory candidates instead of using a budget."""
    try:
        _identifier(match.get('sourceEdgeId'), 'sourceEdgeId')
        _identifier(match.get('sourceFaceId'), 'sourceFaceId')
        if match.get('method') not in _SOURCE_METHODS:
            return 'unsupported-source-correspondence'
        if (match.get('method') != 'SewnSourceCoedge'
                and ('sewingEvidence' in match or 'sourceSeamGapMm' in match)):
            return 'sewing-evidence-requires-explicit-sewn-method'
        if match.get('sourceSurfaceType') not in {'Plane', 'Cylinder'}:
            return 'uncertified-source-surface'
        support = match.get('certifiedSkinSupport')
        if not isinstance(support, dict) or support.get('passed') is not True:
            return 'source-skin-support-not-certified'
        if support.get('method') != 'AnalyticSupportAndFiniteProfilePcurve':
            return 'unsupported-skin-support-certificate'
        if _number(support.get('supportToleranceMm'), 'supportToleranceMm') > SUPPORT_TOLERANCE_MM:
            return 'skin-support-certificate-too-loose'
        profile_ids = support.get('profileStepEdgeIds')
        if not isinstance(profile_ids, list) or not profile_ids:
            return 'missing-finite-profile-support'
        normalized = [_identifier(i, 'profileStepEdgeId') for i in profile_ids]
        if len(set(normalized)) != len(normalized):
            return 'repeated-profile-support'
        if _number(match.get('maxSourceCorrespondenceMm'), 'maxSourceCorrespondenceMm') > CORRESPONDENCE_TOLERANCE_MM:
            return 'source-correspondence-not-precise'
        _number(match.get('maxSkinDeviationMm'), 'maxSkinDeviationMm')
        for key in ('sourceEdgeToleranceMm', 'sourceFaceToleranceMm', 'nativeTolerance'):
            if key in match:
                _number(match[key], key)
        check = match.get('correspondenceCheck')
        if isinstance(check, dict) and 'endpointMaxDistanceMm' in check:
            if _number(check['endpointMaxDistanceMm'], 'endpointMaxDistanceMm') > CORRESPONDENCE_TOLERANCE_MM:
                return 'source-endpoint-correspondence-not-precise'
        if match.get('method') == 'SewnSourceCoedge':
            issue = _sewing_issue(match)
            if issue:
                return issue
    except SourceSkinMembershipError as exc:
        return str(exc)
    return None


def _index_matches(evidence):
    index = {}
    for match in evidence['skinEvidence']['matches']:
        _require(isinstance(match, dict), 'native match must be an object')
        key = _identifier(match.get('cutStepEdgeId'), 'cutStepEdgeId')
        index.setdefault(key, []).append(match)
    return index


def _group_matches(group, index):
    uses = group.get('sourceUses')
    if not isinstance(uses, list) or not uses:
        return None, 'missing-complete-source-uses'
    selected, seen = [], {}
    for use in uses:
        # This evidence contract certifies complete Classify edges. A future
        # partial-edge representation needs its own finite-interval contract.
        if any(key in use for key in ('first', 'last', 'parameterRange', 'range')):
            return None, 'partial-source-use-not-certified'
        key = _identifier(use.get('edgeId'), 'edgeId')
        matches = index.get(key)
        if not matches:
            return None, 'source-use-has-no-certified-skin-match'
        for match in matches:
            issue = _match_issue(match)
            if issue:
                return None, issue
            identity = (key, _identifier(match['sourceEdgeId'], 'sourceEdgeId'),
                        _identifier(match['sourceFaceId'], 'sourceFaceId'))
            if identity in seen and seen[identity] != match:
                return None, 'inconsistent-source-match'
            if identity not in seen:
                selected.append(deepcopy(match))
                seen[identity] = match
    return selected, None


def apply_context(boundary, context_ref):
    """Mutate boundary roles transactionally and return its bound provenance.

    Missing or unproved group matches leave that group unchanged. Broken input
    hashes or an invalid evidence envelope reject the entire operation. Repeated
    application restores saved prior roles first, so provenance stays stable.
    """
    evidence, provenance = _load_context(context_ref)
    for key in ('sourceHash', 'sourceSha256', 'classifySha256'):
        if boundary.get(key) is not None:
            _require(boundary[key] == provenance['classifySha256'],
                     'boundary belongs to a different Classify input')
    working = deepcopy(boundary)
    _restore_memberships(working)
    index = _index_matches(evidence)
    applied, rejected = [], []
    changed = approximate = 0
    for location, group in _groups(working):
        if group.get('seam') or group.get('event'):
            continue
        _require(group.get('kind') in _ROLES and isinstance(group.get('reason'), str),
                 'invalid boundary role')
        matches, issue = _group_matches(group, index)
        if issue:
            rejected.append(dict(location, reason=issue))
            continue
        measured = _number(group.get('maxSkinDistance'), 'maxSkinDistance')
        maximum = max([measured] + [float(m['maxSkinDeviationMm']) for m in matches])
        sewn_matches = [match for match in matches if match['method'] == 'SewnSourceCoedge']
        accuracy = ('Approximate' if maximum > SKIN_TOLERANCE_MM or sewn_matches
                    else 'WithinSkinTolerance')
        membership = {'method': METHOD, 'accuracy': accuracy,
                      'maxSkinDistanceMm': maximum, 'sourceMatches': matches,
                      'previousKind': group['kind'], 'previousReason': group['reason'],
                      'deviationSemantics': 'SampledMaximum_NotContinuousUpperBound',
                      'continuousCertificate': False}
        if sewn_matches:
            membership['maxSourceSeamGapMm'] = max(m['sourceSeamGapMm'] for m in sewn_matches)
            membership['sourceSeamSemantics'] = 'SampledMaximum_NotContinuousUpperBound'
        changed += group['kind'] != 'Skin' or bool(group['reason'])
        approximate += accuracy == 'Approximate'
        group.update(kind='Skin', reason='', skinMembership=membership)
        applied.append(dict(location, accuracy=accuracy, previousKind=membership['previousKind']))
    provenance.update(appliedGroups=applied, rejectedGroups=rejected,
                      appliedGroupCount=len(applied), roleChangedGroupCount=changed,
                      approximateGroupCount=approximate)
    working['skinMembershipContext'] = deepcopy(provenance)
    boundary.clear()
    boundary.update(working)
    return deepcopy(provenance)


def validate_context(boundary, context_ref):
    """Rehash all inputs and replay roles/provenance from their saved prior state."""
    expected = deepcopy(boundary)
    _restore_memberships(expected)
    provenance = apply_context(expected, context_ref)
    _require(boundary == expected, 'source skin membership replay differs')
    for _, group in _groups(boundary):
        if group.get('kind') == 'Skin' and group.get('maxSkinDistance', 0) > SKIN_TOLERANCE_MM:
            _require('skinMembership' in group, 'off-profile Skin has no source membership evidence')
    return provenance
