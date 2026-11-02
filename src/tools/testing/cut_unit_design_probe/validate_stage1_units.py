"""Audit immutable inputs, per-face coverage, chain partition and color STEP readback."""
import math, subprocess
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from run_stage1_units import DEST, OLD, ROOT, ENV, read, write, sha

EXE=OLD.parent/'build/Release/audit_colored_units.exe'
PUBLISHED=ROOT/'TestData/Tube/CutUnits'

def expected_unit_groups(data):
    """Derive membership from face decisions, independently of exported metadata."""
    groups={}
    for face in data['faces']:
        unit=face['units'][0] if face['units'] else None
        key=face.get('assemblyKey') or f"F{face['faceId']:04d}_"+('U001' if unit else 'PENDING')
        ids=unit['sourceRegion'].get('faceIds',[face['faceId']]) if unit else [face['faceId']]
        if key in groups:assert groups[key]==ids,(key,'inconsistent source-face membership')
        else:groups[key]=ids
    return groups

def group_membership(groups):
    result={}
    for group in groups:
        assert group['key'] not in result,('duplicate whole-model group',group['key'])
        result[group['key']]=group['sourceFaceIds']
    return result

def audit_inspection_and_publication(item,folder,data,result,display):
    groups=expected_unit_groups(data)
    assert group_membership(display['groups'])==groups,'whole-model unit membership changed'
    report=read(folder/'inspection-audit.json')
    assert report['passed'] is True and report['displayOnlyGeometry'] is True
    assert report['sourceHash']==item['sha256'] and report['sourceFile']==item['file']
    assert report['caseId']==item['id'] and report['originalFaces']==data['faceCount']
    assert report['modelRoots']==1,'inspection must have exactly one model root'
    assert report['unitGroups']==len(groups)==result['units']+result['pendingFaces']
    inspection_sha=sha(folder/'inspection.step')
    assert report['inspectionSha256']==display['inspectionSha256']==inspection_sha
    expected={key+'__'+'_'.join('F'+str(fid) for fid in ids):
              {f'FACE_F{fid:04d}' for fid in ids} for key,ids in groups.items()}
    actual={};markers=set();seen_faces=Counter()
    for group in report['groups']:
        name=group['unit'];assert name not in actual,('duplicate inspection unit',name)
        faces=[]
        for child in group['children']:
            if child['originalFace']:
                assert child['faces']==1,(name,child['name'],'original face count')
                faces.append(child['name']);seen_faces[child['name']]+=1
            else:
                assert child['name'].startswith('DISPLAY_') and child['edges']>0
                marker=name+'/'+child['name'];assert marker not in markers
                markers.add(marker)
        assert len(faces)==len(set(faces)),('repeated face in inspection unit',name)
        actual[name]=set(faces)
    assert actual==expected,'inspection source-face unit membership changed'
    assert seen_faces==Counter({f'FACE_F{fid:04d}':1 for fid in range(1,data['faceCount']+1)})
    assert report['markerGroups']==len(markers)==len(report['markers'])
    assert {entry['name'] for entry in report['markers']}==markers
    assert all(entry['strokePieces']>0 for entry in report['markers'])

    stem=Path(item['file']).stem
    published={suffix:PUBLISHED/(stem+'.'+suffix) for suffix in ('brep','step','json')}
    hashes={suffix:sha(path) for suffix,path in published.items()}
    assert hashes['brep']==display['brepSha256'],'published whole BREP differs'
    assert hashes['step']==inspection_sha,'published inspection STEP differs'
    metadata=read(published['json'])
    assert metadata['brep']==published['brep'].name and metadata['coloredStep']==published['step'].name
    assert metadata['caseId']==item['id'] and metadata['sourceFile']==item['file']
    assert metadata['sourceSha256']==item['sha256']
    assert metadata['brepSha256']==hashes['brep']
    assert metadata['coloredStepSha256']==metadata['inspectionSha256']==hashes['step']
    assert metadata['faceCount']==data['faceCount'] and metadata['unitCount']==result['units']
    assert metadata['pendingFaces']==result['pendingFaces']
    assert metadata['stepPurpose']=='InspectionWithSeparateDisplayMarkers'
    assert metadata['displayOnlyGeometry'] is True
    assert metadata['inspectionAudit']==report,'published inspection audit differs'
    assert group_membership(metadata['groups'])==groups,'published source-face membership changed'
    cache_groups={group['key']:group for group in display['groups']}
    for group in metadata['groups']:
        cached=cache_groups[group['key']]
        for key in ('childIndex','status','sourceFaceIds','color','faces'):
            assert group[key]==cached[key],(group['key'],key,'published group metadata differs')
    return {'inspectionOriginalFaces':report['originalFaces'],'inspectionUnitGroups':report['unitGroups'],
            'inspectionMarkerGroups':report['markerGroups'],'publishedHashes':hashes}

