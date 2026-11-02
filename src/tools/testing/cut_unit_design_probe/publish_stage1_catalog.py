"""Publish review information for the complete, already exported Classify corpus."""
import argparse
import csv
import json
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path

from publish_stage1_artifacts import CACHE, OUTPUT, digest, read


REASONS = {
    'boundary-unresolved': '存在无法确定角色的边界',
    'boundary-chain-gap': '边界之间存在间隙',
    'no-skin-anchor': '没有皮上边界，需要解释入口与终止关系',
    'boundary-organization-ambiguous': '边界组织尚未确定',
    'invalid-source-brep': '原始面未通过几何检查',
    'zero-area': '源面面积为零',
    'wire-edge-coverage-incomplete': '边界环存在未覆盖的原始边',
}


def save_json(path, data):
    temporary = path.with_name(path.name + '.tmp')
    temporary.write_text(json.dumps(data, ensure_ascii=False, indent=2) + '\n', 'utf-8')
    temporary.replace(path)


def save_csv(path, rows, fields):
    with path.open('w', encoding='utf-8-sig', newline='') as output:
        writer = csv.DictWriter(output, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def review_from_case(case):
    faces, units = [], {}
    for face in case['faces']:
        unit = face['units'][0] if face['units'] else None
        key = face.get('assemblyKey') or (f"F{face['faceId']:04d}_U001" if unit else '')
        faces.append({
            'sourceFaceId': face['faceId'], 'status': face['status'],
            'unitKey': key, 'reason': face['reason'],
            'reasonText': REASONS.get(face['reason'], face['reason']),
            'decision': face['decision'], 'roles': face['roles'],
            'logicalEdgeCount': face['logicalEdgeCount'], 'boundarySegmentCount': len(face['groups']),
            'geometricGroups': face.get('geometricGroups',[]), 'meshIssue': face.get('meshIssue', ''),
            'sourceSkinMembership': [{'key':g['key'],'evidence':g['skinMembership']} for g in face['groups'] if g.get('skinMembership')],
            'boundaryIssues': face['issues'],
            'axisConstraints': face.get('axisConstraints', []),
            'planeEvidence': face.get('planeEvidence'),
        })
        if unit and key not in units:
            units[key] = {
                'key': key, 'form': unit['form'],
                'sourceFaceIds': unit['sourceRegion'].get('faceIds', [face['faceId']]),
                'boundaries': [{k: chain[k] for k in ['id', 'purpose', 'closed', 'roles', 'logicalEdges', 'sourceUses']}
                               for chain in unit.get('boundaries', face['chains'])],
                'internalSeams': [{k: seam[k] for k in ['id', 'purpose', 'logicalEdges', 'sides']}
                                  for seam in unit.get('internalSeams', [])],
                'decision': unit['decision'],
                'axisConstraints': unit.get('axisConstraints', []),
            }
    return faces, list(units.values())


def validate_review_cache(item, metadata, counts):
    prefix = f"{item['file']}: "
    for key in ['faceReview', 'unitReview', 'inspectionAudit']:
        assert key in metadata, prefix + 'missing published cache: ' + key
    assert metadata['caseId'] == item['id'], prefix + 'caseId differs from manifest'
    assert metadata['sourceFile'] == item['file'], prefix + 'source file differs from manifest'
    assert metadata['sourceSha256'] == item['sha256'], prefix + 'sourceSha256 differs from manifest'
    assert metadata['faceCount'] == counts['faces'], prefix + 'faceCount differs from manifest or view'
    assert metadata['unitCount'] == counts['units'], prefix + 'unitCount differs from manifest or view'
    assert metadata['pendingFaces'] == counts['pendingFaces'], prefix + 'pendingFaces differs from manifest or view'
    faces, units, audit = metadata['faceReview'], metadata['unitReview'], metadata['inspectionAudit']
    assert isinstance(faces, list) and len(faces) == counts['faces'], prefix + 'incomplete faceReview cache'
    assert isinstance(units, list) and len(units) == counts['units'], prefix + 'incomplete unitReview cache'
    assert [face['sourceFaceId'] for face in faces] == list(range(1, counts['faces'] + 1)), prefix + 'faceReview identity changed'
    assert all(face['status'] in ('organized','pending') for face in faces), prefix + 'unknown cached face status'
    assert sum(face['status'] == 'pending' for face in faces) == counts['pendingFaces'], prefix + 'cached pending count differs'
    by_key = {unit['key']: unit for unit in units}
    assert len(by_key) == len(units) and all(by_key), prefix + 'duplicate or empty cached unit key'
    owners = {}
    for face in faces:
        if face['status'] == 'pending':
            assert not face['unitKey'], prefix + 'pending face has a cached unit owner'
        else:
            assert face['unitKey'] in by_key, prefix + 'organized face has no cached unit'
            owners.setdefault(face['unitKey'], []).append(face['sourceFaceId'])
    assert set(owners) == set(by_key), prefix + 'cached unit has no source faces'
    for key, unit in by_key.items():
        assert sorted(unit['sourceFaceIds']) == owners[key], prefix + 'cached source-face ownership differs'
    merged = sum(len(unit['sourceFaceIds']) > 1 for unit in units)
    assert merged == counts['mergedUnits'], prefix + 'cached merged-unit count differs'
    assert audit['passed'] is True, prefix + 'cached inspection audit did not pass'
    assert audit['caseId'] == item['id'] and audit['sourceFile'] == item['file'], prefix + 'cached inspection identity differs'
    assert audit['sourceHash'] == item['sha256'], prefix + 'cached inspection source hash differs'
    assert audit['originalFaces'] == counts['faces'], prefix + 'cached inspection face count differs'
    assert audit['unitGroups'] == counts['units'] + counts['pendingFaces'], prefix + 'cached inspection group count differs'
    assert audit['inspectionSha256'] == metadata['coloredStepSha256'] == metadata['inspectionSha256'], prefix + 'cached inspection STEP hash differs'
    return faces, units, audit


def retained_full_audit(report):
    if not report.is_file():
        return None
    previous = read(report)
    if 'fullMetadataAudit' in previous:
        return previous['fullMetadataAudit']
    if 'targetedMetadataRefresh' in previous:
        return None
    # A legacy catalog was generated by the full path, which checked every
    # published BREP/STEP hash. Retain that past run, rather than relabeling it
    # as a fresh all-model verification after a targeted refresh.
    return {'scope':'FullPublishedGeometryHashes',
            'recordedAt':datetime.fromtimestamp(report.stat().st_mtime,timezone.utc).isoformat(),
            'models':previous['models'],'originalFaces':previous['originalFaces'],
            'cutUnits':previous['cutUnits'],'pendingFaces':previous['pendingFaces'],
            'brepFiles':previous['brepFiles'],'stepFiles':previous['stepFiles'],
            'evidence':'PreviouslyPublishedFullCatalog'}


def main(only=None):
    sources = read(CACHE / 'source-manifest.json')
    selected = None if only is None else set(only)
    if selected is not None:
        assert selected, 'empty targeted metadata selection'
        assert selected <= {item['id'] for item in sources}, 'targeted caseId is absent from source manifest'
        result_rows = read(CACHE / 'manifest.json')
        counts_by_id = {item['id']: item for item in result_rows}
        assert len(counts_by_id) == len(result_rows), 'duplicate caseId in result manifest'
        assert set(counts_by_id) == {item['id'] for item in sources}, 'result manifest inventory differs from source manifest'
        previous_audit = retained_full_audit(OUTPUT / '检查清单/summary.json')
    models, pending, all_units, marker_warnings = [], [], [], []
    metadata_writes = []
    for item in sources:
        folder = CACHE / 'cases' / f"{item['id']:03d}"
        stem = Path(item['file']).stem
        metadata_path = OUTPUT / (stem + '.json')
        assert metadata_path.is_file(), f"{item['file']}: missing published metadata cache"
        metadata = read(metadata_path)
        if selected is None or item['id'] in selected:
            case = read(folder / 'view.json')
            assert digest(OUTPUT / (stem + '.brep')) == metadata['brepSha256']
            assert digest(OUTPUT / (stem + '.step')) == metadata['coloredStepSha256']
            assert case['sourceHash'] == metadata['sourceSha256'] == item['sha256']
            counts = {'faces':case['faceCount'],'units':case['unitCount'],
                      'pendingFaces':case['pendingFaces'],'mergedUnits':len(case['assembledUnits'])}
            if selected is not None:
                indexed = counts_by_id[item['id']]
                assert indexed['file'] == item['file'], f"{item['file']}: result manifest identity differs"
                assert all(indexed[key] == value for key,value in counts.items()), f"{item['file']}: result manifest counts differ from view"
            faces, units = review_from_case(case)
            audit = read(folder / 'inspection-audit.json')
            metadata.update(faceReview=faces, unitReview=units, inspectionAudit=audit)
            faces, units, audit = validate_review_cache(item,metadata,counts)
            if selected is None:
                save_json(metadata_path,metadata)
            else:
                metadata_writes.append((metadata_path,metadata))
        else:
            counts = counts_by_id[item['id']]
            assert counts['file'] == item['file'], f"{item['file']}: result manifest identity differs"
            assert (OUTPUT / (stem + '.brep')).is_file() and (OUTPUT / (stem + '.step')).is_file(), f"{item['file']}: missing cached geometry file"
            faces, units, audit = validate_review_cache(item,metadata,counts)
        for face in faces:
            if face['status'] == 'pending':
                pending.append({'模型编号': item['id'], '模型': item['file'],
                                '原面编号': 'F' + str(face['sourceFaceId']),
                                '原因': REASONS.get(face['reason'], face['reason']),
                                '原因代码': face['reason'], '说明': face['decision']})
        warnings = audit.get('displayStrokeFallbacks', [])
        warning_count = len(warnings)
        if warning_count:
            marker_warnings.append({'caseId': item['id'], 'file': item['file'],
                                    'warnings': warnings})
        models.append({'模型编号': item['id'], '模型': item['file'], '原面数': counts['faces'],
                       '切割单元数': counts['units'], '待处理面数': counts['pendingFaces'],
                       '合并单元数': counts['mergedUnits'], '显示标记说明数': warning_count,
                       '完整BREP': stem + '.brep', '着色STEP': stem + '.step', '映射数据': stem + '.json'})
        for unit in units:
            all_units.append({'模型编号': item['id'], '模型': item['file'], '切割单元': unit['key'],
                              '原面编号': '、'.join('F' + str(fid) for fid in unit['sourceFaceIds']),
                              '形式': unit['form'], '外部边界数': len(unit['boundaries']),
                              '内部接缝数': len(unit['internalSeams'])})
    report = OUTPUT / '检查清单'
    report.mkdir(exist_ok=True)
    summary = {'mergeRuleVersion':2,'mergePolicy':'CoplanarUnitsAndBothCutBoundaryJoinsG1',
               'models': len(models), 'originalFaces': sum(m['原面数'] for m in models),
               'cutUnits': sum(m['切割单元数'] for m in models), 'pendingFaces': len(pending),
               'mergedUnits': sum(m['合并单元数'] for m in models),
               'pendingReasons': dict(Counter(row['原因代码'] for row in pending)),
               'displayWarnings': marker_warnings,
               'brepFiles': len(list(OUTPUT.glob('*.brep'))),
               'stepFiles': len(list(OUTPUT.glob('*.step'))), 'modelsIndex': models}
    assert summary['brepFiles'] == summary['stepFiles'] == len(sources)
    if selected is None:
        summary['fullMetadataAudit'] = {'scope':'FullPublishedGeometryHashes',
            'recordedAt':datetime.now(timezone.utc).isoformat(),
            **{key:summary[key] for key in ['models','originalFaces','cutUnits','pendingFaces','brepFiles','stepFiles']},
            'evidence':'CurrentFullCatalogRun'}
    else:
        summary['fullMetadataAudit'] = previous_audit
        summary['targetedMetadataRefresh'] = sorted(selected)
        summary['metadataVerificationScope'] = {
            'geometryHashesVerifiedCaseIds':sorted(selected),
            'reusedPublishedMetadataCaseIds':[item['id'] for item in sources if item['id'] not in selected],
            'unselectedGeometryHashesRechecked':False,
            'unselectedAuditMeaning':'Retained previously passed inspectionAudit and published review caches'}
    # Targeted refreshes reject any missing/stale unselected cache before
    # replacing selected metadata or the global reports.
    for metadata_path,metadata in metadata_writes:
        save_json(metadata_path,metadata)
    save_json(report / 'summary.json', summary)
    for name, rows, fields in [
        ('模型清单.csv', models, ['模型编号','模型','原面数','切割单元数','待处理面数','合并单元数','显示标记说明数','完整BREP','着色STEP','映射数据']),
        ('切割单元.csv', all_units, ['模型编号','模型','切割单元','原面编号','形式','外部边界数','内部接缝数']),
        ('待处理面.csv', pending, ['模型编号','模型','原面编号','原因','原因代码','说明']),
    ]:
        save_csv(report / name, rows, fields)
    print(json.dumps({key: value for key, value in summary.items()
                      if key not in ['modelsIndex', 'displayWarnings', 'metadataVerificationScope']}, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--only',nargs='+',type=int,help='Refresh only these case IDs; reuse the other published review caches.')
    main(parser.parse_args().only)
