"""Eliminate shared Side intervals only for coplanar units and two-sided G1.

Retain original faces, logical primitives and every non-eliminated Side's
geometric cutting-direction constraint. Closed topology alone is insufficient.
"""
from collections import defaultdict
from itertools import product
import math

TOL = .001
ANGLE_TOL = .001
PLANE_TOL = 1e-7
PLANE_ANGLE_TOL = 1e-7
MERGE_RULE = 'CoplanarUnitsWithTwoSidedG1SideElimination'


def ref(face_id, key):
    return f'F{face_id}/{key}'


def unit_vector(value):
    if value is None or len(value) != 3 or not all(math.isfinite(x) for x in value):
        return None
    length = math.sqrt(sum(x*x for x in value))
    return [x/length for x in value] if length > 1e-12 else None


def dot(a, b):
    return sum(x*y for x, y in zip(a, b))


def reversed_uses(uses, reverse):
    return [dict(e, reversed=not e['reversed']) for e in reversed(uses)] if reverse else [dict(e) for e in uses]


def side_constraint(face_id, group):
    direction = unit_vector([b-a for a, b in zip(group['start'], group['end'])]) if group['geometry'] == 'Line' else None
    return {'sourceFaceId': face_id, 'logicalEdge': group['key'],
            'sourceUses': [dict(use) for use in group['sourceUses']],
            'geometry': group['geometry'], 'start': group['start'], 'end': group['end'],
            'points': group['points'], 'candidateDirection': direction,
            'state': 'RetainedSideDirectionConstraint',
            'directionSemantics': 'UnorientedGeometricConstraint_NotMachinePose'}


def coplanarity(faces):
    """Certify the whole component against one fixed reference plane."""
    faces = sorted(faces, key=lambda face: face['faceId'])
    result = {'passed': False, 'reason': '', 'sourceFaceIds': [f['faceId'] for f in faces],
              'positionToleranceMm': PLANE_TOL, 'normalToleranceRadians': PLANE_ANGLE_TOL,
              'maxPlaneDistanceMm': 0., 'maximumNormalAngleRadians': 0., 'certificatePointCount': 0}
    if not faces:
        result['reason'] = 'unit-plane-evidence-missing'
        return result
    evidence = faces[0].get('planeEvidence') or {}
    plane = evidence.get('plane') or {}
    normal = unit_vector(plane.get('normal')); origin = plane.get('origin')
    if normal is None or origin is None:
        result['reason'] = 'unit-plane-evidence-missing' if not evidence else 'unit-not-planar'
        return result
    result.update(referenceFaceId=faces[0]['faceId'], referencePlane={'origin': origin, 'normal': normal})
    for face in faces:
        evidence = face.get('planeEvidence') or {}
        if not evidence:
            result['reason'] = 'unit-plane-evidence-missing'
            return result
        if not evidence.get('isPlanar'):
            result['reason'] = 'unit-not-planar'
            return result
        other = unit_vector((evidence.get('plane') or {}).get('normal'))
        points = evidence.get('certificatePoints', [])
        if (other is None or len(points) < 3 or not evidence.get('method')
                or not evidence.get('parameterDomainCovered') or not evidence.get('positiveWeights')):
            result['reason'] = 'unit-plane-evidence-incomplete'
            return result
        angle = math.acos(min(1., max(-1., abs(dot(normal, other)))))
        result['maximumNormalAngleRadians'] = max(result['maximumNormalAngleRadians'], angle)
        for point in points:
            if len(point) != 3 or not all(math.isfinite(x) for x in point):
                result['reason'] = 'unit-plane-evidence-incomplete'
                return result
            distance = abs(dot(normal, [x-y for x, y in zip(point, origin)]))
            result['maxPlaneDistanceMm'] = max(result['maxPlaneDistanceMm'], distance)
        result['certificatePointCount'] += len(points)
    result['passed'] = result['maxPlaneDistanceMm'] <= PLANE_TOL and result['maximumNormalAngleRadians'] <= PLANE_ANGLE_TOL
    if not result['passed']:
        result['reason'] = 'units-not-coplanar'
    return result


def endpoint_cut(face, side, endpoint):
    """Exact adjacent cut tangent, directed away from the shared endpoint."""
    wire = next(w for w in face['wires'] if side['key'] in w['groups'])
    keys = wire['groups']; index = keys.index(side['key']); groups = {g['key']: g for g in face['groups']}
    neighbor = groups[keys[(index-1) % len(keys)] if endpoint == 'start' else keys[(index+1) % len(keys)]]
    if neighbor['role'] not in ('Skin', 'Internal'):
        return None, 'side-endpoint-has-no-cut-boundary'
    tangent = unit_vector(neighbor.get('endTangent' if endpoint == 'start' else 'startTangent'))
    if tangent is None:
        return None, 'cut-boundary-tangent-unavailable'
    if endpoint == 'start':
        tangent = [-x for x in tangent]
    return {'sourceFaceId': face['faceId'], 'logicalEdge': neighbor['key'], 'role': neighbor['role'],
            'point': neighbor['end' if endpoint == 'start' else 'start'], 'outwardTangent': tangent}, ''


