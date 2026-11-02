"""Face -> logical boundaries -> boundary chains -> placement-independent cut units.

No UV sweep, tool-axis selection, ruled approximation, or machine motion occurs here.
The region is retained exactly; complex end-boundary organizations remain pending.
"""
from collections import Counter, defaultdict
from dataclasses import dataclass, asdict
import math
from cut_unit_assembly import CutUnitAssembler, side_constraint, MERGE_RULE

ROLE_NAMES={'Skin':'皮上','Side':'侧边','Internal':'内部边','Unresolved':'未决'}
FORM_NAMES={'OpenBand':'开口带状','ClosedBand':'闭合带状','UnpairedRegion':'未配对区域'}


@dataclass
class BoundaryChain:
    id: int
    wire: int
    purpose: str
    logicalEdges: list
    roles: list
    closed: bool
    sourceUses: list
    points: list


def reduced(points):
    ids=set(range(0,len(points),4))|set(range(0,len(points),49))|{len(points)-1}
    return [points[i] for i in sorted(ids) if i>=0]


class FaceBoundaryAnalyzer:
    def analyze(self, face):
        groups=[];geometry_groups=[];wires=[];issues=[]

        def boundary_group(g, wire_id, key):
            result={'key':key,'id':g['groupId'],'wire':wire_id,'role':g['kind'],
                'closed':g['closed'],'straight':g['straight'],'reason':g['reason'],
                'sourceUses':g['sourceUses'],'sourceEdges':[e['edgeId'] for e in g['sourceUses']],
                'rawEdges':[{'id':e['edgeId'],'reversed':e['reversed'],'points':reduced(g['polyline'][i*49:(i+1)*49])} for i,e in enumerate(g['sourceUses'])],
                'points':reduced(g['polyline']),'start':g['start'],'end':g['end'],
                'startTangent':g.get('startTangent'),'endTangent':g.get('endTangent'),
                'startOnSkin':g['startOnSkin'],'endOnSkin':g['endOnSkin'],
                'skinDistanceRangeMm':[g['minSkinDistance'],g['maxSkinDistance']],
                'geometry':'Line' if all(e.get('lineLike') for e in g['sourceUses']) else 'Circle' if all(e['curveType']==1 for e in g['sourceUses']) else 'Curve'}
            if 'skinMembership' in g:
                result['skinMembership']=g['skinMembership']
            return result

        for wire in face['wires']:
            physical=[g for g in wire['groups'] if not g['seam'] and not g['event']]
            if wire['rawEdgeUses']!=wire['expectedEdgeUses']:
                issues.append('wire-edge-coverage-incomplete')
            if not physical:
                issues.append('wire-has-no-physical-boundary')
            keys=[];wire_segments=[]
            for g in physical:
                parent_key=f"W{wire['wireId']}:G{g['groupId']}"
                parent=boundary_group(g,wire['wireId'],parent_key)
                children=g.get('roleSegments') or []
                if children:
                    if [e for child in children for e in child['sourceUses']]!=g['sourceUses']:
                        raise ValueError('role-segment-source-partition-changed')
                    if [child['segmentId'] for child in children]!=list(range(1,len(children)+1)):
                        raise ValueError('role-segment-order-invalid')
                    parent['roleSegmentKeys']=[]
                    for child in children:
                        if child['kind'] not in ('Skin','Side','Internal'):
                            raise ValueError('role-segment-not-resolved')
                        key=f"{parent_key}:S{child['segmentId']}"
                        segment=boundary_group(child,wire['wireId'],key)
                        segment.update(geometryGroupKey=parent_key,segmentId=child['segmentId'])
                        groups.append(segment);wire_segments.append(segment);keys.append(key);parent['roleSegmentKeys'].append(key)
                else:
                    groups.append(parent);wire_segments.append(parent);keys.append(parent_key)
                    if g['kind']=='Unresolved':issues.append('boundary-unresolved')
                geometry_groups.append(parent)
            # Cyclic chains must be G0-connected even when a primitive boundary ends.
            for a,b in zip(wire_segments,wire_segments[1:]+wire_segments[:1]):
                if math.dist(a['end'],b['start'])>.001:
                    issues.append('boundary-chain-gap')
            wires.append({'id':wire['wireId'],'outer':wire['outer'],'groups':keys,
                'rawEdgeUses':wire['rawEdgeUses'],'excludedUses':sum(len(g['sourceUses']) for g in wire['groups'] if g['seam'] or g['event'])})
        return groups,wires,list(dict.fromkeys(issues)),geometry_groups


