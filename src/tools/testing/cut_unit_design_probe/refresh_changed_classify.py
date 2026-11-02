"""Stage and audit changed Classify files before replacing their inspection outputs."""
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime
import argparse, hashlib, shutil
from pathlib import Path
import run_classify_units as r
import validate_classify_units as validation


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--stage');args=parser.parse_args()
    destination = r.DEST
    manifest = r.read(destination / 'source-manifest.json')
    changed = [x for x in manifest if sha(x['originalSource']) != x['sha256Before']]
    print('Changed inputs: ' + ', '.join(str(x['id']) for x in changed), flush=True)
    if not changed:
        return
    stamp = datetime.now().strftime('%Y%m%d-%H%M%S')
    staging = Path(args.stage).resolve() if args.stage else destination / ('refresh-' + stamp)
    assert staging.resolve().is_relative_to(destination.resolve())
    archive = destination / ('previous-' + stamp)
    for sub in ['inputs', 'boundaries']:
        (staging / sub).mkdir(parents=True, exist_ok=True)
    r.DEST = staging

    def prepare(item):
        key = f"{item['id']:03d}"
        source = Path(item['originalSource'])
        snapshot = staging / 'inputs' / (key + '.step')
        cache=staging/(key+'-prepared.json')
        if cache.exists():
            saved=r.read(cache)
            if sha(source)==sha(snapshot)==saved['sha256Before']:
                print('Reusing audited staged case '+key,flush=True)
                return saved
        reuse = snapshot.exists() and sha(snapshot) == sha(source)
        shutil.copy2(source, snapshot)
        assert sha(snapshot) == sha(source), ('source-changing', key)
        row, data = r.probe(snapshot, staging / 'boundaries' / (key + '.json'), timeout=600)
        assert data, row
        data['file'] = source.name
        r.write(Path(row['evidence']), data)
        row.update(id=item['id'], originalSource=str(source), file=source.name)
        print(f"Boundary {key}: {len(data['faces'])} faces", flush=True)
        result, _ = r.run(row, reuse, .01)
        assert result['status'] == 'processed', result
        audit = validation.audit(result)
        assert not audit['issues'], audit
        assert sha(source) == row['sha256Before'], ('source-changing', key)
        print(f"Audited {key}: {result['faces']} faces, {result['units']} candidates, {result['pendingFaces']} pending", flush=True)
        r.write(cache,row)
        return row

    with ThreadPoolExecutor(max_workers=4) as pool:
        prepared = list(pool.map(prepare, changed))
    # Every replacement was generated and read back before touching published outputs.
    for row in prepared:
        assert sha(row['originalSource']) == row['sha256Before']
    archive.mkdir()
    shutil.copy2(destination / 'source-manifest.json', archive / 'source-manifest.json')
    for row in prepared:
        key = f"{row['id']:03d}"
        for relative in [Path('inputs') / (key + '.step'), Path('boundaries') / (key + '.json'), Path('cases') / key]:
            old = (destination / relative).resolve()
            backup = (archive / relative).resolve()
            fresh = (staging / relative).resolve()
            assert all(p.is_relative_to(destination.resolve()) for p in [old, backup, fresh])
            backup.parent.mkdir(parents=True, exist_ok=True)
            old.rename(backup)
            fresh.rename(old)
        row.update(source=str(destination / 'inputs' / (key + '.step')), evidence=str(destination / 'boundaries' / (key + '.json')))
    replacements = {x['id']: x for x in prepared}
    r.write(destination / 'source-manifest.json', [replacements.get(x['id'], x) for x in manifest])
    r.write(destination / 'source-refresh.json', {'ids': sorted(replacements), 'archive': str(archive), 'sourceManifestUpdated': True})
    print('Published changed cases; rebuild the full index with reindex_classify_units.py.', flush=True)


if __name__ == '__main__':
    main()