def two_sided_tangency(first_face, first, second_face, second):
    direct = max(math.dist(first['start'], second['start']), math.dist(first['end'], second['end']))
    reverse = max(math.dist(first['start'], second['end']), math.dist(first['end'], second['start']))
    result = {'passed': False, 'reason': '', 'angleToleranceRadians': ANGLE_TOL,
              'positionToleranceMm': TOL, 'endpoints': []}
    if direct <= TOL and reverse <= TOL:
        result['reason'] = 'side-endpoint-correspondence-ambiguous'
        return result
    swap = reverse < direct
    for endpoint in ('start', 'end'):
        other_endpoint = ('end' if endpoint == 'start' else 'start') if swap else endpoint
        a, reason_a = endpoint_cut(first_face, first, endpoint)
        b, reason_b = endpoint_cut(second_face, second, other_endpoint)
        proof = {'sourceEndpoint': endpoint, 'otherEndpoint': other_endpoint,
                 'firstCutBoundary': a, 'secondCutBoundary': b, 'passed': False,
                 'gapMm': None, 'angleRadians': None, 'reason': reason_a or reason_b}
        if a is not None and b is not None:
            proof.update(firstTangent=a['outwardTangent'], secondTangent=b['outwardTangent'],
                         tangentDirectionSemantics='AwayFromJointIntoEachFace_OppositeMeansG1')
            proof['gapMm'] = max(math.dist(first[endpoint], second[other_endpoint]),
                                 math.dist(a['point'], first[endpoint]), math.dist(b['point'], second[other_endpoint]),
                                 math.dist(a['point'], b['point']))
            # Tangents both point away from the joint into their own face.
            # G1 continuation requires opposite vectors, not a folded return.
            proof['angleRadians'] = math.acos(max(-1., min(1., -dot(a['outwardTangent'], b['outwardTangent']))))
            if a['role'] != b['role']:
                proof['reason'] = 'cut-boundary-role-mismatch'
            elif proof['gapMm'] > TOL:
                proof['reason'] = 'cut-boundary-join-gap'
            elif proof['angleRadians'] > ANGLE_TOL:
                proof['reason'] = 'cut-boundary-not-g1'
            else:
                proof['passed'] = True
        result['endpoints'].append(proof)
    result['passed'] = len(result['endpoints']) == 2 and all(p['passed'] for p in result['endpoints'])
    if not result['passed']:
        result['reason'] = next((p['reason'] for p in result['endpoints'] if p['reason']), 'cut-boundary-not-g1')
    return result


def repeated_exterior(entries):
    for index, (fid, group) in enumerate(entries):
        for other_fid, other in entries[:index]:
            if fid == other_fid:
                continue
            mismatch = min(max(math.dist(group['start'], other['start']), math.dist(group['end'], other['end'])),
                           max(math.dist(group['start'], other['end']), math.dist(group['end'], other['start'])))
            if mismatch > TOL:
                continue
            if group['geometry'] == other['geometry'] == 'Line':
                return True
            if (all(min(math.dist(p, q) for q in other['points']) <= TOL for p in group['points'])
                    and all(min(math.dist(p, q) for q in group['points']) <= TOL for p in other['points'])):
                return True
    return False


def reconstruct_loops(entries):
    """Traverse degree-two external loops while retaining source directions."""
    nodes = []; incidence = defaultdict(list); segments = []; loops = []
    def node(point):
        found = [i for i, other in enumerate(nodes) if math.dist(point, other) <= TOL]
        if len(found) > 1:
            raise ValueError('ambiguous-boundary-junction')
        if found:
            return found[0]
        nodes.append(point)
        return len(nodes)-1
    for face_id, group in entries:
        entry = {'sourceFaceId': face_id, 'group': group}
        if group['closed']:
            loops.append([(entry, False)])
            continue
        a, b = node(group['start']), node(group['end']); index = len(segments)
        segments.append((entry, a, b)); incidence[a].append(index); incidence[b].append(index)
    if any(len(edges) != 2 for edges in incidence.values()):
        raise ValueError('external-boundary-open-or-branched')
    used = set()
    for index, (entry, a, b) in enumerate(segments):
        if index in used:
            continue
        loop = []; start = a; cursor = a; segment = index
        while segment not in used:
            used.add(segment); entry, a, b = segments[segment]; reverse = cursor == b
            loop.append((entry, reverse)); cursor = a if reverse else b
            if cursor == start:
                break
            following = [j for j in incidence[cursor] if j not in used]
            if len(following) != 1:
                raise ValueError('external-boundary-open-or-branched')
            segment = following[0]
        if cursor != start:
            raise ValueError('external-boundary-not-closed')
        loops.append(loop)
    return loops


