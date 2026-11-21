"""Audit current skin-only paths, retained boundary evidence and published files."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path

REPO=Path(__file__).resolve().parents[3]
def read(path): return json.loads(Path(path).read_text(encoding='utf-8-sig'))
def sha(path):
    digest=hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda:stream.read(1024*1024),b''): digest.update(block)
    return digest.hexdigest()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run',type=Path,default=REPO/'output/tests/machining-path-analysis-20261105')
    parser.add_argument('--publish',type=Path,default=REPO/'TestData/Tube/PathAnalysis')
    options=parser.parse_args()
    check_count=0;failure_count=0;failures=[];summary={}
    totals={key:0 for key in ('wholeFaces','machiningFaces','edgeOccurrences','pathOccurrences','sideOccurrences','convexEvidenceOccurrences','ready','pendingParallel','unsupported','invalid')}
    endpoint_issues=[];endpoint_totals={'invalid':0,'onUnselectedPath':0,'onSide':0,'missingPosition':0,'missingTangent':0}
    def check(ok,name):
        nonlocal check_count,failure_count
        check_count+=1
        if not ok:
            failure_count+=1
            if len(failures)<200:failures.append(name)
        return bool(ok)
    def file_matches(item,label):
        try:return check(sha(item['path'])==item['sha256'],label)
        except (OSError,KeyError,TypeError,ValueError) as error:return check(False,f'{label}: {error}')
    def inspect_row(row,entries):
        label=f"model {row['id']}";entry=entries[str(row['id'])]
        check(row['status'] in ('passed','partial','failed','not-run'),f'{label}: recognized current status')
        check(entry['status']==row['status'],f'{label}: manifest status matches current run')
        check(sha(row['source'])==row['sourceSha256'] and row['sourceUnchanged'] is True,f'{label}: original CAD bytes unchanged')
        file_matches(row['artifacts']['data'],f'{label}: published JSON matches run')
        published=read(row['artifacts']['data']['path']);run=published['run'];artifacts=published['artifacts']
        check(run['status']==row['status'],f'{label}: JSON status matches current run')
        check(run['producer']==summary['producer'],f'{label}: JSON records this producer')
        check(run['sourceSha256']==row['sourceSha256'],f'{label}: JSON records this original source')
        check(published['stage']=='pathAnalysis' and published.get('analysisGranularity')=='logical-edge-parts',f'{label}: correct logical-edge stage')
        for key in totals:totals[key]+=published['counts'].get(key,0)
        available=artifacts.get('artifactAvailable',{})
        if row['status']=='not-run':
            check(not published['success'] and not published['complete'] and not published['faces'] and not published['edges'],f'{label}: unavailable upstream has no invented analysis')
            check(row.get('nativeInvoked') is False and run.get('nativeInvoked') is False and row.get('producedThisRun') is False and run.get('producedThisRun') is False,f'{label}: not-run has no native invocation or output')
            check(bool(row.get('pendingReason')) and run.get('pendingReason')==row['pendingReason'] and not row.get('error') and not run.get('error'),f'{label}: not-run records an upstream reason rather than an execution failure')
            check(not available.get('brep') and not available.get('step') and not artifacts.get('geometryFromCurrentRun') and 'brep' not in row['artifacts'] and 'step' not in row['artifacts'],f'{label}: not-run exposes no stale geometry')
            check(not Path(row['reportFile']).exists() and row.get('code') is None,f'{label}: not-run has no current native report or exit code')
            return
        faces_by_id={}
        for face in published['faces']:
            face_id=str(face['faceId'])
            check(face_id not in faces_by_id,f'{label}/face {face_id}: unique source face')
            faces_by_id[face_id]=face
            check(len(face.get('pathKeys',[]))==len(set(face.get('pathKeys',[]))),f'{label}/face {face_id}: unique owned path keys')
        edges_by_key={}
        for edge in published['edges']:
            face_id=str(edge['faceId']);path_key=edge['pathKey'];edge_label=f'{label}/{path_key}'
            check(face_id in faces_by_id and faces_by_id.get(face_id,{}).get('machining') is True,f'{edge_label}: owner is an existing machining face')
            check(path_key.startswith(f'F{face_id}:'),f'{edge_label}: key retains owner faceId')
            check(path_key not in edges_by_key,f'{edge_label}: unique face-edge occurrence');edges_by_key[path_key]=edge
            check(path_key in faces_by_id.get(face_id,{}).get('pathKeys',[]),f'{edge_label}: owner face lists this path')
            uses=edge.get('sourceUses',[]);endpoints=edge.get('endpoints',[]);segments=edge.get('previewSegments',[])
            all_skin=bool(uses) and all(use.get('skinValid') is True and use.get('skinRelation')=='OnSkin' for use in uses)
            all_convex=bool(uses) and all(use.get('convexityValid') is True and use.get('convexity')=='Convex' for use in uses)
            convex_evidence=any(use.get('skinValid') is True and use.get('skinRelation')!='OnSkin' and use.get('convexityValid') is True and use.get('convexity')=='Convex' for use in uses)
            expected_side=bool(uses) and not edge.get('closed',False) and not all_skin and all(use.get('skinValid') is True for use in uses) and (uses[0].get('startOnSkin') is True or uses[-1].get('endOnSkin') is True)
            check(edge['path']==edge['onSkinPath']==all_skin,f'{edge_label}: only wholly OnSkin source spans select an actual path')
            check(edge['convexPath']==all_convex and edge.get('convexEvidence')==convex_evidence,f'{edge_label}: per-source convex facts are retained independently of path selection')
            check(edge['side']==expected_side and not (edge['side'] and edge['skinRelation']=='OnSkin'),f'{edge_label}: side uses the actual logical endpoints and excludes whole-skin paths')
            if not edge['path']:check(edge['axisMode']=='None',f'{edge_label}: non-skin reference has no path axis mode')
            check('samples' not in edge and 'edgeId' not in edge and len(endpoints)==2,f'{edge_label}: only two endpoint records and no intermediate analysis samples')
            check([e.get('endpoint') for e in endpoints]==['start','end'],f'{edge_label}: traversal start/end order retained')
            check(bool(uses) and len(segments)==len(uses),f'{edge_label}: complete original curve chain retained')
            if uses and len(endpoints)==2:
                start,end=endpoints;first,last=uses[0],uses[-1]
                check(start['sourceEdgeId']==first['edgeId'] and end['sourceEdgeId']==last['edgeId'],f'{edge_label}: endpoint sources belong to the whole logical edge ends')
                check(start['parameter']==(first['lastParameter'] if first['reversed'] else first['firstParameter']) and end['parameter']==(last['firstParameter'] if last['reversed'] else last['lastParameter']),f'{edge_label}: exact native endpoint parameters follow traversal')
            for use,segment in zip(uses,segments):check(use['edgeId']==segment['edgeId'] and use['spanKey']==segment['spanKey'] and segment['previewOnly'] is True,f'{edge_label}: exact source curve retained only as display preview')
            for endpoint in endpoints:
                if not edge['path']:check(endpoint['axisValid'] is False and endpoint['axis'] is None,f'{edge_label}: non-skin boundary never invents a walking axis')
                if endpoint['status']=='ParallelToGenerator':check(not endpoint['axisValid'],f'{edge_label}: generator-parallel endpoint has no invented axis')
                if not endpoint['positionValid']:check(endpoint['position'] is None,f'{edge_label}: invalid position is not a fake origin')
                if not endpoint['tangentValid']:check(endpoint['tangent'] is None,f'{edge_label}: invalid tangent is explicit')
                if endpoint['axisValid']:
                    check(edge['path'] and endpoint['positionValid'] and endpoint['tangentValid'],f'{edge_label}: valid axis has an actual skin path, position and tangent')
                    check(abs(sum(x*x for x in endpoint['axis'])**.5-1)<1e-6,f'{edge_label}: tool axis is unit length')
                invalid=endpoint.get('status')=='InvalidGeometry' or not endpoint.get('positionValid') or not endpoint.get('tangentValid')
                if invalid:
                    endpoint_totals['invalid']+=1;endpoint_totals['onUnselectedPath']+=int(not edge['path']);endpoint_totals['onSide']+=int(edge['side'])
                    endpoint_totals['missingPosition']+=int(not endpoint.get('positionValid'));endpoint_totals['missingTangent']+=int(not endpoint.get('tangentValid'))
                    if len(endpoint_issues)<1000:endpoint_issues.append({'modelId':row['id'],'name':row['name'],'pathKey':path_key,'faceId':edge['faceId'],'sourceEdgeIds':edge['sourceEdgeIds'],'path':edge['path'],'side':edge['side'],'edgeStatus':edge['status'],'endpoint':endpoint})
        for face_id,face in faces_by_id.items():
            for key in face.get('pathKeys',[]):check(key in edges_by_key and str(edges_by_key.get(key,{}).get('faceId'))==face_id,f'{label}/face {face_id}/{key}: owned path exists and points back')
        blocked={(b['faceId'],b['wireId']) for b in published.get('unresolvedBoundaries',[])}
        for edge in published['edges']:check((edge['faceId'],edge['wireId']) not in blocked,f"{label}/{edge['pathKey']}: unresolved wire has no invented logical path")
        for boundary in published.get('unresolvedBoundaries',[]):check(boundary['isPath'] is False and boundary['hasToolAxes'] is False,f"{label}/{boundary['boundaryKey']}: unresolved boundary retained as source fact only")
        if run.get('producedThisRun') is True:
            check(row.get('nativeInvoked') is True and run.get('nativeInvoked') is True,f'{label}: current output records its native invocation')
            check(published['geometry']['completeModel'] and len(published['faces'])==published['counts']['wholeFaces'],f'{label}: complete model face map')
            display=published['display']
            check(display['pathColor']=='#1fe05c' and display['axisColor']=='#f4df91' and display['sideColor']=='#ff303b',f'{label}: green path, pale-yellow axis and red side colors')
            check(display['allComputedEndpointsPreserved'] and display['bothRolesRetained'],f'{label}: endpoints and boundary roles retained')
            check(published.get('selectionRule')=='OnSkin-only' and published.get('convexEdgesAreEvidenceOnly') is True,f'{label}: export declares the skin-only selection contract')
            for field,predicate in (('edgeOccurrences',lambda edge:True),('pathOccurrences',lambda edge:edge['path']),('sideOccurrences',lambda edge:edge['side']),('convexEvidenceOccurrences',lambda edge:edge['convexEvidence'])):
                check(published['counts'].get(field)==sum(int(predicate(edge)) for edge in published['edges']),f'{label}: {field} matches actual boundary records')
        if row['status'] in ('passed','partial'):
            complete=row['status']=='passed'
            check(row.get('code')==(0 if complete else 1) and not row.get('timedOut') and row.get('nativeReportStatus')==row['status'],f'{label}: native exit and report match completion state')
            native_report=read(row['reportFile']);native_stage=native_report['stages']['pathAnalysis']
            check(native_report['status']==native_stage['status']==row['status'] and native_stage['serviceSuccess'] is True and native_stage['analysisComplete'] is complete,f'{label}: saved native stage confirms service success and completion')
            check(published['success'] is True and published['complete'] is complete and run.get('producedThisRun') is True,f'{label}: current service/completion state is explicit')
            if not complete:check(bool(run.get('pendingReason')) or bool(published.get('diagnostics')) or bool(blocked),f'{label}: incomplete result has a concrete diagnostic')
            check(artifacts['completeModelBrep'] and artifacts['brepRoundTripPassed'] and artifacts['stepRoundTrip']['passed'],f'{label}: complete BREP and styled STEP round trips passed')
            expected_axes=sum(int(e['path'] and endpoint['axisValid']) for e in published['edges'] for endpoint in e['endpoints'])
            check(artifacts['stepRoundTrip'].get('axisOverlays')==expected_axes and artifacts['stepRoundTrip'].get('yellowAxisOverlays')==expected_axes,f'{label}: STEP axes come only from two skin-path endpoints')
            for kind in ('brep','step'):file_matches(row['artifacts'][kind],f'{label}: published {kind} matches current replay')
        else:
            check(not published['success'] and bool(published.get('error')),f'{label}: execution failure is explicit')
            if available.get('brep') or available.get('step'):
                check(available.get('brep') and available.get('step') and artifacts['geometryFromCurrentRun'],f'{label}: failed analysis exposes only verified current complete geometry')
                check(artifacts['brepRoundTripPassed'] and artifacts['stepRoundTrip']['passed'],f'{label}: current geometry round trips passed')
                for kind in ('brep','step'):
                    file_matches(row['artifacts'][kind],f'{label}: failed-result {kind} matches current run')
                    check(row['artifacts'][kind]['currentGeometryOnly'],f'{label}: {kind} is marked as current geometry only')
            else:check('brep' not in row['artifacts'] and 'step' not in row['artifacts'],f'{label}: missing current geometry exposes no old accepted files')
    try:
        summary=read(options.run/'summary.json');manifest=read(options.publish/'manifest.json')
        entries={str(m['id']):m for m in manifest['models']}
        check(summary['status'] in ('passed','completed-with-failures','completed-with-limitations'),'native run completed')
        check(summary['completed']==summary['selected']==len(summary['rows']),'every selected model has a final state')
        check(len({str(row['id']) for row in summary['rows']})==len(summary['rows']),'selected model IDs are unique')
        for status,field in (('passed','passed'),('partial','partial'),('failed','failed'),('not-run','notRun')):check(summary.get(field,0)==sum(row['status']==status for row in summary['rows']),f'{status}: summary count matches current rows')
        current_summary=(options.run/'summary.json').resolve()
        check(any((REPO/relative).resolve()==current_summary for relative in manifest.get('latestRepair',{}).get('summaries',[])),'manifest indexes this exact run summary for same-producer upstream inspection')
        for name,producer in summary['producer'].items():file_matches(producer,f'{name}: producer unchanged')
        for row in summary['rows']:
            try:inspect_row(row,entries)
            except (OSError,KeyError,TypeError,ValueError,IndexError) as error:check(False,f"model {row.get('id','?')}: malformed or unreadable current evidence: {error}")
        for evidence_name,field in (('producer-files.json','files'),('loaded-runtime.json','modules')):
            evidence_file=options.run/evidence_name
            if evidence_file.exists():
                for item in read(evidence_file)[field]:file_matches(item,f"{evidence_name}: {item['path']} remains unchanged")
    except (OSError,KeyError,TypeError,ValueError,IndexError) as error:check(False,f'Cannot audit run evidence: {error}')
    report={'schema':1,'status':'failed' if failure_count else 'passed','selected':summary.get('selected',0),'accepted':summary.get('passed',0),'partial':summary.get('partial',0),'failed':summary.get('failed',0),'notRun':summary.get('notRun',0),
        'totals':totals,'endpointInvalid':endpoint_totals,'endpointInvalidRecords':endpoint_issues,'endpointInvalidRecordsTruncated':endpoint_totals['invalid']>len(endpoint_issues),
        'checks':check_count,'failureCount':failure_count,'failures':failures,'failuresTruncated':failure_count>len(failures)}
    options.run.mkdir(parents=True,exist_ok=True)
    (options.run/'validation.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print(f"Audit {report['status']}: {report['selected']} models, {report['accepted']} accepted, {report['partial']} partial, {report['failed']} failed, {report['notRun']} not-run; {check_count} checks, {failure_count} invariant failures")
    for failure in failures[:10]:print(f'  {failure}')
    return int(bool(failure_count))

if __name__=='__main__':raise SystemExit(main())