def audit_published_inventory(sources):
    """One original-stem file per format; unit BREP files must not be published."""
    issues=[];counts={}
    stems=[Path(item['file']).stem for item in sources]
    assert len({stem.casefold() for stem in stems})==len(stems),'input model stems collide'
    for suffix in ('brep','step','json'):
        expected={stem+'.'+suffix for stem in stems}
        actual={path.name for path in PUBLISHED.iterdir() if path.is_file() and path.suffix.lower()=='.'+suffix}
        counts[suffix]=len(actual)
        missing=sorted(expected-actual);extra=sorted(actual-expected)
        if missing or extra:issues.append({'format':suffix,'missing':missing,'extra':extra})
    return {'counts':counts,'issues':issues}

def audit_axis_constraints(unit, members):
    """Only accepted redundant seams may disappear from Side direction constraints."""
    removed={ref for seam in unit.get('internalSeams',[]) for ref in seam['logicalEdges']}
    expected={f"F{face['faceId']}/{group['key']}":group for face in members
              for group in face['groups'] if group['role']=='Side'
              and f"F{face['faceId']}/{group['key']}" not in removed}
    actual={}
    for constraint in unit['axisConstraints']:
        key=f"F{constraint['sourceFaceId']}/{constraint['logicalEdge']}"
        assert key not in actual,('duplicate retained Side constraint',key)
        assert constraint['state']=='RetainedSideDirectionConstraint'
        actual[key]=constraint
    assert set(actual)==set(expected),'retained Side direction constraints changed'
    for key,group in expected.items():
        constraint=actual[key]
        assert constraint['sourceUses']==group['sourceUses'],(key,'Side source uses changed')
        assert constraint['start']==group['start'] and constraint['end']==group['end']
        assert constraint['geometry']==group['geometry'] and constraint['points']==group['points']
        assert constraint['directionSemantics']=='UnorientedGeometricConstraint_NotMachinePose'
        delta=[b-a for a,b in zip(group['start'],group['end'])];length=math.sqrt(sum(x*x for x in delta))
        if group['geometry']=='Line' and length>1e-12:
            expected_direction=[x/length for x in delta]
            assert constraint['candidateDirection'] is not None
            assert all(abs(a-b)<=1e-12 for a,b in zip(expected_direction,constraint['candidateDirection']))
        else:assert constraint['candidateDirection'] is None,'curved Side must not become a fixed axis'

