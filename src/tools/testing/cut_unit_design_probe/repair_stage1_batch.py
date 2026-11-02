"""Repair selected failed exports without recomputing successful face organization."""
import argparse
import json
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

import run_stage1_units as runner
import run_unit_inspection as inspection
from publish_stage1_artifacts import publish_case


def repair(item):
    folder = runner.DEST / 'cases' / f"{item['id']:03d}"
    native_log = (folder / 'native.log').read_text('utf-8', errors='replace')
    try:
        geometry = runner.read(folder / 'geometry.json')
        result = runner.read(folder / 'result.json')
        complete_geometry = ('wholeModel' in geometry and result.get('mergeRuleVersion') == 1
                             and len(geometry['faces']) == result['faces'])
    except (ValueError, OSError):
        complete_geometry = False
    if not complete_geometry or any(word in native_log[-500:] for word in ['failed', 'invalid', 'changed']):
        runner.generate(item, reuse=False)
        action = 'RegeneratedWholeModel'
    else:
        inspection.generate_inspection(item, folder)
        publish_case(item, folder)
        # Preserve the actual geometry generator fingerprint; only the display
        # exporter has changed in this repair branch.
        fingerprint = dict(runner.read(runner.DEST / 'batch-fingerprint.json'), source=item['sha256'])
        fingerprint['inspectionExporter'] = runner.sha(inspection.EXE)
        runner.write(folder / 'stamp.json', fingerprint)
        action = 'RepairedInspectionOnly'
    print(f"repaired {item['id']:03d}: {action}", flush=True)
    return {'id': item['id'], 'action': action, 'passed': True}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--only', nargs='+', type=int, required=True)
    parser.add_argument('--inspection-exe', type=Path, required=True)
    parser.add_argument('--exporter-exe', type=Path, required=True)
    parser.add_argument('--workers', type=int, default=3)
    parser.add_argument('--report', default='batch-repair.json')
    args = parser.parse_args()
    runner.EXE = args.exporter_exe.resolve()
    runner.INSPECTION_EXE = inspection.EXE = args.inspection_exe.resolve()
    items = [item for item in runner.read(runner.DEST / 'source-manifest.json') if item['id'] in args.only]
    results = []
    with ThreadPoolExecutor(max_workers=args.workers) as pool:
        futures = {pool.submit(repair, item): item for item in items}
        for future in as_completed(futures):
            try:
                results.append(future.result())
            except Exception as error:
                item = futures[future]
                failure = {'id': item['id'], 'passed': False, 'error': repr(error)}
                results.append(failure)
                print(json.dumps(failure), flush=True)
    runner.write(runner.DEST / args.report, {'selected': len(items), 'results': results})
    if not all(result['passed'] for result in results):
        raise SystemExit(1)


if __name__ == '__main__':
    main()
