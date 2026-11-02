"""Create a fresh, independently inspectable first-stage cut-unit dataset."""
import argparse,hashlib,json,shutil,subprocess,sys,time,csv,zipfile
from pathlib import Path
from collections import Counter
from concurrent.futures import ThreadPoolExecutor,as_completed
from run_face_combinations import ROOT,ENV,probe,EXE as BOUNDARY_EXE
from cut_unit_stage1 import analyze_case
from publish_stage1_artifacts import publish_case
from run_unit_inspection import generate_inspection, package_inspection, matches_exporter, EXE as INSPECTION_EXE

sys.stdout.reconfigure(encoding='utf-8')
HERE=Path(__file__).resolve().parent
DEST=ROOT/'output/tests/cut-unit-stage1-20261025'
OLD=ROOT/'output/tests/cut-unit-design-20261023/classify-cut-units'
EXE=OLD.parent/'build/Release/export_stage1_units.exe'

def read(p):return json.loads(p.read_text('utf-8'))
def write(p,data):p.write_text(json.dumps(data,ensure_ascii=False,separators=(',',':')),encoding='utf-8')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def control(case,path):
    lines=[str(len(case['faces']))]
    for f in case['faces']:
        lines.append(f"{f['faceId']} {int(bool(f['units']))} {len(f['chains'])}")
        for c in f['chains']:
            lines.append(f"{c['id']} {len(c['sourceUses'])}")
            lines.extend(f"{e['edgeId']} {int(e['reversed'])}" for e in c['sourceUses'])
    lines.append(str(len(case['assembledUnits'])))
    for u in case['assembledUnits']:
        fids=u['sourceRegion']['faceIds'];lines.append(f"{u['assemblyId']} {len(fids)} {len(u['boundaries'])}")
        lines.append(' '.join(str(fid) for fid in fids))
        for c in u['boundaries']:
            lines.append(f"{c['id']} {len(c['sourceUses'])} {int(c['closed'])}")
            lines.extend(f"{e['edgeId']} {int(e['reversed'])}" for e in c['sourceUses'])
    path.write_text('\n'.join(lines)+'\n','utf-8')


def native_plan(case):
    def chain(c):return (c['id'],c['closed'],c['sourceUses'])
    return {'faces':[(f['faceId'],bool(f['units']),f['units'][0].get('assemblyId',0) if f['units'] else 0,
                      [chain(c) for c in f['chains']]) for f in case['faces']],
            'assemblies':[(u['assemblyId'],u['sourceRegion']['faceIds'],[chain(c) for c in u['boundaries']])
                          for u in case['assembledUnits']]}


def verified_preview_cache(item,folder,previous,geometry):
    if not previous or previous.get('sourceHash')!=item['sha256'] or not geometry:return None
    assert geometry['previewDeflectionMm']==.03
    assert len(previous['faces'])==len(geometry['faces'])
    lines=[json.dumps(item['sha256']),str(len(previous['faces']))]
    for face,g in zip(previous['faces'],geometry['faces']):
        assert face['faceId']==g['faceId']
        mesh=g.get('mesh','');issue=g.get('meshIssue','')
        if mesh:
            assert read(folder/mesh)==face['geometry'],'preview cache differs from verified source-face view'
        else:assert issue and 'geometry' not in face
        lines.append(f"{g['faceId']} {json.dumps(mesh)} {json.dumps(issue,ensure_ascii=False)} {g['areaMm2']:.17g} {int(g['valid'])}")
    path=folder/'verified-preview-cache.txt';path.write_text('\n'.join(lines)+'\n','utf-8')
    return path

def prepare(item):
    key=f"{item['id']:03d}";source=Path(item['originalSource']);snapshot=DEST/'inputs'/f'{key}.step'
    current=sha(source)
    classifier_hash=sha(BOUNDARY_EXE)
    membership_context=item.get('skinMembershipContext')
    if snapshot.exists() and sha(snapshot)==current and (DEST/'boundaries'/f'{key}.json').exists():
        data=read(DEST/'boundaries'/f'{key}.json')
        if (data.get('version')==4 and data.get('boundaryClassifierSha256')==classifier_hash
                and data.get('classifySha256')==current):
            if membership_context:
                cached_context=data.get('skinMembershipContext') or {}
                if all(cached_context.get(k)==v for k,v in membership_context.items()):
                    from source_skin_membership import validate_context
                    validate_context(data,membership_context)
                    return dict(item,source=str(snapshot),sha256=current)
            elif not data.get('skinMembershipContext'):
                return dict(item,source=str(snapshot),sha256=current)
    shutil.copy2(source,snapshot);assert sha(snapshot)==sha(source)
    target=DEST/'boundaries'/f'{key}.json'
    result,data=probe(snapshot,target,timeout=600)
    assert data,result
    data['file']=source.name;data['boundaryClassifierSha256']=classifier_hash;data['classifySha256']=current
    if membership_context:
        from source_skin_membership import apply_context
        data['skinMembershipContext']=apply_context(data,membership_context)
    write(target,data)
    assert sha(source)==current,'source-changed-during-preparation'
    print(f"boundary {key}: {len(data['faces'])} faces",flush=True)
    return dict(item,source=str(snapshot),sha256=current)

