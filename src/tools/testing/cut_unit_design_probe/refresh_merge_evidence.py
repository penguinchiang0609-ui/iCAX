"""Add source-hash-bound whole-face plane certificates to existing logical edges."""
import argparse
import hashlib
import json
import subprocess
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

from run_face_combinations import ROOT, ENV

DEST = ROOT / 'output/tests/cut-unit-stage1-20261025'
EXE = ROOT / 'output/tests/cut-unit-design-20261023/build/Release/face_merge_evidence.exe'


def read(path):
    return json.loads(path.read_text('utf-8'))


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def update(item):
    source = Path(item['source'])
    assert digest(source) == digest(Path(item['originalSource'])) == item['sha256']
    folder = DEST / 'merge-evidence'
    target = folder / f"{item['id']:03d}.json"
    with target.with_suffix('.log').open('wb') as log:
        subprocess.run([str(EXE), str(source), str(target)], env=ENV,
                       stdout=log, stderr=log, check=True, timeout=120)
    evidence = read(target)
    evidence.update(sourceHash=item['sha256'], exporterHash=digest(EXE))
    target.write_text(json.dumps(evidence, ensure_ascii=False, separators=(',', ':')), 'utf-8')
    boundary_path = DEST / 'boundaries' / f"{item['id']:03d}.json"
    boundary = read(boundary_path)
    assert evidence['faceCount'] == len(boundary['faces'])
    by_id = {face['faceId']: face for face in evidence['faces']}
    assert set(by_id) == {face['faceId'] for face in boundary['faces']}
    for face in boundary['faces']:
        face['planeEvidence'] = dict(by_id[face['faceId']], sourceHash=item['sha256'])
    boundary['mergeEvidence'] = {'version': evidence['version'], 'sourceHash': item['sha256'],
                               'planarityToleranceMm': evidence['planarityToleranceMm'],
                               'exporterHash': evidence['exporterHash']}
    temporary = boundary_path.with_suffix('.json.tmp')
    temporary.write_text(json.dumps(boundary, ensure_ascii=False, separators=(',', ':')), 'utf-8')
    temporary.replace(boundary_path)
    assert digest(source) == item['sha256']
    print(f"plane {item['id']:03d}: {evidence['planarFaceCount']}/{evidence['faceCount']}", flush=True)
    return {'id': item['id'], 'faces': evidence['faceCount'], 'planarFaces': evidence['planarFaceCount']}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--only', nargs='+', type=int)
    parser.add_argument('--workers', type=int, default=4)
    args = parser.parse_args()
    (DEST / 'merge-evidence').mkdir(exist_ok=True)
    items = [item for item in read(DEST / 'source-manifest.json')
             if not args.only or item['id'] in args.only]
    results = []
    with ThreadPoolExecutor(max_workers=args.workers) as pool:
        for future in as_completed([pool.submit(update, item) for item in items]):
            results.append(future.result())
    (DEST / 'merge-evidence' / 'summary.json').write_text(json.dumps(
        {'models': len(results), 'results': results}, ensure_ascii=False, indent=2), 'utf-8')


if __name__ == '__main__':
    main()
