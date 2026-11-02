"""Consistency and source-preservation checks for delivered design evidence."""
import hashlib,json,re,sys
from pathlib import Path
sys.stdout.reconfigure(encoding='utf-8')
root=Path(__file__).resolve().parents[4]
out=root/'output/tests/cut-unit-design-20261023'
manifest=json.loads((out/'manifest.json').read_text(encoding='utf-8'))
summary=json.loads((out/'summary.json').read_text(encoding='utf-8'))
assert len(manifest)==270 and len({r['id'] for r in manifest})==270
assert all(r['status']=='passed' for r in manifest)
face_count=0
for row in manifest:
    assert hashlib.sha256(Path(row['source']).read_bytes()).hexdigest()==row['sha256Before'],row['source']
    d=json.loads(Path(row['evidence']).read_text(encoding='utf-8'))
    assert d['originalFaceCount']==d['sewnFaceCount']==len(d['faces'])
    ids={f['id'] for f in d['faces']}
    assert len(ids)==len(d['faces'])
    assert all(f['sourceFaceIndices'] for f in d['faces'])
    for edge in d['edges']:
        assert set(edge['faces'])<=ids
        assert len(edge['samples'])==edge['sampleCount']
        assert 0<=edge['profileSamplesOn']<=edge['sampleCount']
        for relation in edge['relations']:
            assert relation['a'] in ids and relation['b'] in ids
    face_count+=len(d['faces'])
assert face_count==summary['faceCount']==14757
fixtures=json.loads((out/'analytic-checks.json').read_text(encoding='utf-8'))
assert len(fixtures)==5 and all(f['passed'] for f in fixtures)
for p in [out/'实样分析报告.md',root/'doc/iCAX-Engine/Products/TubeCAM/03-Classify后切割单元与刀路生成详细方案.md']:
    text=p.read_text(encoding='utf-8')
    assert text.count('```')%2==0
    for link in re.findall(r'\]\(([^)]+)\)',text):
        if not link.startswith(('http:','https:','#')):
            assert (p.parent/link).resolve().exists(),(str(p),link)
result={'passed':True,'sourceFilesUnchanged':270,'faceRecords':face_count,'analyticCasesPassed':5,'documentLinksValid':True,'scope':'evidence consistency, not toolpath acceptance'}
(out/'validation.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(result,ensure_ascii=False,indent=2))