def audit_assembly_rules(data, unit, members):
    """Check removal evidence independently of the assembler's accepted flag."""
    assert unit['mergeRule']=='CoplanarUnitsWithTwoSidedG1SideElimination'
    common_plane=unit['coplanarity']
    assert common_plane['passed'] and set(common_plane['sourceFaceIds'])=={face['faceId'] for face in members}
    assert common_plane['positionToleranceMm']<=1e-7 and common_plane['normalToleranceRadians']<=1e-7
    assert math.isfinite(common_plane['maxPlaneDistanceMm']) and common_plane['maxPlaneDistanceMm']<=common_plane['positionToleranceMm']
    assert math.isfinite(common_plane['maximumNormalAngleRadians']) and common_plane['maximumNormalAngleRadians']<=common_plane['normalToleranceRadians']
    origin=common_plane['referencePlane']['origin'];normal=common_plane['referencePlane']['normal']
    normal_length=math.sqrt(sum(x*x for x in normal))
    assert abs(normal_length-1.)<1e-8
    for member in members:
        evidence=member['planeEvidence']
        assert evidence['isPlanar'] and len(evidence['certificatePoints'])>=3
        assert evidence['method'] and evidence['parameterDomainCovered'] and evidence['positiveWeights']
        for point in evidence['certificatePoints']:
            distance=abs(sum(n*(x-o) for n,x,o in zip(normal,point,origin)))
            assert math.isfinite(distance) and distance<=common_plane['positionToleranceMm'],('whole-unit common plane exceeded',member['faceId'],distance)
        other=evidence['plane']['normal'];other_length=math.sqrt(sum(x*x for x in other))
        assert abs(other_length-1.)<1e-8
        cross=[normal[1]*other[2]-normal[2]*other[1],normal[2]*other[0]-normal[0]*other[2],normal[0]*other[1]-normal[1]*other[0]]
        angle=math.atan2(math.sqrt(sum(x*x for x in cross)),abs(sum(a*b for a,b in zip(normal,other))))
        assert angle<=common_plane['normalToleranceRadians'],('whole-unit plane normal exceeded',member['faceId'],angle)
    boundaries=unit['boundaries'];ends={c['id'] for c in boundaries if c['purpose']=='EndBoundary'}
    cuts={c['id'] for c in boundaries if c['purpose']=='CutBoundary'}
    assert len({c['id'] for c in boundaries})==len(boundaries)
    assert set(unit['endChainIds'])==ends and set(unit['cutChainIds'])==cuts
    assert set(unit['chainIds'])==ends|cuts
    assert all(c['roles']==['Side'] for c in boundaries if c['id'] in ends)
    assert all('Side' not in c['roles'] for c in boundaries if c['id'] in cuts)
    if unit['form']=='OpenBand':
        assert len(boundaries)==4 and len(ends)==len(cuts)==2
        assert unit['eulerCharacteristic']==1 and all(not c['closed'] for c in boundaries)
    elif unit['form']=='ClosedBand':
        assert len(boundaries)==len(cuts)==2 and not ends
        assert unit['eulerCharacteristic']==0
        assert all(c['closed'] and c['roles']==['Skin'] for c in boundaries)
    elif unit['form']=='UnpairedRegion':
        assert not ends and len(cuts)==len(boundaries)
        assert unit['eulerCharacteristic']==2-len(boundaries)
        assert all(c['closed'] and c['roles']==['Skin'] for c in boundaries)
    else:raise AssertionError(('unsupported assembled form',unit['form']))
    checks={frozenset(f"F{s['sourceFaceId']}/{s['logicalEdge']}" for s in decision['sides']):decision
            for decision in data['mergeDecisions'] if decision.get('kind')=='SideElimination' and decision['accepted']}
    ids={face['faceId'] for face in members}
    for seam in unit['internalSeams']:
        check=checks[frozenset(seam['logicalEdges'])]
        assert set(check['sourceFaceIds'])<=ids
        plane=check['coplanarity'];tangent=check['tangency']
        assert plane['passed'] and tangent['passed'],'Side was removed without coplanarity and G1'
        assert plane['positionToleranceMm']<=1e-7
        assert math.isfinite(plane['maxPlaneDistanceMm']) and plane['maxPlaneDistanceMm']<=plane['positionToleranceMm']
        assert tangent['angleToleranceRadians']<=.001 and len(tangent['endpoints'])==2
        assert {endpoint['sourceEndpoint'] for endpoint in tangent['endpoints']}=={'start','end'}
        for endpoint in tangent['endpoints']:
            assert endpoint['passed']
            assert math.isfinite(endpoint['angleRadians']) and endpoint['angleRadians']<=tangent['angleToleranceRadians']
            assert math.isfinite(endpoint['gapMm']) and endpoint['gapMm']<=.001
            first=endpoint['firstTangent'];second=endpoint['secondTangent']
            assert endpoint['tangentDirectionSemantics']=='AwayFromJointIntoEachFace_OppositeMeansG1'
            assert all(math.isfinite(x) for x in first+second)
            assert abs(math.sqrt(sum(x*x for x in first))-1.)<1e-8 and abs(math.sqrt(sum(x*x for x in second))-1.)<1e-8
            measured_angle=math.acos(max(-1.,min(1.,-sum(a*b for a,b in zip(first,second)))))
            assert measured_angle<=tangent['angleToleranceRadians']
            assert abs(measured_angle-endpoint['angleRadians'])<=1e-7
    audit_axis_constraints(unit,members)

