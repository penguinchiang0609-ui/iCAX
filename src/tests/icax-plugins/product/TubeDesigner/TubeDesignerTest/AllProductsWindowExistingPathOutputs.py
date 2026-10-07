"""Read-only proof that the polygon dialect fix leaves prior path cases identical."""
from copy import deepcopy
import json
from pathlib import Path
from AllProductsWindowRecheck import CORE, OUTPUT, source_snapshot, forbid_product_processing, forbid_display_callback


def main():
    snapshot = source_snapshot()
    original = json.loads((OUTPUT / 'all-products-recheck-window-native-before-polygon-resource-fix.json').read_text(encoding='utf-8'))
    results = []
    for case in original['cases']:
        if not case['passed']:
            continue
        reference_file = Path(case['sourceReferenceFile'])
        if case['name'] == 'inactive-external-three-spatial_v_notch':
            reference_file = OUTPUT / ('all-products-recheck-window-native-' + case['name']
                + '-before-spatial-member-extension-source-reference.json')
        reference = json.loads(reference_file.read_text(encoding='utf-8'))
        parameters = deepcopy(reference['parameters'])
        frozen = deepcopy(parameters)
        profiles = [node for node in reference['display']['resources'] if node['operator'] == 'profile2d']
        assert profiles and all(curve['kind'] == 'path' for node in profiles for curve in node['arguments']['contours'])
        with forbid_product_processing():
            display = CORE.display(parameters)
            with forbid_display_callback():
                manufacturing = CORE.manufacturing(parameters)
        assert display == reference['display'], case['name'] + ' entire display changed'
        assert manufacturing == reference['manufacturing'], case['name'] + ' raw manufacturing changed'
        assert parameters == frozen
        results.append({'name': case['name'], 'passed': True,
            'previousActualSourceReference': str(reference_file), 'entireDisplayDeepEqual': True,
            'entireManufacturingDeepEqual': True, 'hostParametersUnchanged': True,
            'originalProfile2dNodes': len(profiles), 'allOriginalGraphProfileContoursAlreadyPath': True})
    assert len(results) == 11
    shifted_name = 'shifted-asymmetric-datum-segment_weld'
    shifted_reference = json.loads((OUTPUT / ('all-products-recheck-window-native-' + shifted_name + '-source-reference.json')).read_text(encoding='utf-8'))
    shifted_actual = json.loads((OUTPUT / ('all-products-recheck-window-native-' + shifted_name + '-actual-execution.json')).read_text(encoding='utf-8'))
    old_stocks = shifted_reference['document']['extensions']['tubeDesigner.assemblyProcessSource']['stocks']
    actual_stocks = {stock['id']: stock for stock in shifted_actual['extensions']['tubeDesigner.assemblyProcessSource']['stocks']}
    assert all(stock['matrix'] == actual_stocks[stock['id']]['matrix'] for stock in old_stocks)
    unchanged = snapshot == source_snapshot()
    assert unchanged
    report = {'schema': 'icax.window-existing-path-output-preservation', 'passed': True,
        'sourcePhase': 4, 'readOnly': True, 'cases': results, 'count': len(results),
        'sourceHashes': snapshot, 'sourceHashesUnchangedDuringRun': unchanged,
        'shiftedOriginalCurrentStockMatricesExactlyEqual': True,
        'shiftedOriginalStockMatricesCompared': len(old_stocks),
        'nativeResults': 'Original 11 real native lifecycle cases retained with their actual stage provenance; no new native run asserted here'}
    target = OUTPUT / 'all-products-recheck-window-native-existing-path-output-preservation.json'
    target.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({'passed': True, 'count': len(results), 'sourceDependencies': len(snapshot), 'report': str(target)}))


if __name__ == '__main__':
    main()
