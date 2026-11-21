// Face-local boundary coverage after the existing logical-edge path Work.
// Never substitutes old successful output for a failed current invocation.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {spawn} from 'node:child_process';
import {fileURLToPath} from 'node:url';

const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../..');
const args=process.argv.slice(2),option=(key,fallback)=>args.includes(key)?args[args.indexOf(key)+1]:fallback;
const allowed=new Set(['--native','--path-root','--checkpoints','--output','--publish','--workers','--timeout','--models']);
for(let i=0;i<args.length;i+=2)if(!allowed.has(args[i])||!args[i+1])throw Error('Unknown or incomplete option: '+args[i]);
const output=path.resolve(option('--output',path.join(repo,'output/tests/skin-path-evidence-20261109/coverage-batch-v3')));
const native=path.resolve(option('--native',path.join(repo,'output/tests/skin-path-evidence-20261109/native-v3')));
const inputRoot=path.resolve(option('--path-root',path.join(repo,'TestData/Tube/PathAnalysis')));
const publish=path.resolve(option('--publish',path.join(output,'publish/FaceCutCoverage')));
const checkpoints=path.resolve(option('--checkpoints',path.join(repo,'TestData/Tube/PathAnalysis/cache-manifest.json')));
const workers=Number(option('--workers','4')),timeoutSeconds=Number(option('--timeout','240'));
if(path.basename(publish)!=='FaceCutCoverage')throw Error('Publication directory must be named FaceCutCoverage');
if(!Number.isInteger(workers)||workers<1||workers>8||!Number.isFinite(timeoutSeconds)||timeoutSeconds<=0)throw Error('Invalid workers or timeout');
const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
const sha=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const write=(p,value)=>{fs.mkdirSync(path.dirname(p),{recursive:true});const temp=p+'.tmp-'+crypto.randomUUID();fs.writeFileSync(temp,JSON.stringify(value,null,2));fs.renameSync(temp,p);};
const sourceManifest=read(path.join(inputRoot,'manifest.json')),cacheManifest=read(checkpoints);
const caches=new Map(cacheManifest.models.map(m=>[String(m.id),m]));
const selected=new Set(option('--models','').split(',').filter(Boolean));
const models=sourceManifest.models.filter(m=>!selected.size||selected.has(String(m.id))||selected.has(m.name));
if(!models.length)throw Error('No input models');
const producer=Object.fromEntries(['TubeStageProbe.exe','ExtrusionRecognition.dll','OpenCascadeResourceImport.dll'].map(name=>[name,{path:path.join(native,name),sha256:sha(path.join(native,name))}]));
const summary={schema:1,stage:'faceCutCoverage',status:'running',startedAt:new Date().toISOString(),selected:models.length,workers,timeoutSeconds,
 producer,inputPathManifest:{path:path.join(inputRoot,'manifest.json'),sha256:sha(path.join(inputRoot,'manifest.json'))},checkpointManifest:{path:checkpoints,sha256:sha(checkpoints)},rows:[]};