def audit(item):
    case_id=item['id'];folder=DEST/'cases'/f'{case_id:03d}';issues=[]
    for key in ['source','originalSource']:assert sha(Path(item[key]))==item['sha256'],(case_id,key,'source changed')
    data=read(folder/'view.json');boundary=read(DEST/'boundaries'/f'{case_id:03d}.json');geometry=read(folder/'geometry.json');result=read(folder/'result.json')
    if item.get('skinMembershipContext'):
        from source_skin_membership import validate_context
        assert boundary['classifySha256']==item['sha256']==item['skinMembershipContext']['classifySha256']
        provenance=validate_context(boundary,item['skinMembershipContext'])
        assert provenance==boundary['skinMembershipContext']==data['skinMembershipContext']
        raw_membership={}
        for wire in boundary['faces']:
            for loop in wire['wires']:
                for parent in loop['groups']:
                    for part in parent.get('roleSegments') or [parent]:
                        key=f"W{loop['wireId']}:G{parent['groupId']}"+(f":S{part['segmentId']}" if part.get('segmentId') else '')
                        if part.get('skinMembership'):raw_membership[(wire['faceId'],key)]=part['skinMembership']
        actual_membership={(face['faceId'],group['key']):group['skinMembership'] for face in data['faces'] for group in face['groups'] if group.get('skinMembership')}
        assert raw_membership==actual_membership,'source skin-membership evidence was lost or changed'
    assert data['boundaryVersion']==4
    assert len(data['faces'])==len(boundary['faces'])==result['faces']
    assert [f['faceId'] for f in data['faces']]==list(range(1,result['faces']+1))
    expected={};meshes=0;chains=0;known_invalid=0
    for f,b,g in zip(data['faces'],boundary['faces'],geometry['faces']):
        assert f['faceId']==b['faceId']==g['faceId']
        assert bool(f['units'])==(f['status']=='organized')
        assert (folder/f['sourceBrep']).stat().st_size>0
        assert f['newBoundaryCount']==0 and f['roundTrip']['region']=='OriginalFaceUnmodified'
        assert f['roundTrip']['valid']==g['valid']
        assert f['roundTrip']['areaDeltaMm2']<=max(1e-7,g['areaMm2']*1e-9)
        known_invalid+=not g['valid']
        source_uses=Counter((w['wireId'],e['edgeId'],e['reversed']) for w in b['wires'] for x in w['groups'] if not x['seam'] and not x['event'] for e in x['sourceUses'])
        # A geometric G1 group can cover different material-contact roles.
        # Role runs partition complete original edge occurrences, without
        # trimming edges, replacing source geometry, or adding face boundaries.
        for wire in b['wires']:
            for parent in wire['groups']:
                segments=parent.get('roleSegments',[])
                if not segments:continue
                assert parent['kind']=='Unresolved' and parent['reason']=='mixed-skin-contact'
                assert not parent['seam'] and not parent['event']
                expected_uses=[(e['edgeId'],e['reversed']) for e in parent['sourceUses']]
                actual_uses=[(e['edgeId'],e['reversed']) for segment in segments for e in segment['sourceUses']]
                assert Counter(actual_uses)==Counter(expected_uses),'role segments duplicate or lose source edges'
                if parent['closed']:
                    assert any(actual_uses==expected_uses[i:]+expected_uses[:i] for i in range(len(expected_uses)))
                else:assert actual_uses==expected_uses,'role segments change source traversal'
                raw_role=lambda segment:segment.get('skinMembership',{}).get('previousKind',segment['kind'])
                assert len({raw_role(segment) for segment in segments})>1
                for segment in segments:
                    assert segment['kind'] in ('Skin','Side','Internal')
                    assert not segment['seam'] and not segment['event']
                    assert len(segment['polyline'])==49*len(segment['sourceUses'])
                    if raw_role(segment)=='Skin':
                        assert segment['allOnSkin'] and segment['maxSkinDistance']<=.001
                    else:
                        assert not segment['allOnSkin']
                        assert segment['bodyStates']['inside']>0
                        assert not segment['bodyStates']['outside'] and not segment['bodyStates']['unknown']
                        if raw_role(segment)=='Side':assert segment['startOnSkin'] or segment['endOnSkin']
        group_uses=Counter((x['wire'],e['edgeId'],e['reversed']) for x in f['groups'] for e in x['sourceUses'])
        chain_uses=Counter((x['wire'],e['edgeId'],e['reversed']) for x in f['chains'] for e in x['sourceUses'])
        assert source_uses==group_uses==chain_uses,(case_id,f['faceId'],'source-use partition')
        assert Counter(k for c in f['chains'] for k in c['logicalEdges'])==Counter(x['key'] for x in f['groups'])
        for c in f['chains']:assert (folder/c['file']).stat().st_size>0;chains+=1
        if f['units']:
            assert len(f['units'])==1 and g['valid']
            u=f['units'][0]
            if u.get('assemblyId'):
                assert f['faceId'] in u['sourceRegion']['faceIds'] and u['sourceRegion']['scope']=='WholeOriginalFaces'
                assert set(u['chainIds'])=={c['id'] for c in u['boundaries']}
            else:
                assert u['sourceRegion']=={'faceId':f['faceId'],'scope':'WholeOriginalFace'}
                assert set(u['chainIds'])=={c['id'] for c in f['chains']}
            assert u['machiningState']=='NotPlanned' and u['pairingState']=='Deferred'
            if not u.get('assemblyId'):audit_axis_constraints(u,[f])
            name=f"F{f['faceId']:04d}_A{u['assemblyId']:03d}_UNIT" if u.get('assemblyId') else f"F{f['faceId']:04d}_U001_UNIT"
        else:
            assert f['reason'];name=f"F{f['faceId']:04d}_PENDING"
        expected[name]=g
        if 'geometry' in f:
            m=f['geometry'];assert m['vertices'] and m['triangles']
            assert all(len(p)==3 and all(math.isfinite(v) for v in p) for p in m['vertices'])
            assert all(len(t)==3 and min(t)>=0 and max(t)<len(m['vertices']) for t in m['triangles'])
            meshes+=1
        else:assert f['meshIssue']
    assemblies=data.get('assembledUnits',[])
    assert result['units']==len(assemblies)+sum(not u.get('assemblyId') for f in data['faces'] for u in f['units'])
    for u in assemblies:
        members=[f for f in data['faces'] if f['faceId'] in u['sourceRegion']['faceIds']]
        assert len(members)==u['roundTrip']['faceCount'] and u['roundTrip']['valid']
        assert (folder/u['file']).stat().st_size>0
        assert u['roundTrip']['areaDeltaMm2']<=max(1e-7,u['areaMm2']*1e-9)
        before=Counter((f['faceId'],e['edgeId']) for f in members for g in f['groups'] for e in g['sourceUses'])
        after=Counter((e['sourceFaceId'],e['edgeId']) for c in u['boundaries'] for e in c['sourceUses'])
        after.update((s['sourceFaceId'],e['edgeId']) for seam in u['internalSeams'] for s in seam['sides'] for e in s['sourceUses'])
        assert before==after
        audit_assembly_rules(data,u,members)
        for c in u['boundaries']:assert (folder/c['file']).stat().st_size>0
        assert all(f['sourceColor']==u['color'] for f in members)
    assert result['pendingFaces']==sum(not f['units'] for f in data['faces'])
    publication={}
    if data.get('wholeModel'):
        whole=data['wholeModel'];display=read(folder/whole['colors'])
        assert sha(folder/whole['file'])==display['brepSha256']
        assert display['sourceSha256']==item['sha256']
        assert display['faceCount']==whole['faceCount']==len(data['faces'])
        assert whole['groupCount']==len(display['groups'])==result['units']+result['pendingFaces']
        assert sorted(fid for g in display['groups'] for fid in g['sourceFaceIds'])==list(range(1,len(data['faces'])+1))
        assert whole['areaDeltaMm2']<=max(1e-7,sum(g['areaMm2'] for g in geometry['faces'])*1e-9)
        for group in display['groups']:
            for fid in group['sourceFaceIds']:assert group['color']==data['faces'][fid-1]['sourceColor']
        assert (folder/whole['bundle']).stat().st_size>0
        publication=audit_inspection_and_publication(item,folder,data,result,display)
    else:raise AssertionError('missing complete whole-model export')
    output=folder/'step-audit.json'
    if not output.exists() or output.stat().st_mtime<max((folder/'colored.step').stat().st_mtime,EXE.stat().st_mtime):
        with (folder/'step-audit.log').open('wb') as log:subprocess.run([str(EXE),str(folder/'colored.step'),str(output)],env=ENV,stdout=log,stderr=log,check=True,timeout=600)
    step=read(output);assert step['faces']==len(expected)==step['coloredFaces']
    assert len(step['entries'])==len(expected)
    assert {x['name'] for x in step['entries']}==set(expected)
    max_delta=0
    for entry in step['entries']:
        g=expected[entry['name']];delta=abs(g['areaMm2']-entry['areaMm2']);max_delta=max(max_delta,delta)
        if delta>max(1e-6,g['areaMm2']*1e-7):issues.append('STEP area drift '+entry['name'])
        if entry['valid']!=g['valid']:issues.append('STEP validity changed '+entry['name'])
        if max(abs(a-b) for a,b in zip(entry['color'],g['color']))>1e-4:issues.append('STEP color changed '+entry['name'])
    return {'id':case_id,'sourceUnchanged':True,'faces':len(expected),'units':result['units'],'chains':chains,'meshes':meshes,'sourceInvalidFaces':known_invalid,'maxStepAreaDeltaMm2':max_delta,**publication,'issues':issues}