def generate(item,reuse=False):
    key=f"{item['id']:03d}";folder=DEST/'cases'/key;folder.mkdir(parents=True,exist_ok=True)
    assert sha(Path(item['source']))==item['sha256']
    fingerprint={'source':item['sha256'],'boundary':sha(DEST/'boundaries'/f'{key}.json'),'analysis':sha(HERE/'cut_unit_stage1.py'),'assembly':sha(HERE/'cut_unit_assembly.py'),'exporter':sha(EXE),'packager':sha(Path(__file__)),'publisher':sha(HERE/'publish_stage1_artifacts.py'),'inspection':sha(HERE/'run_unit_inspection.py'),'inspectionExporter':sha(INSPECTION_EXE)}
    if (reuse and (folder/'stamp.json').exists() and read(folder/'stamp.json')==fingerprint
            and (folder/'inspection-audit.json').exists()
            and matches_exporter(read(folder/'inspection-audit.json'),fingerprint['inspectionExporter'])):
        publish_case(item,folder)
        return read(folder/'result.json')
    previous=read(folder/'view.json') if (folder/'view.json').exists() else None
    previous_geometry=read(folder/'geometry.json') if previous and (folder/'geometry.json').exists() else None
    boundary=read(DEST/'boundaries'/f'{key}.json')
    assert boundary.get('mergeEvidence',{}).get('sourceHash')==item['sha256'],'missing current whole-face plane certificates'
    case=analyze_case(boundary);case['caseId']=item['id'];case['sourceHash']=item['sha256']
    case['mergeRuleVersion']=2
    case['mergePolicy']={'name':'CoplanarUnitsAndBothCutBoundaryJoinsG1',
                         'sideMatchToleranceMm':.001,'planeToleranceMm':1e-7,'tangentToleranceRadians':.001,
                         'failedChecks':'KeepSideAndDirectionConstraint'}
    write(folder/'analysis.json',case);control(case,folder/'controls.txt')
    same_plan=bool(previous and previous_geometry and previous.get('sourceHash')==item['sha256'] and native_plan(previous)==native_plan(case))
    if same_plan:
        previous_display=read(folder/previous['wholeModel']['colors'])
        assert sha(folder/previous['wholeModel']['file'])==previous_display['brepSha256']
    else:
        cache=verified_preview_cache(item,folder,previous,previous_geometry)
        with (folder/'native.log').open('wb') as log:
            subprocess.run([str(EXE),item['source'],str(folder/'controls.txt'),str(folder)]+([str(cache)] if cache else []),env=ENV,stdout=log,stderr=log,check=True,timeout=600)
    geometry=read(folder/'geometry.json');assert len(geometry['faces'])==len(case['faces'])
    old_item=next((r for r in read(OLD/'source-manifest.json') if r['file']==item['file']),None)
    old_native=None
    if old_item and old_item['sha256Before']==item['sha256']:
        old_native=read(OLD/'cases'/f"{old_item['id']:03d}"/'native.json')
    legacy={f['faceId']:f for f in old_native['faces']} if old_native else {}
    for f,g in zip(case['faces'],geometry['faces']):
        assert f['faceId']==g['faceId']
        f['sourceBrep']=g['brep'];f['roundTrip']=g['roundTrip'];f['meshIssue']=g.get('meshIssue','')
        if not g['valid']:
            f['units']=[];f['status']='pending';f['reason']='invalid-source-brep';f['decision']='原始面 BREP 校验未通过，保留原面供检查。'
        if g.get('mesh'):
            f['geometry']=read(folder/g['mesh'])
        for u in f['units']:
            if not u.get('assemblyId'):u.update(file=g['brep'],color=g['color'],areaMm2=g['areaMm2'])
        f['sourceColor']=g['color']
        for c in f['chains']:c['file']=g['chainFiles'].get(str(c['id']))
        old_face=legacy.get(f['faceId']);f['previousUnitCount']=len(old_face['units']) if old_face else None
        if old_face and len(old_face['units'])>1:
            (folder/'legacy').mkdir(exist_ok=True)
            old_folder=OLD/'cases'/f"{old_item['id']:03d}"
            old_pieces=[{'id':u['id'],'color':u['color'],'geometry':read(old_folder/u['mesh'])} for u in old_face['units']]
            write(folder/'legacy'/f"F{f['faceId']:04d}.json",old_pieces)
            f['comparison']=f"legacy/F{f['faceId']:04d}.json"
    native_assemblies={g['assemblyId']:g for g in geometry['assembledUnits']}
    for u in case['assembledUnits']:
        g=native_assemblies[u['assemblyId']];u.update(file=g['brep'],color=g['color'],areaMm2=g['areaMm2'],roundTrip=g['roundTrip'])
        for c in u['boundaries']:c['file']=g['chainFiles'][str(c['id'])]
    all_units=case['assembledUnits']+[u for f in case['faces'] for u in f['units'] if not u.get('assemblyId')]
    case.update(faceCount=len(case['faces']),unitCount=len(all_units),pendingFaces=sum(not f['units'] for f in case['faces']))
    case['wholeModel']=geometry['wholeModel']
    display=read(folder/case['wholeModel']['colors'])
    display.update(sourceFile=item['file'],sourceSha256=item['sha256'],brepSha256=sha(folder/case['wholeModel']['file']),
        skinMembershipContext=case.get('skinMembershipContext'),
        edgeStyles={'Skin':{'color':'#50d8e3','widthPx':3.5,'pattern':'solid'},'Side':{'color':'#ffac58','widthPx':1.8,'pattern':'dashed'},
            'Internal':{'color':'#ce91ff','widthPx':1.25,'pattern':'solid'},'Unresolved':{'color':'#ff6e83','widthPx':1.8,'pattern':'dashed'},
            'InternalSeam':{'color':'#d7b9df','widthPx':1.2,'pattern':'dashed','defaultVisible':False}})
    for group in display['groups']:
        group['faces']=[]
        owner=case['faces'][group['sourceFaceIds'][0]-1]
        group['axisConstraints']=(owner['units'][0].get('axisConstraints',[]) if owner['units'] else owner.get('axisConstraints',[]))
        for index,fid in enumerate(group['sourceFaceIds'],1):
            f=case['faces'][fid-1];seams={ref for u in f['units'] for seam in u.get('internalSeams',[]) for ref in seam['logicalEdges']}
            group['faces'].append({'faceIndexInGroup':index,'sourceFaceId':fid,'logicalEdges':[
                {'key':g['key'],'geometryGroupKey':g.get('geometryGroupKey',g['key']),
                 'sourceRole':g['role'],'unitRole':'InternalSeam' if f"F{fid}/{g['key']}" in seams else g['role'],'sourceStepEdgeIds':g['sourceEdges'],
                 'skinMembership':g.get('skinMembership')}
                for g in f['groups']]})
    write(folder/case['wholeModel']['colors'],display)
    case['wholeModel']['bundle']='whole-model.zip'
    with zipfile.ZipFile(folder/'whole-model.zip','w',compression=zipfile.ZIP_DEFLATED) as archive:
        for file in [case['wholeModel']['file'],case['wholeModel']['colors'],'colored.step']:archive.write(folder/file,file)
        archive.writestr('README.txt',f"模型：{item['file']}\n\ncut-units.brep 是一份完整的 Classify 加工面模型，保留全部 {case['faceCount']} 个原面、{case['unitCount']} 个切割单元及 {case['pendingFaces']} 个待处理面，保持原坐标。根 Compound 下按单元分组；待处理面独立保留。不是完整原始实体，不补造不加工皮面。\n\n标准 BREP 不存颜色、名称或线型。cut-units.colors.json 按一级子组的遍历序号记录单元、原面、颜色与边界角色；其中 sourceStepEdgeIds 是原 STEP 编号。colored.step 保存整份加工面模型的面色和原面/单元名称，可直接在支持颜色的 CAD 查看器打开；本网页使用粗实线、虚线、普通实线显示边界角色。\n\n单元是几何区域，尚未生成实际刀路。\n")
    write(folder/'view.json',case)
    rows=[{'caseId':item['id'],'file':item['file'],'faceId':f['faceId'],'status':f['status'],'form':f['units'][0]['form'] if f['units'] else '',
        'unitCount':len(f['units']),'countedUnitCount':sum(not u.get('assemblyId') or f['faceId']==u['sourceRegion']['faceIds'][0] for u in f['units']),
        'unitKeys':';'.join(u.get('key',f"F{f['faceId']}:U{u['id']}") for u in f['units']),
        'logicalEdges':f['logicalEdgeCount'],'boundarySegments':len(f['groups']),
        'chains':len(f['chains']),'reason':f['reason'],'previousUnitCount':f['previousUnitCount']} for f in case['faces']]
    write(folder/'rows.json',rows)
    result={'id':item['id'],'file':item['file'],'status':'processed','faces':case['faceCount'],'units':case['unitCount'],'pendingFaces':case['pendingFaces'],
        'forms':dict(Counter(u['form'] for u in all_units)),'comparisons':sum('comparison' in f for f in case['faces']),
        'mergeRuleVersion':2,'mergedUnits':len(case['assembledUnits']),'mergedSourceFaces':sum(len(u['sourceRegion']['faceIds']) for u in case['assembledUnits']),
        'nativeGeometryReused':same_plan,'mergeChecks':dict(Counter(d.get('reason') or 'accepted' for d in case['mergeDecisions']))}
    write(folder/'result.json',result)
    if (same_plan and previous.get('inspection') and (folder/'inspection-audit.json').exists()
            and matches_exporter(read(folder/'inspection-audit.json'),sha(INSPECTION_EXE))):
        report=read(folder/'inspection-audit.json')
        assert report['passed'] and report['sourceHash']==item['sha256']
        assert report['inspectionSha256']==sha(folder/'inspection.step')
        package_inspection(case,folder,report)
    else:generate_inspection(item,folder)
    publish_case(item,folder)
    write(folder/'stamp.json',fingerprint)
    print(f"units {key}: {result['faces']} faces, {result['units']} units, {result['pendingFaces']} pending",flush=True)
    return result

