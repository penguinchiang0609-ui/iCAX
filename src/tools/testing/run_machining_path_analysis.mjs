// Reimport each current original CAD file, then run the production staged
// pipeline through face-edge path analysis. Previous successful files cannot
// supply the output of a failed current invocation.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {spawn} from 'node:child_process';
import {fileURLToPath} from 'node:url';

const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../..');
const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
const write=(p,v)=>{fs.mkdirSync(path.dirname(p),{recursive:true});fs.writeFileSync(p,JSON.stringify(v,null,2));};
const hash=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const args=process.argv.slice(2), option=(key,fallback)=>args.includes(key)?args[args.indexOf(key)+1]:fallback;
const supported=new Set(['--native','--output','--publish','--workers','--timeout','--models','--fresh-catalog','--checkpoints']);
for(const arg of args)if(arg.startsWith('--')&&!supported.has(arg))throw Error(`Unknown option ${arg}`);
const output=path.resolve(option('--output',path.join(repo,'output/tests/machining-path-analysis-20261105')));
const native=path.resolve(option('--native',path.join(output,'native')));
const publish=path.resolve(option('--publish',path.join(repo,'TestData/Tube/PathAnalysis')));
const workers=Number(option('--workers','4')),timeoutSeconds=Number(option('--timeout','240'));
if(path.basename(publish)!=='PathAnalysis')throw Error('Publication directory must be named PathAnalysis');
if(!Number.isInteger(workers)||workers<1||workers>8||!Number.isFinite(timeoutSeconds)||timeoutSeconds<=0)throw Error('Invalid workers or timeout');
const prior=read(path.join(repo,'TestData/Tube/EdgeAnalysis/manifest.json'));
const selected=option('--models','').split(',').filter(Boolean);
const parts=prior.models.filter(p=>!selected.length||selected.includes(String(p.id))||selected.includes(p.name));
if(!parts.length)throw Error('No models selected');
const checkpointFile=option('--checkpoints','');
const checkpointIndex=checkpointFile?read(path.resolve(checkpointFile)):null;
const checkpointModels=new Map((checkpointIndex?.models??[]).map(row=>[String(row.id),row]));
const checkpointFiles=['split.icaxbrep','machining.icaxbrep','stock.icaxbrep','section-curves.icaxbrep','section-domains.json','performance.json'];
const executable=path.join(native,'TubeStageProbe.exe');
const producer={};
for(const name of ['TubeStageProbe.exe','ExtrusionRecognition.dll','OpenCascadeResourceImport.dll'])producer[name]={path:path.join(native,name),sha256:hash(path.join(native,name))};
const env={...process.env,PATH:[native,path.join(repo,'src/x64/Debug'),path.join(repo,'src/third_party/opencascade/install/vs2022-x64/win64/vc14/bind'),process.env.PATH].join(path.delimiter)};
const began=new Date().toISOString(),staging=path.join(output,'staging');
const summary={schema:1,stage:'pathAnalysis',status:'running',startedAt:began,selected:parts.length,workers,timeoutSeconds,
  analysisGranularity:'logical-edge-parts',
  sourcePolicy:checkpointIndex?'Current original CAD and exact Classify-cache SHAs are verified before current native edge/path replay; original neutral topology IDs are retained.':'Current original CAD is freshly imported; Classify, edge analysis and path analysis run in the same current native invocation.',
  checkpointIndex:checkpointFile?{path:path.resolve(checkpointFile),sha256:hash(path.resolve(checkpointFile))}:null,
  producer,rows:[]};
const manifestPath=path.join(publish,'manifest.json');
let priorCatalog=null;
if(args.includes('--fresh-catalog')&&fs.existsSync(manifestPath)){
  const archive=path.join(output,'prior-catalog-'+crypto.randomUUID()+'.json');fs.mkdirSync(path.dirname(archive),{recursive:true});fs.copyFileSync(manifestPath,archive);
  priorCatalog={path:archive,sha256:hash(archive)};summary.priorCatalog=priorCatalog;
}
const manifest=!args.includes('--fresh-catalog')&&fs.existsSync(manifestPath)?read(manifestPath):{schema:1,stage:'pathAnalysis',analysisGranularity:'logical-edge-parts',models:[],priorCatalog};
manifest.latestRepair={models:parts.length,startedAt:began,pathRule:'wholly-on-skin-only',
  summaries:[...new Set([path.relative(repo,path.join(output,'summary.json')).split(path.sep).join('/'),...(manifest.latestRepair?.summaries??[])])]};
