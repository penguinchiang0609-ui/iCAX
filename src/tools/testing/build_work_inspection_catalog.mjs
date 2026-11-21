// Publish existing native Work outputs for the inspection page. This tool never
// reruns Heal, Align, Contour, Classify, edge analysis or path analysis.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {spawn} from 'node:child_process';
import {fileURLToPath} from 'node:url';

const repoDefault=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../..');
const args=process.argv.slice(2);
if(args.includes('--help')){
  process.stdout.write('node build_work_inspection_catalog.mjs [--native <TubeStageProbe.exe>] [--workers 2] [--timeout 120] [--models 13,69] [--output <WorkInspection>] [--mirror <served PathAnalysis>] [--coverage-root <FaceCutCoverage>]\nWithout --native, only already verified mesh previews are reused. Existing Work results are read only.\n');
  process.exit(0);
}
const supported=new Set(['--native','--workers','--timeout','--models','--output','--mirror','--repo','--seed','--path-root','--coverage-root']);
for(let i=0;i<args.length;i+=2)if(!supported.has(args[i])||i+1>=args.length)throw Error('Unknown or incomplete option: '+args[i]);
const option=(name,fallback)=>args.includes(name)?args[args.indexOf(name)+1]:fallback;
const repo=path.resolve(option('--repo',repoDefault));
const pathRoot=path.resolve(option('--path-root',path.join(repo,'TestData/Tube/PathAnalysis')));
const coverageRoot=option('--coverage-root','')?path.resolve(option('--coverage-root','')):null;
const output=path.resolve(option('--output',path.join(repo,'TestData/Tube/WorkInspection')));
const mirror=path.resolve(option('--mirror',path.join(repo,'output/tests/machining-path-analysis-20261105/smoke-initial/PathAnalysis')));
const seedRoot=path.resolve(option('--seed',path.join(repo,'output/tests/machining-path-analysis-20261105/logical-batch')));
const seedStaging=path.join(seedRoot,'staging');
const native=option('--native','')?path.resolve(option('--native','')):null;
const workers=Number(option('--workers','2')),timeoutSeconds=Number(option('--timeout','120'));
if(!Number.isInteger(workers)||workers<1||workers>4||!Number.isFinite(timeoutSeconds)||timeoutSeconds<=0)throw Error('Invalid preview workers or timeout');
if(path.basename(output)!=='WorkInspection')throw Error('Output directory must be named WorkInspection');
if(native&&!fs.existsSync(native))throw Error('Native mesh exporter is missing: '+native);
const selected=new Set(option('--models','').split(',').filter(Boolean));
const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
const write=(p,value)=>{fs.mkdirSync(path.dirname(p),{recursive:true});const temp=p+'.tmp-'+crypto.randomUUID();fs.writeFileSync(temp,JSON.stringify(value,null,2));fs.renameSync(temp,p);};
const exists=p=>typeof p==='string'&&fs.existsSync(p)&&fs.statSync(p).isFile();
const hashes=new Map();
function sha(p){p=path.resolve(p);const stat=fs.statSync(p),key=p+':'+stat.size+':'+stat.mtimeMs;if(!hashes.has(key))hashes.set(key,crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex'));return hashes.get(key);}
const assetUrl=relative=>'/data/WorkInspection/'+relative.split(path.sep).map(encodeURIComponent).join('/');
const slash=p=>p.split(path.sep).join('/');
function within(p,root){const relative=path.relative(root,path.resolve(p));return relative===''||(!relative.startsWith('..'+path.sep)&&relative!=='..'&&!path.isAbsolute(relative));}
const publishedFiles=new Set();
function retain(source,relative){
  if(typeof source!=='string'||!source)return null;
  source=path.resolve(source);const target=path.join(output,relative);
  if(!within(target,output))throw Error('Asset leaves WorkInspection: '+relative);
  if(!exists(source))return null;
  const digest=sha(source);fs.mkdirSync(path.dirname(target),{recursive:true});
  if(!exists(target)||sha(target)!==digest){
    // Only the inspection link/copy is replaced; the source is never opened
    // for writing. Native previews also use a temporary destination.
    if(exists(target))fs.unlinkSync(target);
    try{fs.linkSync(source,target);}catch{fs.copyFileSync(source,target);}
  }
  if(sha(source)!==digest||sha(target)!==digest)throw Error('Source or copied asset changed: '+source);
  publishedFiles.add(relative);
  return {url:assetUrl(relative),sourcePath:source,sha256:digest};
}
function artifact(record,label,source,relative){const retained=retain(source,relative);if(retained)record.artifacts.push({label,...retained});return retained?.url??null;}
function metaAsset(id,work,value){const relative=`assets/${id}-${work}.json`;write(path.join(output,relative),value);publishedFiles.add(relative);return assetUrl(relative);}
const works=[{id:'import',label:'导入'},{id:'heal',label:'Heal · 几何修整'},{id:'align',label:'Align · X轴对齐'},{id:'contour',label:'Contour · 截面轮廓'},{id:'classify',label:'Classify · 加工面分类'},{id:'edgeAnalysis',label:'EdgeAnalysis · 皮关系与凹凸性'},{id:'pathAnalysis',label:'PathAnalysis · 路径与刀轴'}];
const pathManifestFile=path.join(pathRoot,'manifest.json'),cacheManifestFile=path.join(pathRoot,'cache-manifest.json');
const pathManifest=read(pathManifestFile),cacheManifest=read(cacheManifestFile);
const coverageManifestFile=coverageRoot?path.join(coverageRoot,'manifest.json'):null;
const coverageManifest=coverageManifestFile?read(coverageManifestFile):null;
const coverageEntries=new Map((coverageManifest?.models??[]).map(m=>[String(m.id),m]));
if(coverageRoot)works.push({id:'faceCutCoverage',label:'FaceCutCoverage · 逐面切透分析'});
const caches=new Map(cacheManifest.models.map(m=>[String(m.id),m]));
const seedSummaryFile=path.join(seedRoot,'summary.json'),seedSummary=read(seedSummaryFile);
const seedRows=new Map(seedSummary.rows.map(r=>[String(r.id),r]));

// Discover only small run-summary indexes in the existing inspection tasks.
// Match the actual Path producer/source/geometry counts, never a filename alone.
const summaryFiles=new Set([seedSummaryFile]);
for(const item of pathManifest.latestRepair?.summaries??[])summaryFiles.add(path.resolve(repo,item));
for(const task of ['machining-path-analysis-20261105','machining-path-side-skin-20261105']){
  const root=path.join(repo,'output/tests',task);if(!fs.existsSync(root))continue;
  for(const entry of fs.readdirSync(root,{withFileTypes:true}))if(entry.isDirectory()&&!/^(native|obj-|prior-|browser|baseline)/.test(entry.name)){
    const file=path.join(root,entry.name,'summary.json');if(exists(file))summaryFiles.add(file);
  }
}
const runRows=[];
for(const file of summaryFiles)if(exists(file)){
  const summary=read(file);if(summary.stage!=='pathAnalysis'||!Array.isArray(summary.rows))continue;
  for(const row of summary.rows)runRows.push({row,summary,file,when:summary.completedAt??summary.startedAt??''});
}
const producerEqual=(a,b)=>!!a&&!!b&&['TubeStageProbe.exe','ExtrusionRecognition.dll','OpenCascadeResourceImport.dll'].every(k=>a[k]?.sha256&&a[k].sha256===b[k]?.sha256);
function actualReplay(entry,document){
  const candidates=runRows.filter(({row})=>String(row.id)===String(entry.id)&&row.name===entry.name
    &&producerEqual(row.producer,document.run?.producer??entry.producer)
    &&row.sourceSha256===(document.run?.sourceSha256??entry.sourceSha256)
    &&Object.entries(document.counts??{}).every(([k,v])=>row.counts?.[k]===v||(row.counts?.[k]===undefined&&v===0)));
  candidates.sort((a,b)=>b.when.localeCompare(a.when));
  return candidates.find(({row,file})=>exists(path.resolve(path.dirname(row.reportFile??path.join(path.dirname(file),'staging/Reports',entry.name,'part-1.json')),'../../EdgeAnalysis',entry.name,'part-1.json')))||null;
}
function nativeReport(row,name){const file=row?.reportFile??path.join(seedStaging,'Reports',name,'part-1.json');return {file,value:exists(file)?read(file):null};}
function baseRecord(identityGroup,coordinateNote,provenance){return {status:'not-run',reason:'前序阶段未完成，本轮未执行',dataUrl:null,geometryUrl:null,transform4x4:null,identityGroup,coordinateNote,artifacts:[],provenance};}
function stageState(record,stage,runRow,report){
  if(!stage){record.status=report?'not-run':'missing';record.reason=report?'前序阶段未完成，本轮未执行':'缺少本批阶段报告';return;}
  if(stage.status==='passed'){record.status='passed';record.reason='';}
  else if(stage.status==='partial'){record.status='partial';record.reason=stage.reason??'本轮部分完成';}
  else if(stage.status==='running'&&runRow?.timedOut){record.status='failed';record.reason=runRow.error??'本轮执行超时';}
  else if(stage.status==='failed'){record.status='failed';record.reason=stage.error??report?.error??runRow?.error??(stage.diagnostics??[]).join('; ')??'本轮阶段失败';}
  else{record.status='failed';record.reason=runRow?.error??'本轮执行未完成';}
}
function originProvenance(entry,row,reportFile,producer,run){
  const source=path.join(repo,'TestData/Tube/origin',entry.name);
  return {run,source,sourceSha256:exists(source)?sha(source):null,producer:producer??{},report:exists(reportFile)?{sourcePath:reportFile,sha256:sha(reportFile)}:null};
}
const previewJobs=[],previewRefs=[],previewResults=[];
function preview(record,id,stage,source,transform){
  record.transform4x4=transform??null;
  if(!exists(source)){record.previewStatus='missing';if(record.status==='passed'||record.status==='partial')record.reason='本轮几何缓存缺失，保留阶段报告与已有原产物';return;}
  const relative=`assets/${id}-${stage}.mesh.json`,target=path.join(output,relative),evidenceFile=target+'.evidence.json';
  const sourceSha256=sha(source),nativeSha256=native?sha(native):null;
  let existing=false;
  if(exists(target)&&exists(evidenceFile)){
    const evidence=read(evidenceFile);
    existing=['passed','partial'].includes(evidence.status)&&evidence.sourceSha256===sourceSha256&&evidence.outputSha256===sha(target)
      &&(!native||evidence.producer?.sha256===nativeSha256);
  }
  const ref={record,id,stage,source,sourceSha256,relative,target,evidenceFile};previewRefs.push(ref);
  if(existing){const evidence=read(evidenceFile);record.geometryUrl=assetUrl(relative);record.previewStatus=evidence.status;record.previewReason=evidence.previewReason??'';record.previewEvidence=evidence;publishedFiles.add(relative);publishedFiles.add(relative+'.evidence.json');return;}
  record.previewStatus='missing';
  if(native)previewJobs.push(ref);else if(record.status==='passed'||record.status==='partial')record.reason=[record.reason,'几何产物可下载，三维预览尚未生成'].filter(Boolean).join('；');
}
const models=[];
for(const entry of pathManifest.models){
  if(selected.size&&!selected.has(String(entry.id))&&!selected.has(entry.name))continue;
  const id=String(entry.id).padStart(3,'0'),seedRow=seedRows.get(String(entry.id)),sourceReport=nativeReport(seedRow,entry.name);
  const report=sourceReport.value,cache=caches.get(String(entry.id));
  if(!entry.dataUrl?.startsWith('/data/'))throw Error('Unexpected PathAnalysis data URL');
  const pathDataFile=path.resolve(pathRoot,decodeURIComponent(entry.dataUrl.slice(6)));
  if(!within(pathDataFile,pathRoot))throw Error('PathAnalysis URL leaves publication root');
  const document=exists(pathDataFile)?read(pathDataFile):null;
  const provenance=originProvenance(entry,seedRow,sourceReport.file,seedSummary.producer,seedSummaryFile);
  const expectedSourceSha=document?.run?.sourceSha256??entry.sourceSha256;
  if(expectedSourceSha&&provenance.sourceSha256!==expectedSourceSha)throw Error('Current original CAD differs from this Work run: '+entry.name);
  const matrix=report?.stages?.align?.status==='passed'?report.stages.align.transform4x4:null;
  const coordinateNote=matrix?'统一按本批 Align 变换展示；下载文件保留原坐标':'本轮尚未完成对齐；按原坐标展示';
  const records=Object.fromEntries(works.map(w=>[w.id,baseRecord(['classify','edgeAnalysis','pathAnalysis','faceCutCoverage'].includes(w.id)?'classified-id':w.id==='import'?'import-id':'healed-id',['classify','edgeAnalysis','pathAnalysis','faceCutCoverage'].includes(w.id)?'本批 Classify 坐标：+X 对齐；分析阶段共用精确面编号':coordinateNote,{...provenance})]));
  for(const work of ['import','heal','align','contour','classify']){
    const record=records[work],stage=report?.stages?.[work];stageState(record,stage,seedRow,report);
    const data={schema:1,stage:work,name:entry.name,status:record.status,success:record.status==='passed'||record.status==='partial',reason:record.reason,report:stage??null,diagnostics:stage?.diagnostics??[],provenance:record.provenance};
    if(work==='classify'&&document?.faces?.length){
      data.faces=document.faces.map(f=>({faceId:f.faceId,faceIndex:f.faceIndex,machining:f.machining,machiningIndex:f.machiningIndex,sourceId:f.sourceId,sourceTags:f.sourceTags,displaySkippedZeroArea:f.displaySkippedZeroArea,displaySkipReason:f.displaySkipReason}));
      data.machiningFaceIds=data.faces.filter(f=>f.machining).map(f=>String(f.faceId));data.nonMachiningFaceIds=data.faces.filter(f=>!f.machining).map(f=>String(f.faceId));
      data.counts={wholeFaces:data.faces.length,machiningFaces:data.machiningFaceIds.length,nonMachiningFaces:data.nonMachiningFaceIds.length};
    }
    if(stage||work==='classify')record.dataUrl=metaAsset(id,work,data);
    artifact(record,'阶段报告',path.join(output,`assets/${id}-${work}.json`),`assets/${id}-${work}.json`);
    artifact(record,'同批原生运行报告',sourceReport.file,`assets/${id}/upstream-native-report.json`);
  }
  const imported=report?.inputBRep??path.join(seedStaging,'Imported',entry.name,'part-1.icaxbrep');
  const healed=report?.healedBRep??path.join(seedStaging,'Reports',entry.name,'part-1-healed.icaxbrep');
  artifact(records.import,'原始 CAD',provenance.source,`assets/${id}/import-original${path.extname(entry.name)}`);
  artifact(records.import,'导入中性 BREP',imported,`assets/${id}/import.icaxbrep`);
  artifact(records.import,'导入报告',path.join(seedStaging,'Imported',entry.name,'import.json'),`assets/${id}/import-report.json`);
  artifact(records.heal,'Heal STEP',path.join(seedStaging,'Heal',entry.name,'part-1.step'),`assets/${id}/heal.step`);
  artifact(records.heal,'Heal 中性 BREP',healed,`assets/${id}/heal.icaxbrep`);
  artifact(records.heal,'Heal 原始报告',path.join(seedStaging,'Heal',entry.name,'part-1.json'),`assets/${id}/heal-native.json`);
  artifact(records.align,'Align STEP',path.join(seedStaging,'Align',entry.name,'part-1.step'),`assets/${id}/align.step`);
  const contourFile=path.join(seedStaging,'Coutour',entry.name,'part-1.json');
  for(const [extension,label]of [['.json','轮廓数据'],['.step','轮廓与本体 STEP'],['.svg','截面 SVG'],['.raw.svg','原始投影 SVG']]){
    const retained=artifact(records.contour,label,path.join(seedStaging,'Coutour',entry.name,'part-1'+extension),`assets/${id}/contour${extension}`);
    if(extension==='.json'&&retained)records.contour.dataUrl=retained;
  }
  preview(records.import,id,'import',imported,matrix);
  preview(records.heal,id,'heal',healed,matrix);
  // Align and Contour reuse the exact healed preview in the display frame.
  previewRefs.push({record:records.align,reuse:records.heal});previewRefs.push({record:records.contour,reuse:records.heal});
  records.align.transform4x4=records.contour.transform4x4=matrix;
  if(cache?.available){
    records.classify.provenance={...provenance,checkpoint:{directory:cache.directory,files:cache.files,producer:cache.producer},inputUnchanged:cache.originalCad?.sha256===provenance.sourceSha256};
    for(const [name,label]of [['split.icaxbrep','Classify 完整中性 BREP'],['machining.icaxbrep','加工面中性 BREP'],['stock.icaxbrep','非加工面中性 BREP'],['section-curves.icaxbrep','精确截面曲线'],['section-domains.json','精确截面参数']]){
      const file=cache.files?.[name];if(file&&exists(file.path)&&sha(file.path)!==file.sha256)throw Error('Classify checkpoint SHA changed: '+file.path);
      artifact(records.classify,label,file?.path,`assets/${id}/classify-${name}`);
    }
    artifact(records.classify,'加工面 STEP',path.join(seedStaging,'Classify',entry.name,'part-1-machining.step'),`assets/${id}/classify-machining.step`);
    if(document?.geometry?.completeModel&&document.geometry.faces?.length)records.classify.geometryUrl=entry.dataUrl;
  }
  const replay=document?actualReplay(entry,document):null;
  const replayReport=replay?nativeReport(replay.row,entry.name):null;
  if(replay){
    records.edgeAnalysis.provenance=originProvenance(entry,replay.row,replayReport.file,replay.row.producer,replay.file);
    records.edgeAnalysis.provenance.checkpoint=replay.row.upstreamCheckpoint??cache??null;
    artifact(records.edgeAnalysis,'本轮边分析运行报告',replayReport.file,`assets/${id}/edge-native-report.json`);
    const edgeFile=path.resolve(path.dirname(replayReport.file),'../../EdgeAnalysis',entry.name,'part-1.json');
    const edgeStage=replayReport.value?.stages?.edgeAnalysis;
    stageState(records.edgeAnalysis,edgeStage,replay.row,replayReport.value);
    if(records.edgeAnalysis.status==='passed'&&edgeStage?.analysisComplete===false){records.edgeAnalysis.status='partial';records.edgeAnalysis.reason='部分物理边界未可靠排序，原边界仍保留';}
    for(const [extension,label]of [['.json','逐边皮关系与凹凸性'],['.brep','本 Work 加工面 BREP'],['.step','边分析着色 STEP']]){
      const url=artifact(records.edgeAnalysis,label,edgeFile.replace(/\.json$/i,extension),`assets/${id}/edge-analysis${extension}`);
      if(extension==='.json')records.edgeAnalysis.dataUrl=url;
    }
    if(records.edgeAnalysis.dataUrl&&document?.geometry?.faces?.length)records.edgeAnalysis.geometryUrl=entry.dataUrl;
  }else{
    records.edgeAnalysis.status=cache?.available?'missing':'not-run';records.edgeAnalysis.reason=cache?.available?'未找到与当前 Path 生产者及输入一致的本轮边分析原产物':'前序阶段未完成，本轮未执行';
  }
  const paths=records.pathAnalysis;paths.provenance={...originProvenance(entry,null,pathDataFile,document?.run?.producer??entry.producer,replay?.file??null),checkpoint:document?.run?.upstreamCheckpoint??cache??null};
  paths.status=entry.status==='partial'?'partial':entry.status==='passed'?'passed':cache?.available?'failed':'not-run';paths.reason=entry.pendingReason??entry.error??document?.run?.pendingReason??document?.run?.error??'';if(paths.status==='not-run')paths.reason='前序阶段未完成，本轮未执行路径分析；'+paths.reason;paths.dataUrl=document?entry.dataUrl:null;
  if(document?.artifacts?.artifactAvailable?.brep&&document.geometry?.faces?.length)paths.geometryUrl=entry.dataUrl;
  for(const [extension,label]of [['.json','路径分析数据'],['.brep','完整模型 BREP'],['.step','路径与刀轴 STEP']]){
    if(extension!=='.json'&&!document?.artifacts?.artifactAvailable?.[extension.slice(1)])continue;
    const source=pathDataFile.replace(/\.json$/i,extension);if(!exists(source))continue;
    paths.artifacts.push({label,url:entry.dataUrl.replace(/\.json$/i,extension),sourcePath:source,sha256:sha(source)});
  }
  if(coverageRoot){
    const coverageEntry=coverageEntries.get(String(entry.id)),coverage=records.faceCutCoverage;
    if(!coverageEntry){coverage.status='not-run';coverage.reason='本轮未执行逐面切透分析';}
    else{
      if(coverageEntry.name!==entry.name||!coverageEntry.dataUrl?.startsWith('/data/FaceCutCoverage/'))throw Error('Unexpected FaceCutCoverage entry: '+entry.id);
      const coverageFile=path.resolve(coverageRoot,decodeURIComponent(coverageEntry.dataUrl.slice('/data/FaceCutCoverage/'.length)));
      if(!within(coverageFile,coverageRoot)||!exists(coverageFile))throw Error('Missing or invalid coverage data path: '+entry.id);
      const coverageData=read(coverageFile);
      if(coverageData.run?.inputPath?.sha256!==sha(pathDataFile))throw Error('Coverage input differs from current PathAnalysis: '+entry.id);
      if(coverageData.run?.sourceSha256!==provenance.sourceSha256)throw Error('Coverage source CAD differs: '+entry.id);
      coverage.status=coverageEntry.status;
      coverage.reason=coverageEntry.reason??(!coverageData.complete&&coverageData.success?'部分皮边区间尚未完成边界映射；仅未完成区间保留待定':'');
      coverage.provenance={...originProvenance(entry,null,coverageFile,coverageData.run?.producer,coverageManifestFile),inputPath:coverageData.run?.inputPath,checkpoint:coverageData.run?.upstreamCheckpoint};
      for(const [extension,label]of [['.json','逐面映射与覆盖数据'],['.brep','完整模型 BREP'],['.step','切透分析着色 STEP']]){
        if(extension!=='.json'&&!['passed','partial'].includes(coverage.status))continue;
        const url=artifact(coverage,label,coverageFile.replace(/\.json$/i,extension),`assets/${id}/face-cut-coverage${extension}`);
        if(extension==='.json')coverage.dataUrl=url;
      }
      if(coverageData.geometry?.completeModel&&coverageData.geometry.faces?.length)coverage.geometryUrl=coverage.dataUrl;
      if(coverage.geometryUrl){coverage.previewStatus=coverageData.geometry.previewStatus??(coverageData.geometry.issues?.length?'partial':'passed');coverage.previewReason=coverageData.geometry.issues?.length?`${coverageData.geometry.issues.length} 项预览问题；原生分析和完整产物仍保留`:'';}
    }
  }
  models.push({id:entry.id,name:entry.name,works:records});
  if(models.length%25===0)process.stdout.write(`Indexed ${models.length}/${pathManifest.models.length} existing model outputs\n`);
}

function runPreview(job){return new Promise(resolve=>{
  const temporary=job.target+'.tmp-'+crypto.randomUUID(),log=job.target+'.log';fs.mkdirSync(path.dirname(job.target),{recursive:true});
  const fd=fs.openSync(log,'w'),nativeRoot=path.dirname(native);
  const env={...process.env,PATH:[nativeRoot,path.join(repo,'src/x64/Debug'),path.join(repo,'src/third_party/opencascade/install/vs2022-x64/win64/vc14/bind'),process.env.PATH].join(path.delimiter)};
  let done=false,timedOut=false;
  const child=spawn(native,['--work-inspection-mesh',job.source,temporary],{cwd:path.join(repo,'src/x64/Debug'),env,windowsHide:true,stdio:['ignore',fd,fd]});
  const timer=setTimeout(()=>{timedOut=true;child.kill();},timeoutSeconds*1000);
  const finish=(code,error)=>{if(done)return;done=true;clearTimeout(timer);fs.closeSync(fd);
    const evidence={status:'failed',sourcePath:job.source,sourceSha256:job.sourceSha256,producer:{path:native,sha256:sha(native)},code,timedOut,error:error??null,log};
    try{
      if(timedOut||!exists(temporary))throw Error(error??(timedOut?'预览导出超时':'预览导出失败'));
      if(sha(job.source)!==job.sourceSha256)throw Error('Preview export modified the source neutral BREP');
      const data=read(temporary);
      const hasFaces=Array.isArray(data.geometry?.faces)&&data.geometry.faces.length>0;
      const valid=data.stage==='inspectionMesh'&&data.success===true&&data.sourceUnchanged===true&&hasFaces;
      const partial=valid&&data.complete===false;
      if(!valid||(code!==0&&!(code===1&&partial)))throw Error('Native preview has no verified usable face mesh');
      if(exists(job.target))fs.unlinkSync(job.target);fs.renameSync(temporary,job.target);
      evidence.status=partial?'partial':'passed';evidence.outputSha256=sha(job.target);evidence.faceCount=data.geometry.faces.length;evidence.issues=data.geometry.issues??[];evidence.counts=data.counts??{};evidence.diagnostics=data.diagnostics??[];
      evidence.previewReason=partial?`预览部分完成：${evidence.issues.length} 项几何显示问题；保留已成功面与原产物`:'';
      job.record.geometryUrl=assetUrl(job.relative);job.record.previewStatus=evidence.status;job.record.previewReason=evidence.previewReason;job.record.previewEvidence=evidence;publishedFiles.add(job.relative);publishedFiles.add(job.relative+'.evidence.json');
    }catch(exception){evidence.error=exception.message;job.record.previewStatus='failed';job.record.previewReason=evidence.error;if(['passed','partial'].includes(job.record.status))job.record.reason=[job.record.reason,'本轮产物保留，三维预览失败：'+evidence.error].filter(Boolean).join('；');}
    write(job.evidenceFile,evidence);previewResults.push(evidence);resolve();
  };
  child.on('error',error=>finish(null,error.message));child.on('exit',code=>finish(code));
});}
let nextJob=0;
if(native)await Promise.all(Array.from({length:Math.min(workers,previewJobs.length)},async()=>{while(nextJob<previewJobs.length){const job=previewJobs[nextJob++];await runPreview(job);process.stdout.write(`Preview ${previewResults.length}/${previewJobs.length}: ${job.id} ${job.stage} ${job.record.previewStatus}\n`);}}));
for(const ref of previewRefs)if(ref.reuse){
  const target=ref.record,source=ref.reuse;
  // Never show a previous stage's body as the successful result of an
  // unexecuted or failed stage. Existing diagnostics/downloads remain usable.
  if(['passed','partial'].includes(target.status)){target.geometryUrl=source.geometryUrl;target.previewStatus=source.previewStatus;target.previewEvidence=source.previewEvidence;target.previewReason=source.previewReason??(!source.geometryUrl?'同批 Heal 几何预览不可用':'');}
}
const manifest={schema:1,works,models,generatedAt:new Date().toISOString(),policy:'Existing exact Work products only; no Work recomputation. Import/Heal preview transforms affect display only. Earlier failed stages do not borrow historical successful products.',sources:{pathManifest:{path:pathManifestFile,sha256:sha(pathManifestFile)},classifyCacheManifest:{path:cacheManifestFile,sha256:sha(cacheManifestFile)},seedRun:{path:seedSummaryFile,sha256:sha(seedSummaryFile)},matchedRunSummaries:[...new Set(models.map(m=>m.works.edgeAnalysis.provenance.run).filter(Boolean))].map(file=>({path:file,sha256:sha(file)}))},preview:{requested:previewJobs.length,passed:previewResults.filter(r=>r.status==='passed').length,partial:previewResults.filter(r=>r.status==='partial').length,failed:previewResults.filter(r=>r.status==='failed').length,workers,native:native?{path:native,sha256:sha(native)}:null}};
if(coverageManifestFile)manifest.sources.faceCutCoverageManifest={path:coverageManifestFile,sha256:sha(coverageManifestFile)};
write(path.join(output,'work-manifest.json'),manifest);
write(path.join(pathRoot,'work-manifest.json'),manifest);
const mirrorAssets=path.join(mirror,'WorkInspection');fs.mkdirSync(mirrorAssets,{recursive:true});
for(const relative of publishedFiles){const source=path.join(output,relative),target=path.join(mirrorAssets,relative);if(!exists(source))continue;if(!within(target,mirrorAssets))throw Error('Mirror asset leaves expected root');fs.mkdirSync(path.dirname(target),{recursive:true});if(exists(target)&&sha(target)===sha(source))continue;if(exists(target))fs.unlinkSync(target);try{fs.linkSync(source,target);}catch{fs.copyFileSync(source,target);}if(sha(target)!==sha(source))throw Error('Mirror asset mismatch: '+relative);}
write(path.join(mirrorAssets,'work-manifest.json'),manifest);write(path.join(mirror,'work-manifest.json'),manifest);
const states={};for(const m of models)for(const [work,r]of Object.entries(m.works)){states[work]??={};states[work][r.status]=(states[work][r.status]??0)+1;}
const verification={schema:1,status:manifest.preview.failed?'completed-with-preview-failures':manifest.preview.partial?'completed-with-partial-previews':'passed',models:models.length,states,sourceProductsReadOnly:true,sourceManifestSha256:sha(pathManifestFile),assets:publishedFiles.size,manifestSha256:sha(path.join(output,'work-manifest.json')),mirrorManifestSha256:sha(path.join(mirror,'work-manifest.json')),preview:manifest.preview};
write(path.join(output,'validation.json'),verification);write(path.join(mirrorAssets,'validation.json'),verification);
process.stdout.write(JSON.stringify(verification,null,2)+'\n');