def index(items):
    results=[];rows=[]
    for item in items:
        folder=DEST/'cases'/f"{item['id']:03d}"
        if (folder/'result.json').exists():results.append(read(folder/'result.json'));rows.extend(read(folder/'rows.json'))
    summary={'sourceFiles':len(items),'processedFiles':len(results),'faces':len(rows),'unitCount':sum(r.get('countedUnitCount',r['unitCount']) for r in rows),
        'pendingFaces':sum(r['status']=='pending' for r in rows),'forms':dict(sum((Counter(r['forms']) for r in results),Counter())),
        'mergeRuleVersion':2,'mergeProcessedFiles':sum(r.get('mergeRuleVersion')==2 for r in results),'mergedUnits':sum(r.get('mergedUnits',0) for r in results),
        'mergeChecks':dict(sum((Counter(r.get('mergeChecks',{})) for r in results),Counter())),
        'pendingReasons':dict(Counter(r['reason'] for r in rows if r['status']=='pending')),'newFaceBoundaries':0,'machinePlanning':'Deferred'}
    write(DEST/'manifest.json',results);write(DEST/'faces.json',rows);write(DEST/'summary.json',summary)
    with (DEST/'faces.csv').open('w',encoding='utf-8-sig',newline='') as out:
        writer=csv.DictWriter(out,fieldnames=list(dict.fromkeys(k for r in rows for k in r)));writer.writeheader();writer.writerows(rows)
    print(json.dumps(summary,ensure_ascii=False,indent=2),flush=True)

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--prepare',action='store_true');ap.add_argument('--only',nargs='+',type=int);ap.add_argument('--reuse',action='store_true');ap.add_argument('--workers',type=int,default=4);args=ap.parse_args()
    DEST.mkdir(exist_ok=True);(DEST/'inputs').mkdir(exist_ok=True);(DEST/'boundaries').mkdir(exist_ok=True)
    if args.prepare:
        sources=sorted((ROOT/'TestData/Tube/Classify').glob('*.step'),key=lambda p:p.name)
        items=[{'id':i,'file':s.name,'originalSource':str(s)} for i,s in enumerate(sources,1)]
        with ThreadPoolExecutor(max_workers=args.workers) as pool:items=list(pool.map(prepare,items))
        write(DEST/'source-manifest.json',items);print(f'Prepared {len(items)} current files',flush=True);return
    items=read(DEST/'source-manifest.json');selected=[i for i in items if not args.only or i['id'] in args.only]
    failures=[]
    with ThreadPoolExecutor(max_workers=args.workers) as pool:
        futures={pool.submit(generate,i,args.reuse):i for i in selected}
        for f in as_completed(futures):
            try:f.result()
            except Exception as error:
                failure={'id':futures[f]['id'],'file':futures[f]['file'],'error':repr(error)};failures.append(failure);print(json.dumps(failure,ensure_ascii=False),flush=True)
    write(DEST/'batch-errors.json',failures)
    if failures:raise SystemExit(1)
    index(items)

if __name__=='__main__':main()