const manifest={schema:3,stage:'faceCutCoverage',coverageDomain:'source-skin-native-intervals',models:[],rules:{sources:'OnSkin only, per machining face',destination:'first actual boundary inside the same finite face along the native direction family; certified complete skin-boundary sets may prove exit existence without expanding correspondence',through:'proved SkinExit, complete SkinExitBoundarySet, non-skin ConvexBoundary, or endpoint-adjacent SideCompletion',partial:'proved concave termination with material retention and no valid endpoint-side completion',pending:'unresolved source interval only',convexEdges:'coverage evidence only; never additional paths',sideCompletion:'existing first-hit arrival component reaches a genuine incident source endpoint in the exact source or target-side native domain; all positive gaps retained; closed seams excluded'}};
const env={...process.env,PATH:[native,path.join(repo,'src/x64/Debug'),path.join(repo,'src/third_party/opencascade/install/vs2022-x64/win64/vc14/bind'),process.env.PATH].join(path.delimiter)};
const staging=path.join(output,'staging');
function save(){summary.rows.sort((a,b)=>a.id-b.id);manifest.models.sort((a,b)=>a.id-b.id);summary.completed=summary.rows.length;for(const status of ['passed','partial','failed','not-run'])summary[status]=summary.rows.filter(r=>r.status===status).length;write(path.join(output,'summary.json'),summary);write(path.join(publish,'manifest.json'),manifest);}
function nativeRun(nativeArgs,log){return new Promise(resolve=>{fs.mkdirSync(path.dirname(log),{recursive:true});const fd=fs.openSync(log,'w');let complete=false,timedOut=false;const child=spawn(path.join(native,'TubeStageProbe.exe'),nativeArgs,{cwd:path.join(repo,'src/x64/Debug'),env,windowsHide:true,stdio:['ignore',fd,fd]});const timer=setTimeout(()=>{timedOut=true;child.kill();},timeoutSeconds*1000);const finish=result=>{if(complete)return;complete=true;clearTimeout(timer);fs.closeSync(fd);resolve({...result,timedOut});};child.once('error',error=>finish({code:null,error:error.message}));child.once('exit',code=>finish({code}));});}
async function run(model){
 const began=Date.now(),id=Number(model.id),source=path.join(repo,'TestData/Tube/origin',model.name),cache=caches.get(String(id));
 const relative=decodeURIComponent(model.dataUrl.slice('/data/'.length)),inputFile=path.resolve(inputRoot,relative);
 if(!inputFile.startsWith(inputRoot+path.sep))throw Error('Input URL leaves path root');
 const input=read(inputFile),stem=String(id).padStart(3,'0')+'-'+path.parse(model.name).name;
 const base=path.join(staging,'FaceCutCoverage',model.name,'part-1');
 const row={id,name:model.name,source,status:'failed',producer,input:{path:inputFile,sha256:sha(inputFile),producer:input.run?.producer??model.producer},artifacts:{},counts:null};
 let document=null;
 try{
  row.sourceSha256=sha(source);
  if(!cache?.available){row.status='not-run';row.reason='前序 Classify 未完成，本轮没有可用的逐面刀路输入';}
  else{
   if(cache.originalCad.sha256!==row.sourceSha256||cache.originalCad.sha256!==input.run?.sourceSha256)throw Error('Original CAD identity differs from upstream path data');
   for(const name of ['split.icaxbrep','machining.icaxbrep','stock.icaxbrep','section-curves.icaxbrep','section-domains.json','performance.json']){const file=cache.files?.[name];if(!file||sha(file.path)!==file.sha256)throw Error('Classify checkpoint changed: '+name);}
   if(input.stage!=='pathAnalysis'||input.success!==true||!Array.isArray(input.edges))throw Error('No successful current PathAnalysis result');
   if(fs.existsSync(base+'.json'))throw Error('Refusing stale staged output: '+base);
   row.upstreamCheckpoint=cache;
   Object.assign(row,await nativeRun(['--face-cut-coverage-checkpoint',cache.directory,staging,model.name,'part-1'],path.join(output,'logs',String(id).padStart(3,'0')+'.log')));
   document=fs.existsSync(base+'.json')?read(base+'.json'):null;
   const correct=document?.schema===3&&document.coverageDomain==='source-skin-native-intervals'&&document.stage==='faceCutCoverage'&&Array.isArray(document.sources)&&Array.isArray(document.arrivals)&&Array.isArray(document.coverage)&&Array.isArray(document.evidence);
   if(row.timedOut)throw Error(`Native processing exceeded ${timeoutSeconds} seconds`);
   if(![0,1].includes(row.code))throw Error('Native process failed with exit status '+row.code+(row.error?': '+row.error:''));
   if(!correct||document.success!==true)throw Error(document?.error??document?.diagnostics?.join('; ')??'No usable current coverage output');
   row.status=document.complete===true&&row.code===0?'passed':document.complete===false&&row.code===1?'partial':'failed';row.counts=document.counts;
   if(row.status==='failed')throw Error('Complete output and native exit status disagree');
   if(!document.artifacts?.brepRoundTripPassed||!document.artifacts?.stepRoundTrip?.passed)throw Error('Current whole-model artifact round trip failed');
   for(const extension of ['.brep','.step'])if(!fs.existsSync(base+extension))throw Error('Current full-model artifact is missing: '+extension);
  }
 }catch(error){row.status='failed';row.error=error.message;}
 row.sourceUnchanged=!!row.sourceSha256&&sha(source)===row.sourceSha256;
 if(!row.sourceUnchanged){row.status='failed';row.error='Original CAD bytes changed during coverage analysis';}
 if(!['passed','partial'].includes(row.status))document={schema:3,stage:'faceCutCoverage',coverageDomain:'source-skin-native-intervals',name:model.name,success:false,complete:false,sources:[],arrivals:[],evidence:[],sideCorrespondences:[],coverage:[],faces:[],geometry:{faces:[]},incompleteFaceIds:[],counts:{},diagnostics:[row.reason??row.error??'本轮没有产物'],error:row.reason??row.error};
 document.run={status:row.status,producedThisRun:['passed','partial'].includes(row.status),sourceSha256:row.sourceSha256,sourceUnchanged:row.sourceUnchanged,producer,inputPath:row.input,upstreamCheckpoint:cache??null,error:row.error??null};
 const target=path.join(publish,stem+'.json');write(target,document);row.artifacts.data={path:target,sha256:sha(target)};
 if(['passed','partial'].includes(row.status))for(const [kind,extension]of [['brep','.brep'],['step','.step']]){const file=path.join(publish,stem+extension);fs.copyFileSync(base+extension,file);row.artifacts[kind]={path:file,sha256:sha(file)};}
 row.seconds=(Date.now()-began)/1000;summary.rows.push(row);manifest.models.push({id,name:model.name,status:row.status,reason:row.reason??row.error??null,dataUrl:'/data/FaceCutCoverage/'+encodeURIComponent(stem+'.json'),counts:row.counts,complete:document.complete,producer,sourceSha256:row.sourceSha256});save();
 console.log(`${summary.completed}/${models.length} ${id}: ${row.status}${row.error?' — '+row.error:''}`);
}
fs.mkdirSync(output,{recursive:true});fs.mkdirSync(publish,{recursive:true});save();let next=0;
await Promise.all(Array.from({length:Math.min(workers,models.length)},async()=>{while(next<models.length)await run(models[next++]);}));
for(const file of Object.values(producer))if(sha(file.path)!==file.sha256)throw Error('Producer changed during coverage run: '+file.path);
summary.status=summary.failed?'completed-with-failures':summary.partial||summary['not-run']?'completed-with-limitations':'passed';summary.completedAt=new Date().toISOString();summary.allSourceCadUnchanged=summary.rows.every(row=>row.sourceUnchanged===true);save();
fs.writeFileSync(path.join(publish,'README.md'),`# 皮边刀路的逐面切透分析\n\n本轮 ${summary.selected} 个模型：${summary.passed} 个完整，${summary.partial} 个部分完成，${summary.failed} 个执行失败，${summary['not-run']} 个因前序 Classify 缓存不可用而未执行。\n\n实际走刀只来自加工面的皮边。非皮凸边（包含凸侧边）保留为证据，不增加走刀或刀轴。沿原生刀轴方向族求同一有限面上的第一条真实边界，再将证据对应回来源皮边的原生参数区间。若已证明有效有界面域的全部真实边界均为来源皮边，也可独立证明出口存在；此类记录明确标注具体出口参数对应尚未展开，不伪造到达记录。\n\n- 绿色：切透，已有到达皮边、完整皮边出口集合、非皮凸边或有效端点侧边补全的证据。\n- 橙色：半切透，该区间已证明凹边终止、保留材料，且没有有效侧边补全。\n- 灰色粗线：判定待定，JSON 中保留具体原因。\n- 灰色细线：全部原加工边参考。\n\n同一闭合皮边的远侧区间可作为出口；参数接缝不作为侧边补全端点。侧边补全必须来自已证首达关系。可沿来源原生区间连通到共享真实端点；对于平面直线路径，也可由目标侧边自身的精确原生区间连通到同一来源路径的共享真实端点来证明已有到达记录。两域中的所有正宽缺口均保留。另一条路径未完成不改变当前已证明区间的判断。\n\n每个已执行模型仍输出一个完整 BREP、一份着色 STEP 和分析 JSON。证据保留 faceId、来源与对应原边、逻辑标识及原生参数；网页不另作几何推断。未支持的曲边映射明确待定，不以采样或拟合替代。原始 CAD 与 Classify 缓存保持不变。\n\n运行证据：${path.join(output,'summary.json')}\n`);
console.log(`Completed: ${summary.passed} passed, ${summary.partial} partial, ${summary.failed} failed, ${summary['not-run']} not run`);

if(summary.failed)process.exitCode=1;