def boundary_chains(loops):
    chains = []
    for wire_id, loop in enumerate(loops, 1):
        buckets = []
        for entry, reverse in loop:
            purpose = 'EndBoundary' if entry['group']['role'] == 'Side' else 'CutBoundary'
            if buckets and buckets[-1][0] == purpose:
                buckets[-1][1].append((entry, reverse))
            else:
                buckets.append((purpose, [(entry, reverse)]))
        if len(buckets) > 1 and buckets[0][0] == buckets[-1][0]:
            buckets[0][1][:0] = buckets[-1][1]; buckets.pop()
        for purpose, items in buckets:
            uses = []; points = []; keys = []; parts = []; roles = []
            for entry, reverse in items:
                group = entry['group']; fid = entry['sourceFaceId']; keys.append(ref(fid, group['key']))
                uses.extend(dict(use, sourceFaceId=fid) for use in reversed_uses(group['sourceUses'], reverse))
                points.extend(reversed(group['points']) if reverse else group['points'])
                parts.append({'sourceFaceId': fid, 'logicalEdge': group['key'], 'reversed': reverse})
                if group['role'] not in roles:
                    roles.append(group['role'])
            chains.append({'id': len(chains)+1, 'wire': wire_id, 'purpose': purpose, 'logicalEdges': keys,
                           'parts': parts, 'roles': roles, 'closed': len(buckets) == 1,
                           'sourceUses': uses, 'points': points})
    return chains