class CutUnitBuilder:
    def build(self, face):
        groups,wires,issues,geometry_groups=FaceBoundaryAnalyzer().analyze(face)
        bykey={g['key']:g for g in groups};chains=[]
        for wire in wires:
            ordered=[bykey[k] for k in wire['groups']]
            if not ordered:continue
            # Keep distinct logical primitives. Only concatenate their references
            # into the same boundary chain between Side/non-Side transitions.
            buckets=[]
            for g in ordered:
                purpose='EndBoundary' if g['role']=='Side' else 'CutBoundary'
                if buckets and buckets[-1][0]==purpose:buckets[-1][1].append(g)
                else:buckets.append((purpose,[g]))
            if len(buckets)>1 and buckets[0][0]==buckets[-1][0]:
                buckets[0][1][:0]=buckets[-1][1];buckets.pop()
            for purpose,gs in buckets:
                points=[]
                for g in gs:points.extend(g['points'])
                chain=BoundaryChain(len(chains)+1,wire['id'],purpose,[g['key'] for g in gs],
                    list(dict.fromkeys(g['role'] for g in gs)),len(buckets)==1,
                    [e for g in gs for e in g['sourceUses']],points)
                chains.append(asdict(chain))
        roles=Counter(g['role'] for g in groups)
        ends=[c for c in chains if c['purpose']=='EndBoundary']
        cuts=[c for c in chains if c['purpose']=='CutBoundary']
        form=None;reason='';decision=''
        if face['area']<=1e-10:issues.append('zero-area')
        if issues:reason=issues[0];decision='边界或源区域未通过检查，保留原面并列出具体问题。'
        elif not roles.get('Skin'):
            reason='no-skin-anchor';decision='没有皮上边界链；保留内部区域，需结合相邻面解释入口和终止关系。'
        elif len(wires)==1 and len(ends)==2 and len(cuts)==2:
            form='OpenBand';decision='一个闭环由两条切割边界链和两条端部侧边链交替组成；保留整个原面为一个开口带状单元。'
        elif len(wires)==2 and not ends and len(cuts)==2 and sum(w['outer'] for w in wires)==1:
            form='ClosedBand';decision='一个外环和一个内环构成闭合带状区域；两条闭合边界链完整保留，尚未选择配对或入射方向。'
        elif not ends and all(g['role']=='Skin' for g in groups):
            form='UnpairedRegion';decision='边界全部在皮上且没有侧边；保留完整区域和所有环，边界配对留待摆放及机构约束明确后确定。'
        else:
            reason='boundary-organization-ambiguous'
            decision=f'现有 {len(wires)} 个环、{len(ends)} 条端部侧边链和 {len(cuts)} 条切割边界链，无法由当前规则确定单元组织；未按 UV 方向强行分片。'
        termination=[{'logicalEdge':g['key'],'chain':next(c['id'] for c in chains if g['key'] in c['logicalEdges']),
            'kind':'InteriorBoundary','state':'RequiresAdjacentMaterialInterpretation',
            'reason':'内部边界保留为材料约束；尚未把它指定为半切透工艺或打标。'} for g in groups if g['role']=='Internal']
        units=[]
        axis_constraints=[side_constraint(face['faceId'],g) for g in groups if g['role']=='Side']
        if form:
            units=[{'id':1,'form':form,'name':FORM_NAMES[form],
                'sourceRegion':{'faceId':face['faceId'],'scope':'WholeOriginalFace'},
                'chainIds':[c['id'] for c in chains],'cutChainIds':[c['id'] for c in cuts],
                'endChainIds':[c['id'] for c in ends],'terminationConstraints':termination,
                'axisConstraints':axis_constraints,
                'pairingState':'Deferred','machiningState':'NotPlanned','newBoundaryCount':0,
                'decision':decision,'geometryPolicy':'PreserveExactSourceSurfaceAndTrims'}]
        return {'faceId':face['faceId'],'surface':face['surface'],'sourceAreaMm2':face['area'],
            'planeEvidence':face.get('planeEvidence'), 'axisConstraints':axis_constraints,
            'status':'organized' if units else 'pending','reason':reason,'issues':issues,
            'decision':decision,'groups':groups,'geometricGroups':geometry_groups,'wires':wires,'chains':chains,'roles':dict(roles),
            'rawEdgeUses':sum(w['rawEdgeUses'] for w in wires),'logicalEdgeCount':len(geometry_groups),
            'boundarySegmentCount':len(groups),
            'terminationConstraints':termination,'units':units,'newBoundaryCount':0,
            'neighbors':[],'adjacencyState':'TopoEdgeUsesOnly','coverageState':'WholeFaceRetained'}


def analyze_case(data):
    builder=CutUnitBuilder();faces=[builder.build(f) for f in data['faces']]
    # Exact shared topology is evidence. Detached coincident edges are not silently
    # stitched: they remain explicit unresolved adjacency, without changing CAD.
    uses=defaultdict(list)
    for f in faces:
        for g in f['groups']:
            for e in g['sourceUses']:uses[e['edgeId']].append((f['faceId'],g['key']))
    for f in faces:
        seen=set()
        for g in f['groups']:
            for e in g['sourceUses']:
                for other,key in uses[e['edgeId']]:
                    if other==f['faceId']:continue
                    signature=(g['key'],other,key,e['edgeId'])
                    if signature in seen:continue
                    seen.add(signature);f['neighbors'].append({'logicalEdge':g['key'],'otherFace':other,'otherLogicalEdge':key,'sourceEdge':e['edgeId'],'evidence':'SharedTopoEdge'})
        if not f['neighbors']:f['adjacencyState']='NoSharedTopoEdge_GeometricCorrespondenceNotResolved'
    assemblies,merge_decisions=CutUnitAssembler().assemble(faces)
    result={'schemaVersion':2,'stage':'GeometryUnits','sourceFile':data['file'],
        'mergeRulesVersion':2,'mergeRule':MERGE_RULE,
        'assembledUnits':assemblies,'mergeDecisions':merge_decisions,
        'boundaryVersion':data['version'],'faces':faces,
        'profile':[reduced(p) for p in data['profile']],
        'tolerances':{'positionMm':.001,'tangentRadians':.001,'materialMm':1e-7},
        'boundaryEvidence':'49 samples per source edge; no continuous skin-contact certificate',
        'machinePlanning':'Deferred'}
    if 'skinMembershipContext' in data:
        result['skinMembershipContext']=data['skinMembershipContext']
    return result