def safe(item):
    try:return audit(item)
    except Exception as e:return {'id':item['id'],'issues':[f'{type(e).__name__}: {e}']}

def main():
    sources=read(DEST/'source-manifest.json');manifest=read(DEST/'manifest.json')
    assert {i['id'] for i in sources}=={i['id'] for i in manifest}
    assert {Path(i['originalSource']).name for i in sources}=={p.name for p in (ROOT/'TestData/Tube/Classify').glob('*.step')}
    inventory=audit_published_inventory(sources)
    with ThreadPoolExecutor(max_workers=4) as pool:results=list(pool.map(safe,sources))
    summary={'files':len(results),'filesWithIssues':sum(bool(r['issues']) for r in results),'sourceUnchanged':all(r.get('sourceUnchanged') for r in results),'faces':sum(r.get('faces',0) for r in results),'units':sum(r.get('units',0) for r in results),'chains':sum(r.get('chains',0) for r in results),'meshes':sum(r.get('meshes',0) for r in results),'sourceInvalidFaces':sum(r.get('sourceInvalidFaces',0) for r in results),'publishedInventory':inventory,'results':results}
    write(DEST/'validation.json',summary)
    print({k:v for k,v in summary.items() if k!='results'},flush=True)
    for r in results:
        if r['issues']:print(r,flush=True)
    if summary['filesWithIssues'] or inventory['issues']:raise SystemExit(1)

if __name__=='__main__':main()
