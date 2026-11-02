"""Validate the delivered face inventory, native evidence and local report links."""
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import sys
from run_face_combinations import ROOT, OUT, NAMES, face_row

sys.stdout.reconfigure(encoding='utf-8')

def read(path):
    return json.loads(path.read_text(encoding='utf-8'))

def main():
    manifest=read(OUT/'manifest.json');rows=read(OUT/'faces.json');summary=read(OUT/'summary.json')
    source_paths={Path(r['source']).resolve() for r in manifest}
    actual_paths={p.resolve() for p in (ROOT/'TestData/Tube/Classify').iterdir() if p.suffix.lower() in {'.step','.stp','.brep','.igs','.iges'}}
    assert source_paths==actual_paths, ('source inventory mismatch',source_paths^actual_paths)
    assert len(manifest)==270 and all(r['status']=='diagnosed' for r in manifest)
    regenerated=[];seams=events=uses=groups=0
    for item in manifest:
        assert hashlib.sha256(Path(item['source']).read_bytes()).hexdigest()==item['sha256Before'],item['source']
        data=read(Path(item['evidence']));assert data['version']==3
        for face in data['faces']:
            regenerated.append(face_row(item,data,face))
            for wire in face['wires']:
                assert wire['rawEdgeUses']==wire['expectedEdgeUses']==sum(len(g['sourceUses']) for g in wire['groups'])
                uses+=wire['rawEdgeUses'];groups+=len(wire['groups'])
                seams+=sum(g['seam'] for g in wire['groups']);events+=sum(g['event'] for g in wire['groups'])
    assert sorted(regenerated,key=lambda r:(r['caseId'],r['faceId']))==rows
    complete=[r for r in rows if r['complete']];incomplete=[r for r in rows if not r['complete']]
    assert len(rows)==14757 and len(complete)==14310 and len(incomplete)==447
    counts=Counter(r['combination'] for r in complete)
    assert counts==summary['completeCounts'] and set(counts)==set(NAMES.values())
    assert sum(counts.values())+len(incomplete)==len(rows)
    reason_faces=Counter(reason for row in incomplete for reason in row['issues'])
    reason_groups=Counter()
    for row in incomplete:reason_groups.update(row['issues'])
    assert reason_groups==summary['unresolvedReasons']
    assert reason_faces=={'outside-section-material':235,'mixed-skin-contact':269}
    assert sum(len(row['issues'])==2 for row in incomplete)==57
    zero=[r for r in rows if abs(r['area'])<1e-10]
    assert len(zero)==4 and sum(r['complete'] and r['mask']==2 for r in zero)==2
    checks=read(OUT/'checks.json');assert len(checks)==9 and all(x['passed'] for x in checks)
    rulings=read(OUT/'ruling-checks.json');assert {r['mask'] for r in rulings}==set(range(1,8))
    for r in rulings:
        stem=OUT/'examples'/f"combination-{r['mask']}"
        for suffix in ['.brep','.step','.json','.mesh.json','.png','.plan.json','.segments.txt','.ruling-check.json']:
            assert stem.with_suffix(suffix).stat().st_size>0,str(stem)+suffix
        exported=read(stem.with_suffix('.json'));choice=summary['examples'][str(r['mask'])][0]
        assert exported['faces'][0]['faceId']==choice['faceId'] and exported['file']==choice['file']
        native=read(stem.with_suffix('.ruling-check.json'))
        assert len(native['lines'])==17 and native['maxSampledDistanceMm']<1e-6
        assert all(x['queryFailures']==0 for x in native['lines'])
        assert native['maxSampledDistanceMm']==r['maxSampledDistanceMm']
    multi=read(OUT/'multiwire-checks.json')
    assert [r['maxIntervalsAtOneStation'] for r in multi]==[4,2]
    assert all(r['testedStations']==499 and r['oddIntersectionStations']==0 for r in multi)
    for name in ['skin-multiple-loops','three-roles-eight-holes','nonruled-four-groups']:
        for suffix in ['.brep','.step','.json','.mesh.json','.png','.plan.json']:
            assert (OUT/'examples'/f'{name}{suffix}').stat().st_size>0
    curved=read(OUT/'examples/nonruled-four-groups.ruling-check.json')
    assert .18060<curved['maxSampledDistanceMm']<.18062 and all(x['queryFailures']==0 for x in curved['lines'])
    report=OUT/'组合与切割单元分析.md'
    links=re.findall(r'\]\(([^)]+)\)',report.read_text(encoding='utf-8'))
    for link in links:
        if link=='validation.json':continue
        assert (report.parent/link).is_file(),link
    result={'status':'passed','sourceFiles':len(manifest),'sourceHashesUnchanged':len(manifest),'faces':len(rows),
        'completeFaces':len(complete),'unresolvedFaces':len(incomplete),'completeCounts':dict(counts),
        'rawEdgeUses':uses,'logicalGroupsIncludingEventsAndSeams':groups,'seamGroups':seams,'degenerateEventGroups':events,
        'zeroAreaRecords':[{'file':r['file'],'faceId':r['faceId'],'combination':r['combination']} for r in zero],
        'analyticChecks':len(checks),'representativeCandidatesChecked':len(rulings),
        'candidateMaxSampledDistanceMm':max(r['maxSampledDistanceMm'] for r in rulings),
        'initialBsplinePairingMaxSampledDistanceMm':curved['maxSampledDistanceMm'],
        'reportLinksChecked':len(links),'scope':'source integrity, inventory and sampled native diagnostics; not machining qualification'}
    (OUT/'validation.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(result,ensure_ascii=False,indent=2))

if __name__=='__main__':main()
