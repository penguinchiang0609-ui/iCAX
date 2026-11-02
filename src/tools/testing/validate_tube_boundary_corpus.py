"""Audit corpus provenance and distinguish a complete run from CAD acceptance."""
import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[3]
def read(path):return json.loads(Path(path).read_text(encoding='utf-8-sig'))
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run',type=Path,default=ROOT/'output/tests/tube-boundary-core-20261030/corpus')
    args=parser.parse_args();run=args.run.resolve();summary=read(run/'summary.json');manifest=read(run/'manifest.json')
    assert summary['status']!='running' and summary['selected']==summary['completed']==len(summary['rows'])
    assert len({r['id'] for r in summary['rows']})==summary['selected']
    for rec in summary['runtime'].values():assert sha(rec['path'])==rec['sha256']
    frozen=read(run/'producer.json')
    for rec in frozen['files'].values():assert sha(rec['path'])==rec['sha256']
    for rec in summary['runOrigins']+[summary['sourceReport']]:assert sha(rec['path'])==rec['sha256']
    catalog={r['id']:r for r in manifest['models']};assert len(catalog)==len(summary['rows'])
    failures=[];counts=Counter();cache_modes=Counter();reference_modes=Counter();artifacts=[]
    for row in summary['rows']:
        assert row['sourceUnchanged'] is True and sha(row['inputBRep'])==row['sourceSha256']
        assert sha(row['healed']['path'])==row['healed']['sha256']
        for rec in row.get('cache',{}).values():assert sha(rec['path'])==rec['sha256']
        file=Path(row['resultFile']);data=read(file)
        assert data['modelId']==row['id'] and data['originalName']==row['group'] and data['status']==row['status']
        assert data['producerEvidence']==summary['runtime'] and catalog[row['id']]['status']==row['status']
        cache_modes[row['upstreamMode']]+=1;reference_modes[data.get('sourceReferenceMethod','not-yet-referenced')]+=1
        if row['status']=='passed':
            assert row['exitCode']==0 and data['coreOnlyRestore'] and data['binaryReopenPassed'] and data['materialEquivalent']
            assert data['areaRelativeError']<1e-6 and data['sourceNeutralUnchanged']
            assert data['restoredSolids']>0 and data['sourceReferenceSolids']>0
            if data.get('sourceReferenceMethod','').startswith('existing-closed-shell'):
                assert data['referenceNoFacesAddedOrRemoved'] and data['referenceExactNativeFaceIdentityPreserved'] and data['referenceNeutralRoundtripPassed']
            core=file.parent/'core.ictcad';brep=file.parent/'whole-model.brep'
            assert core.is_file() and brep.is_file() and core.read_bytes()[:8]==b'ICTCAD05'
            assert core.read_bytes()==(file.parent/'candidate.ictcad').read_bytes()
            assert catalog[row['id']]['wholeModelBrep']==brep.relative_to(run).as_posix()
            artifacts.extend([{'path':str(p),'sha256':sha(p)} for p in [core,brep,file,file.parent/'native-result.json']])
            counts['acceptedModels']+=1;counts['explicitMachiningFaces']+=data['machiningFaces'];counts['restoredSolids']+=data['restoredSolids']
        else:
            assert row.get('error')
            assert data.get('execution',{}).get('error') or data.get('error')
            assert 'wholeModelBrep' not in catalog[row['id']] and 'coreFile' not in catalog[row['id']]
            failures.append({'id':row['id'],'name':row['group'],'stage':row['stage'],'reason':row['error'],'sourceValid':data.get('sourceValid'),
                'sourceSolids':data.get('sourceSolids'),'sourceReferenceSolids':data.get('sourceReferenceSolids'),'machiningFaces':data.get('machiningFaces')})
    assert counts['acceptedModels']==summary['passed'] and len(failures)==summary['failed']
    report={'schema':1,'status':'passed' if not failures else 'completed-with-failures','verifiedAt':datetime.now(timezone.utc).isoformat(),
        'scope':'Complete current-Classify and fixed-kernel boundary-core corpus; reporting/provenance audit passed, failed CAD reconstructions are not accepted',
        'selectedModels':summary['selected'],'completedModels':summary['completed'],**counts,'failedModels':len(failures),
        'reportingChecksPassed':True,'nativeProducerHashesPassed':True,'sourceNeutralUnchanged':True,'upstreamCacheHashesPassed':True,
        'oneCoreAndWholeModelPerAcceptedOriginal':True,'failedCasesNeverUseOldSuccess':True,'upstreamModes':dict(cache_modes),'sourceReferenceModes':dict(reference_modes),
        'failureReasons':dict(Counter(f['reason'] for f in failures)),'failures':failures,'acceptedArtifactHashes':artifacts,
        'summary':{'path':str(run/'summary.json'),'sha256':sha(run/'summary.json')}}
    (run/'validation.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps({k:report[k] for k in ['status','selectedModels','acceptedModels','failedModels','reportingChecksPassed']},ensure_ascii=True))
if __name__=='__main__':main()
