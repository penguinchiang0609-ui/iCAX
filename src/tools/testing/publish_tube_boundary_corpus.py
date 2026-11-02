"""Publish a completed boundary-core run: one document and whole BREP per model."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
from urllib.parse import quote
from datetime import datetime, timezone

ROOT=Path(__file__).resolve().parents[3]
def read(path):return json.loads(Path(path).read_text(encoding='utf-8-sig'))
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def write(path,data):Path(path).write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run',type=Path,default=ROOT/'output/tests/tube-boundary-core-20261030/corpus')
    parser.add_argument('--publish',type=Path,default=ROOT/'TestData/Tube/BoundaryCore')
    args=parser.parse_args();run=args.run.resolve();publish=args.publish.resolve()
    assert publish.name=='BoundaryCore' and publish.parent==(ROOT/'TestData/Tube').resolve()
    summary=read(run/'summary.json');assert summary['status']!='running' and summary['completed']==summary['selected']
    assert len({r['id'] for r in summary['rows']})==summary['selected']
    for record in summary['runtime'].values():assert sha(record['path'])==record['sha256']
    parts=read(ROOT/'TestData/Tube/Reports/summary.json')['parts'];groups=[p['group'] for p in parts]
    def stem(group):
        item=Path(group);base=item.stem
        count=sum(Path(g).stem.lower()==base.lower() for g in groups)
        return base+'__'+item.suffix[1:].lower() if count>1 else base
    publish.mkdir(exist_ok=True);archive=run/'previous-publication';archive.mkdir(exist_ok=True)
    prior_manifest=read(publish/'manifest.json') if (publish/'manifest.json').exists() else None
    prior_models={model['id']:model for model in prior_manifest['models']} if prior_manifest else {}
    prior_evidence=None
    if prior_manifest:
        old_sha=sha(publish/'manifest.json');old=archive/('manifest-'+old_sha[:12]+'.json')
        if not old.exists():shutil.copy2(publish/'manifest.json',old)
        prior_evidence={'path':str(old),'sha256':old_sha}
        for model in prior_models.values():
            if 'producerEvidence' not in model:
                model['producerEvidence']=read(publish/model['dataFile']).get('producerEvidence')
    manifest={'schema':1,'kind':'tube-boundary-core','publishedAt':datetime.now(timezone.utc).isoformat(),
        'scope':'Complete current-Classify/core run; explicit failures remain inspectable' if summary['selected']==len(groups) else 'Verified baseline plus selected-model repairs; per-model producer evidence retained',
        'runSummary':{'path':str(run/'summary.json'),'sha256':sha(run/'summary.json')},
        'runtime':summary['runtime'],'priorManifest':prior_evidence,'selectedInLatestRun':summary['selected'],'models':[]}
    files=[]
    for row in summary['rows']:
        source=Path(row['inputBRep'])
        if row['status']=='passed':assert sha(source)==row['sourceSha256'] and row.get('sourceUnchanged') is True
        case=Path(row['resultFile']).parent;data=read(case/'result.json');name=stem(row['group'])
        assert data['modelId']==row['id'] and data['status']==row['status']
        for ext in ['.json','.ictcad','.candidate.ictcad','.brep']:
            target=publish/(name+ext)
            assert target.parent==publish
            if target.exists():
                old=archive/target.name
                if not old.exists():shutil.copy2(target,old)
                target.unlink()
        model={'id':row['id'],'name':row['group'],'status':row['status'],'stage':row.get('stage'),'error':row.get('error'),
            'dataUrl':'/data/'+quote(name+'.json'),'dataFile':name+'.json','machiningFaces':data.get('machiningFaces'),
            'inputSha256':row.get('sourceSha256'),'sourceUnchanged':row.get('sourceUnchanged'),'producerEvidence':data.get('producerEvidence',summary['runtime'])}
        if row['status']=='passed':
            assert data.get('coreOnlyRestore') and data.get('binaryReopenPassed') and data.get('materialEquivalent')
            for source_name,ext,key in [('core.ictcad','.ictcad','coreFile'),('whole-model.brep','.brep','wholeModelBrep')]:
                original=case/source_name;assert original.is_file();target=publish/(name+ext);shutil.copy2(original,target)
                model[key]=target.name;files.append({'path':str(target),'sha256':sha(target)})
        elif (case/'candidate.ictcad').is_file():
            target=publish/(name+'.candidate.ictcad');shutil.copy2(case/'candidate.ictcad',target)
            model['candidateCore']=target.name;files.append({'path':str(target),'sha256':sha(target)})
        data['publication']={'status':row['status'],'wholeModelOnly':True,'modelFiles':model,
            'candidateIsNotAcceptedCad':row['status']!='passed'}
        target=publish/(name+'.json');write(target,data);files.append({'path':str(target),'sha256':sha(target)});prior_models[row['id']]=model
    manifest['models']=sorted(prior_models.values(),key=lambda model:model['id'])
    manifest['passed']=sum(m['status']=='passed' for m in manifest['models']);manifest['failed']=len(manifest['models'])-manifest['passed']
    write(publish/'manifest.json',manifest)
    write(run/'publication.json',{'status':'passed','selected':len(manifest['models']),'acceptedCoreModels':manifest['passed'],'failedModels':manifest['failed'],'files':files,
        'manifest':{'path':str(publish/'manifest.json'),'sha256':sha(publish/'manifest.json')},'oneCoreAndWholeModelPerOriginal':True,'oldSuccessNotReused':True})
    print(json.dumps({'published':len(manifest['models']),'passed':manifest['passed'],'failed':manifest['failed'],'directory':str(publish)}))
if __name__=='__main__':main()