class CutUnitAssembler:
    def assemble(self, faces):
        byid = {face['faceId']: face for face in faces}
        entries = sorted([(face['faceId'], group) for face in faces
                          if not face['issues'] and face['sourceAreaMm2'] > 1e-10
                          for group in face['groups'] if group['role'] == 'Side' and group['geometry'] == 'Line'
                          and group['straight'] and not group['closed']], key=lambda item: (item[0], item[1]['key']))
        buckets = defaultdict(set); candidates = defaultdict(list)
        def cell(point):
            return tuple(math.floor(x/TOL) for x in point)
        for index, (fid, group) in enumerate(entries):
            near = set(); c = cell(group['start'])
            for offset in product([-1, 0, 1], repeat=3):
                near.update(buckets[tuple(x+y for x, y in zip(c, offset))])
            for other_index in sorted(near):
                other_fid, other = entries[other_index]
                if other_fid == fid:
                    continue
                error = min(max(math.dist(group['start'], other['start']), math.dist(group['end'], other['end'])),
                            max(math.dist(group['start'], other['end']), math.dist(group['end'], other['start'])))
                if error <= TOL:
                    candidates[index].append((other_index, error)); candidates[other_index].append((index, error))
            buckets[cell(group['start'])].add(index); buckets[cell(group['end'])].add(index)
        decisions = []; eligible = []; parent = {fid: fid for fid in byid}; members = {fid: {fid} for fid in byid}
        def root(fid):
            while parent[fid] != fid:
                fid = parent[fid]
            return fid
        for index in sorted(candidates):
            for other_index, error in sorted(candidates[index]):
                if index >= other_index:
                    continue
                fid, group = entries[index]; other_fid, other = entries[other_index]
                sides = [{'sourceFaceId': source_fid, 'logicalEdge': source['key'], 'sourceUses': source['sourceUses']}
                         for source_fid, source in [(fid, group), (other_fid, other)]]
                decision = {'kind': 'SideElimination', 'sourceFaceIds': [fid, other_fid], 'sides': sides,
                            'accepted': False, 'reason': '', 'maximumEndpointMismatchMm': error}
                decisions.append(decision)
                if len(candidates[index]) != 1 or len(candidates[other_index]) != 1:
                    decision['reason'] = 'ambiguous-multiple-neighbors'
                    continue
                a, b = root(fid), root(other_fid); combined = sorted(members[a] | members[b])
                plane = coplanarity([byid[k] for k in combined])
                tangent = two_sided_tangency(byid[fid], group, byid[other_fid], other)
                decision.update(coplanarity=plane, tangency=tangent)
                if not plane['passed'] or not tangent['passed']:
                    decision['reason'] = plane['reason'] if not plane['passed'] else tangent['reason']
                    continue
                seam = {'purpose': 'InternalSeam', 'sides': sides,
                        'logicalEdges': [ref(side['sourceFaceId'], side['logicalEdge']) for side in sides],
                        'points': group['points'], 'maximumEndpointMismatchMm': error,
                        'evidence': MERGE_RULE, 'coplanarity': plane, 'tangency': tangent}
                decision['candidateSeams'] = [seam]; decision['accepted'] = True
                eligible.append((index, other_index, seam, decision))
                if a != b:
                    first, second = min(a, b), max(a, b); parent[second] = first; members[first] = set(combined)
        components = defaultdict(list)
        for pair in eligible:
            components[root(entries[pair[0]][0])].append(pair)
        assemblies = []
        for component_root, pairs in sorted(components.items()):
            face_ids = sorted(members[component_root]); seams = []; internal = set()
            for _, _, seam, decision in pairs:
                seam = dict(seam, id=len(seams)+1); seams.append(seam); internal.update(seam['logicalEdges'])
                decision['candidateSeams'] = [seam]
            remaining = [(fid, group) for fid in face_ids for group in byid[fid]['groups'] if ref(fid, group['key']) not in internal]
            reason = ''; chains = []; loops = []; form = None
            plane = coplanarity([byid[fid] for fid in face_ids])
            if not plane['passed']:
                reason = plane['reason']
            elif any(group['role'] == 'Unresolved' for _, group in remaining):
                reason = 'external-boundary-unresolved'
            elif repeated_exterior(remaining):
                reason = 'repeated-exterior-boundary-cannot-prove-coverage'
            else:
                try:
                    loops = reconstruct_loops(remaining); chains = boundary_chains(loops)
                except ValueError as error:
                    reason = str(error)
            euler = sum(2-len(byid[fid]['wires']) for fid in face_ids)-len(seams)
            ends = [chain for chain in chains if chain['purpose'] == 'EndBoundary']
            cuts = [chain for chain in chains if chain['purpose'] == 'CutBoundary']
            if not reason:
                if len(loops) == 1 and len(ends) == 2 and len(cuts) == 2 and euler == 1:
                    form = 'OpenBand'
                elif len(loops) == 2 and not ends and len(cuts) == 2 and euler == 0 and all(group['role'] == 'Skin' for _, group in remaining):
                    form = 'ClosedBand'
                elif not ends and all(group['role'] == 'Skin' for _, group in remaining) and euler == 2-len(loops):
                    form = 'UnpairedRegion'
                else:
                    reason = 'merged-boundary-organization-ambiguous'
            for _, _, _, decision in pairs:
                decision.update(externalLoopCount=len(loops), eulerCharacteristic=euler)
                if reason:
                    decision.update(accepted=False, reason=reason, regionReason=reason)
            if reason:
                continue
            aid = len(assemblies)+1; key = f'A{aid:03d}'
            names = {'OpenBand': '多面共面开口带状', 'ClosedBand': '多面共面闭合带状', 'UnpairedRegion': '多面共面未配对区域'}
            explanation = (f"{'、'.join('F'+str(fid) for fid in face_ids)} 整体共面，{len(seams)} 条公共侧边的两端切割边界均连续相切；"
                           f"这些侧边可消去为冗余接缝，保留 {len(ends)} 条外部侧边链及其切割方向约束。")
            constraints = [side_constraint(fid, group) for fid, group in remaining if group['role'] == 'Side']
            termination = [{'sourceFaceId': fid, 'logicalEdge': group['key'], 'kind': 'InteriorBoundary',
                            'state': 'RequiresAdjacentMaterialInterpretation'} for fid, group in remaining if group['role'] == 'Internal']
            unit = {'id': 1, 'assemblyId': aid, 'key': key, 'form': form, 'name': names[form],
                    'sourceRegion': {'faceIds': face_ids, 'scope': 'WholeOriginalFaces'},
                    'chainIds': [chain['id'] for chain in chains], 'cutChainIds': [chain['id'] for chain in cuts],
                    'endChainIds': [chain['id'] for chain in ends], 'boundaries': chains, 'internalSeams': seams,
                    'axisConstraints': constraints, 'terminationConstraints': termination,
                    'pairingState': 'Deferred', 'machiningState': 'NotPlanned', 'newBoundaryCount': 0,
                    'decision': explanation, 'coplanarity': plane, 'geometryPolicy': 'PreserveExactSourceFacesWithoutFlattening',
                    'mergeRule': MERGE_RULE, 'eulerCharacteristic': euler, 'externalLoopCount': len(loops)}
            assemblies.append(unit)
            for fid in face_ids:
                face = byid[fid]; face['initialOrganization'] = {field: face[field] for field in ['status', 'reason', 'decision']}
                face['initialOrganization']['unitCount'] = len(face['units'])
                face.update(units=[unit], status='organized', reason='', decision=explanation, assemblyKey=key)
        return assemblies, decisions
