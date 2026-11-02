"""Publish whole-model results beside Classify, using the original model name."""
import argparse
import hashlib
import json
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
CACHE = ROOT / 'output/tests/cut-unit-stage1-20261025'
OUTPUT = ROOT / 'TestData/Tube/CutUnits'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read(path):
    return json.loads(path.read_text('utf-8'))


def publish_case(item, folder):
    case = read(folder / 'view.json')
    whole = case['wholeModel']
    display = read(folder / whole['colors'])
    assert digest(Path(item['originalSource'])) == item['sha256'], 'Classify source changed'
    assert display['sourceSha256'] == case['sourceHash'] == item['sha256']
    assert digest(folder / whole['file']) == display['brepSha256']
    assert display['faceCount'] == case['faceCount']
    assert sorted(fid for group in display['groups'] for fid in group['sourceFaceIds']) == list(range(1, case['faceCount'] + 1))

    OUTPUT.mkdir(parents=True, exist_ok=True)
    stem = Path(item['file']).stem
    files = {'brep': stem + '.brep', 'step': stem + '.step', 'display': stem + '.json'}
    inspection = read(folder / 'inspection-audit.json') if (folder / 'inspection-audit.json').exists() else None
    step = folder / 'colored.step'
    if inspection:
        assert inspection['passed'] and inspection['sourceHash'] == item['sha256']
        assert inspection['originalFaces'] == case['faceCount']
        step = folder / 'inspection.step'
        assert digest(step) == inspection['inspectionSha256']
        display.update(stepPurpose='InspectionWithSeparateDisplayMarkers', displayOnlyGeometry=True,
                       markerGroups=inspection['markerGroups'], inspectionAudit=inspection)
    for source, name in [(folder / whole['file'], files['brep']), (step, files['step'])]:
        target = OUTPUT / name
        staging = target.with_name(target.name + '.tmp')
        shutil.copy2(source, staging)
        assert digest(staging) == digest(source), 'Published geometry changed'
        staging.replace(target)

    display.update(brep=files['brep'], coloredStep=files['step'], caseId=item['id'],
                   unitCount=case['unitCount'], pendingFaces=case['pendingFaces'],
                   mergeRuleVersion=case.get('mergeRuleVersion',1),mergePolicy=case.get('mergePolicy'),
                   mergeReview=case.get('mergeDecisions',[]),
                   sourcePath='TestData/Tube/Classify/' + item['file'],
                   coloredStepSha256=digest(OUTPUT / files['step']))
    target = OUTPUT / files['display']
    staging = target.with_name(target.name + '.tmp')
    staging.write_text(json.dumps(display, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    staging.replace(target)
    return files


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--only', nargs='+', type=int)
    args = parser.parse_args()
    count = 0
    for item in read(CACHE / 'source-manifest.json'):
        if args.only and item['id'] not in args.only:
            continue
        folder = CACHE / 'cases' / f"{item['id']:03d}"
        if not (folder / 'cut-units.brep').exists():
            if args.only:
                raise RuntimeError(f"Case {item['id']} has no whole-model export")
            continue
        files = publish_case(item, folder)
        print(files['brep'], flush=True)
        count += 1
    print(f'Published {count} whole models to {OUTPUT}', flush=True)


if __name__ == '__main__':
    main()