const writeProgress=()=>{
  summary.rows.sort((a,b)=>a.id-b.id);summary.completed=summary.rows.length;summary.passed=summary.rows.filter(r=>r.status==='passed').length;summary.partial=summary.rows.filter(r=>r.status==='partial').length;summary.failed=summary.rows.filter(r=>r.status==='failed').length;summary.notRun=summary.rows.filter(r=>r.status==='not-run').length;
  manifest.models.sort((a,b)=>Number(a.id)-Number(b.id));write(path.join(output,'summary.json'),summary);write(manifestPath,manifest);
};
fs.mkdirSync(output,{recursive:true});fs.mkdirSync(publish,{recursive:true});
function runNative(arguments_,log){return new Promise(resolve=>{
  fs.mkdirSync(path.dirname(log),{recursive:true});const fd=fs.openSync(log,'w');let timedOut=false,finished=false;
  const child=spawn(executable,arguments_,{cwd:path.join(repo,'src/x64/Debug'),env,windowsHide:true,stdio:['ignore',fd,fd]});
  const finish=result=>{if(finished)return;finished=true;clearTimeout(timer);fs.closeSync(fd);resolve(result);};
  const timer=setTimeout(()=>{timedOut=true;child.kill();},timeoutSeconds*1000);
  child.on('error',error=>finish({code:null,timedOut,error:error.message}));child.on('exit',code=>finish({code,timedOut}));
});}
async function run(part){
  const started=Date.now(),id=Number(part.id),source=path.join(repo,'TestData/Tube/origin',part.name),partId='part-1';
  const base=path.join(staging,'PathAnalysis',part.name,partId),report=path.join(staging,'Reports',part.name,partId+'.json');
  const safeStem=String(id).padStart(3,'0')+'-'+path.parse(part.name).name;
  const row={id,name:part.name,source,status:'failed',error:null,complete:false,producer,reportFile:report,nativeInvoked:false,producedThisRun:false};
  try{
    if(!fs.existsSync(source))throw Error('Current original CAD file is missing');row.sourceSha256=hash(source);
    for(const extension of ['.json','.brep','.step']){const file=base+extension;if(fs.existsSync(file))fs.unlinkSync(file);}
    if(fs.existsSync(report))fs.unlinkSync(report);
    let nativeArgs=['--path-analysis-source',source,staging,part.name,partId];
    if(checkpointIndex){
      const cache=checkpointModels.get(String(id));
      row.checkpointIndex=summary.checkpointIndex;
      if(!cache?.available){
        row.status='not-run';row.complete=false;row.sourceUnchanged=hash(source)===row.sourceSha256;
        row.pendingReason='No validated exact Classify cache; current PathAnalysis was not run'+(cache?.reason||cache?.error?': '+(cache.reason||cache.error):'');
        publishRow(part,row,null,null,safeStem);return;
      }
      if(cache.name!==part.name)throw Error('Classify cache model name does not match the selected original');
      if(path.resolve(cache.originalCad.path).toLowerCase()!==path.resolve(source).toLowerCase()||cache.originalCad.sha256!==row.sourceSha256)throw Error('Original CAD differs from the cached source identity');
      for(const name of checkpointFiles){const file=cache.files?.[name];if(!file||path.resolve(file.path)!==path.resolve(cache.directory,name)||hash(file.path)!==file.sha256)throw Error(`Exact cached file changed: ${name}`);}
      if(!cache.importNeutral||hash(cache.importNeutral.path)!==cache.importNeutral.sha256)throw Error('Original imported neutral cache changed');
      const proof=read(cache.files['performance.json'].path);
      if(proof.status!=='passed'||proof.sourceNeutralUnchanged!==true||!['heal','align','contour','classify'].every(stage=>proof.stages?.[stage]?.passed===true))throw Error('Cached upstream Classify validation is incomplete');
      row.upstreamCheckpoint=cache;row.checkpointIndex=summary.checkpointIndex;
      nativeArgs=['--path-analysis-checkpoint',cache.directory,staging,part.name,partId];
    }
    row.nativeInvoked=true;
    const result=await runNative(nativeArgs,path.join(output,'logs',String(id).padStart(3,'0')+'.log'));
    Object.assign(row,result);row.sourceUnchanged=hash(source)===row.sourceSha256;
    const reportData=fs.existsSync(report)?read(report):null,pathStage=reportData?.stages?.pathAnalysis;
    row.nativeReportStatus=reportData?.status??null;row.nativeServiceSuccess=pathStage?.serviceSuccess??null;row.nativeAnalysisComplete=pathStage?.analysisComplete??null;
    const current=fs.existsSync(base+'.json')?read(base+'.json'):null;
    const validCurrent=current?.schema===1&&current.stage==='pathAnalysis'&&current.analysisGranularity==='logical-edge-parts'&&Array.isArray(current.faces)&&Array.isArray(current.edges);
    const artifactsExist=['.brep','.step'].every(extension=>fs.existsSync(base+extension));
    row.producedThisRun=!!validCurrent;
    const passed=result.code===0&&!result.timedOut&&row.sourceUnchanged&&validCurrent&&current.success===true&&current.complete===true&&artifactsExist
      &&reportData?.status==='passed'&&pathStage?.status==='passed'&&pathStage?.serviceSuccess===true&&pathStage?.analysisComplete===true
      &&current.artifacts?.brepRoundTripPassed===true&&current.artifacts?.stepRoundTrip?.passed===true;
    const currentGeometry=validCurrent&&artifactsExist&&row.sourceUnchanged&&current.artifacts?.brepRoundTripPassed===true&&current.artifacts?.stepRoundTrip?.passed===true;
    const partial=result.code===1&&!result.timedOut&&validCurrent&&current.success===true&&current.complete===false&&reportData?.status==='partial'
      &&pathStage?.status==='partial'&&pathStage?.serviceSuccess===true&&pathStage?.analysisComplete===false&&currentGeometry;
    row.status=passed?'passed':partial?'partial':'failed';row.counts=current?.counts??null;row.complete=current?.complete??false;
    if(partial)row.pendingReason=current.unresolvedBoundaries?.length?'Some physical boundaries have no reliable logical chain; source boundaries are retained without invented path axes':'Current native analysis reports incomplete boundary facts; see diagnostics and retained source records';
    if(!passed&&!partial)row.error=result.error??reportData?.error??(result.timedOut?`Native processing exceeded ${timeoutSeconds} seconds`:!row.sourceUnchanged?'Source CAD changed during processing':!validCurrent?'No current path analysis JSON':'Current analysis or artifact checks did not pass');
    publishRow(part,row,validCurrent?current:null,currentGeometry?base:null,safeStem);
  }catch(error){row.error=error.message;row.sourceUnchanged=row.sourceSha256&&fs.existsSync(source)?hash(source)===row.sourceSha256:null;publishRow(part,row,null,null,safeStem);}
  finally{
    row.seconds=(Date.now()-started)/1000;summary.rows.push(row);writeProgress();
    process.stdout.write(`${summary.completed}/${parts.length} ${id}: ${row.status}${row.error?' — '+row.error:row.pendingReason?' — '+row.pendingReason:''}\n`);
  }
}
function publishRow(part,row,current,geometryBase,stem){
  const archiveRoot=path.join(output,'prior-artifacts',String(row.id)+'-'+crypto.randomUUID());
  const priorArtifacts={};
  for(const extension of ['.json','.brep','.step']){
    const target=path.join(publish,stem+extension);
    if(!fs.existsSync(target))continue;
    const archive=path.join(archiveRoot,stem+extension);fs.mkdirSync(path.dirname(archive),{recursive:true});
    fs.copyFileSync(target,archive);priorArtifacts[extension]={path:archive,sha256:hash(archive)};
  }
  if(Object.keys(priorArtifacts).length)row.priorArtifacts=priorArtifacts;
  const document=current??{schema:1,stage:'pathAnalysis',analysisGranularity:'logical-edge-parts',name:part.name,success:false,complete:false,faces:[],edges:[],geometry:{faces:[],completeModel:true,previewOnly:true,issues:[]},
    counts:{wholeFaces:0,machiningFaces:0,edgeOccurrences:0,pathOccurrences:0,sideOccurrences:0,ready:0,pendingParallel:0,unsupported:0,invalid:0},diagnostics:[]};
  document.run={status:row.status,error:row.error??null,pendingReason:row.pendingReason??null,source:row.source,sourceSha256:row.sourceSha256??null,sourceUnchanged:row.sourceUnchanged,producer,upstreamCheckpoint:row.upstreamCheckpoint??null,nativeInvoked:row.nativeInvoked,producedThisRun:row.producedThisRun};
  if(row.status==='failed'){document.success=false;document.error=row.error;}
  document.artifacts={...document.artifacts,artifactAvailable:{brep:!!geometryBase,step:!!geometryBase,json:true},
    brep:geometryBase?stem+'.brep':null,step:geometryBase?stem+'.step':null,geometryFromCurrentRun:!!geometryBase};
  const artifacts={};
  for(const extension of ['.brep','.step']){
    const target=path.join(publish,stem+extension);
    if(geometryBase){fs.copyFileSync(geometryBase+extension,target);artifacts[extension==='.brep'?'brep':'step']={path:target,sha256:hash(target),currentGeometryOnly:row.status!=='passed'};}
    else if(fs.existsSync(target)){
      // Preserve prior output as evidence while removing it from this model's
      // live download name; a failed current row must never expose old success.
      fs.unlinkSync(target);
    }
  }
  const target=path.join(publish,stem+'.json');write(target,document);artifacts.data={path:target,sha256:hash(target)};row.artifacts=artifacts;
  const entry={id:row.id,name:part.name,status:row.status,error:row.error??null,pendingReason:row.pendingReason??null,analysisGranularity:'logical-edge-parts',dataUrl:'/data/'+encodeURIComponent(stem+'.json'),counts:document.counts,
    complete:document.complete,producer,sourceSha256:row.sourceSha256??null};
  const index=manifest.models.findIndex(m=>String(m.id)===String(row.id));if(index>=0)manifest.models[index]=entry;else manifest.models.push(entry);
}
writeProgress();let next=0;
await Promise.all(Array.from({length:Math.min(workers,parts.length)},async()=>{while(next<parts.length){const part=parts[next++];await run(part);}}));
for(const [name,p] of Object.entries(producer))if(hash(p.path)!==p.sha256)throw Error(`Native producer changed during run: ${name}`);
summary.status=summary.failed||summary.partial||summary.notRun?'completed-with-limitations':'passed';summary.completedAt=new Date().toISOString();summary.allSourceCadUnchanged=summary.rows.every(r=>r.sourceUnchanged===true);manifest.latestRepair.completedAt=summary.completedAt;writeProgress();
const passed=manifest.models.filter(m=>m.status==='passed').length,partial=manifest.models.filter(m=>m.status==='partial').length,failed=manifest.models.filter(m=>m.status==='failed').length,notRun=manifest.models.filter(m=>m.status==='not-run').length;
fs.writeFileSync(path.join(publish,'README.md'),`# 加工面走刀路径与刀轴分析\n\n当前目录 ${manifest.models.length} 个模型，${passed} 个完整逻辑结果，${partial} 个部分待处理，${failed} 个执行失败，${notRun} 个因上游缓存不可用而未执行。逐件状态见 manifest.json。\n\n每个原模型一份完整 BREP、一份着色 STEP 和分析 JSON。只有整段在皮上的逻辑边作为实际走刀路径（绿色 #1fe05c），刀轴为淡黄色 #f4df91，侧边为红色 #ff303b 虚线。非皮凸边只保留为切透分析的覆盖证据，侧边与其他边界事实也完整保留；它们不增加走刀，不生成有效刀轴。凹凸属性保留在每个来源区间，连续非皮逻辑边不因凹凸变化而拆散。平行母线和暂不支持曲面单独记录，未伪造刀轴。每条逻辑边只分析首尾两点；所有 Topo 来源片段仅用于完整曲线显示，STEP 和 JSON 保留选中路径两个端点的刀轴。\n\n${summary.sourcePolicy}分析不完整时，如本轮完整几何读回校验通过，仍提供本轮 BREP/STEP 查看，并明确标为部分待处理或分析失败；缺少本轮几何时不链接旧成功模型。未执行模型没有本轮刀路或几何产物。具体运行证据：${path.join(output,'summary.json')}。\n`);
process.stdout.write(`Completed: ${summary.passed}/${summary.selected} passed, ${summary.partial} partial, ${summary.failed} failed, ${summary.notRun} not-run.\n`);
if(summary.failed)process.exitCode=1;
