"""Strictly assemble unique native evidence without rerunning unchanged path cases."""
from copy import deepcopy
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
from AllProductsWindowRecheck import OUTPUT, source_snapshot


def read(path):
    return json.loads(path.read_text(encoding='utf-8'))


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    snapshot = source_snapshot()
    original_file = OUTPUT / 'all-products-recheck-window-native-before-polygon-resource-fix.json'
    original = read(original_file)
    path_file = OUTPUT / 'all-products-recheck-window-native-existing-path-output-preservation.json'
    preservation = read(path_file)
    assert preservation['passed'] and preservation['count'] == 11
    assert preservation['sourceHashes'] == snapshot and preservation['sourceHashesUnchangedDuringRun']
    assert preservation['shiftedOriginalCurrentStockMatricesExactlyEqual']
    cases = [deepcopy(case) for case in original['cases'] if case['passed']]
    assert len(cases) == 11
    for case in cases:
        case['nativeEvidenceSourcePhase'] = 2
        case['nativeEvidenceReport'] = str(original_file)
        case['nativeEvidenceReportHash'] = digest(original_file)
        case['currentEntirePublicOutputsDeepEqualOriginal'] = True
    new_names = ('inactive-external-three-spatial_v_notch',
        'shifted-asymmetric-datum-segment_weld', 'sharp-rect-zero-radius-segment_weld')
    for name in new_names:
        target = OUTPUT / ('all-products-recheck-window-native-' + name + '.json')
        current = read(target)
        assert current['passed'] and len(current['cases']) == 1
        case = deepcopy(current['cases'][0])
        assert case['passed'] and case['name'] == name
        assert case['hostParametersEqual'] and case['exactSaveReopenPreserved']
        assert case['shapes']['parts'] == 4 and case['shapes']['persistedPoints'] > 0
        assert case['shapes']['classifierReuseCheckedAgainstFreshPerState']
        if name == 'inactive-external-three-spatial_v_notch':
            assert any(key.startswith('outer_frame.spatial.continuous') for key in case['shapes']['itemKeys'])
        else:
            assert case['referencePolygonControls']['negativeVertexControlDistinguishable']
            assert case['shapes']['changedNativePointControl']['strictComparisonRejected']
            assert case['shapes']['initialStockCoordinateConvention']['publicNormalizerAppliedToBothWithActualSavedProperties']
        case['nativeEvidenceSourcePhase'] = 4
        case['nativeEvidenceReport'] = str(target)
        case['nativeEvidenceReportHash'] = digest(target)
        cases = [row for row in cases if row['name'] != name]
        cases.append(case)
    assert len(cases) == 13 and len({case['name'] for case in cases}) == 13
    execution_scans = []
    for case in cases:
        assert all(case[field] for field in ('passed', 'hostParametersEqual',
            'nativePreviewAndManufacturingAccepted', 'sharedReferencesConsumed', 'exactSaveReopenPreserved'))
        assert all(case['shapes'][field] for field in ('volumesBoundsSolidsEqual',
            'classificationsEqual', 'classifierReuseCheckedAgainstFreshPerState'))
        target = OUTPUT / ('all-products-recheck-window-native-' + case['name'] + '-actual-execution.json')
        actual = read(target)
        nodes = [node for node in actual['resources'] if node['operator'] == 'profile2d']
        assert nodes and all(contour['kind'] == 'path' for node in nodes for contour in node['arguments']['contours'])
        execution_scans.append({'name': case['name'], 'actualSavedExecutionFile': str(target),
            'actualSavedExecutionHash': digest(target), 'profile2dNodes': len(nodes), 'allGraphContoursGenericPath': True})
    assert snapshot == source_snapshot()
    report = {'schema': original['schema'], 'schemaVersion': 1, 'passed': True,
        'prepareOnly': False, 'runtimeRoot': original['runtimeRoot'],
        'finishedAt': datetime.now(timezone.utc).isoformat(), 'cases': cases,
        'uniqueCases': 13, 'phase2InheritedUnchangedPathCases': 10,
        'phase4FreshNativeCases': 3, 'sourceHashes': snapshot,
        'sourceHashesUnchangedDuringAggregation': True,
        'unchangedPathPublicOutputProof': str(path_file), 'actualExecutionGenericProfileScans': execution_scans,
        'provenance': 'Real native lifecycle evidence from phases 2 and 4. Unchanged path outputs were independently deep-compared; the spatial continuous frame, shifted polygon datum and zero-radius polygon received fresh phase-4 native acceptance.'}
    target = OUTPUT / 'all-products-recheck-window-native.json'
    target.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({'passed': True, 'uniqueCases': 13,
        'parts': sum(case['parts'] for case in cases),
        'representativeParts': sum(case['shapes']['parts'] for case in cases),
        'persistedPoints': sum(case['shapes']['persistedPoints'] for case in cases),
        'freshClassifierControls': sum(case['shapes']['freshClassifierControls'] for case in cases),
        'report': str(target)}))


if __name__ == '__main__':
    main()
