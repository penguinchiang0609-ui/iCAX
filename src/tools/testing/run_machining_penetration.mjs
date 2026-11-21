// Replay native Works and publish their saved verdicts to the Work viewer.
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import {spawn} from 'node:child_process';
import {fileURLToPath} from 'node:url';

const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../..');
const tube=path.join(repo,'TestData/Tube');
const output=path.join(tube,'Penetration');
const pathRoot=path.join(tube,'PathAnalysis');
const args=new Map();
for(let i=2;i<process.argv.length;i+=2){
  const key=process.argv[i],value=process.argv[i+1];
  if(!['--native','--models','--workers','--timeout'].includes(key)||!value)throw Error('Invalid argument: '+key);
  args.set(key,value);
}
if(!args.has('--native'))throw Error('Pass --native with the directory containing TubeStageProbe.exe');
const native=path.resolve(args.get('--native'));
const probe=path.join(native,'TubeStageProbe.exe');
const concurrency=Number(args.get('--workers')??3),timeout=Number(args.get('--timeout')??120);
if(!Number.isInteger(concurrency)||concurrency<1||concurrency>8||!Number.isFinite(timeout)||timeout<=0)
  throw Error('Invalid worker count or timeout');
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const hash=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const write=(file,value)=>{
  fs.mkdirSync(path.dirname(file),{recursive:true});
  const temp=file+'-'+crypto.randomUUID()+'.tmp';
  fs.writeFileSync(temp,JSON.stringify(value,null,2));
  fs.renameSync(temp,file);
};
const url=file=>'/data/Penetration/'+path.relative(output,file).split(path.sep).map(encodeURIComponent).join('/');
const runId=new Date().toISOString().replaceAll(':','-')+'-'+crypto.randomUUID().slice(0,8);
const run=path.join(output,'runs',runId);
const modelIndex=read(path.join(pathRoot,'manifest.json')).models;
const caches=new Map(read(path.join(pathRoot,'cache-manifest.json')).models.map(cache=>[String(cache.id),cache]));
const selected=new Set((args.get('--models')??'').split(',').filter(Boolean));
const models=modelIndex.filter(model=>!selected.size||selected.has(String(model.id))||selected.has(model.name));
if(!models.length)throw Error('No matched models');
const producer=Object.fromEntries(['TubeStageProbe.exe','ExtrusionRecognition.dll','OpenCascadeResourceImport.dll']
  .map(name=>[name,hash(path.join(native,name))]));
const env={...process.env,PATH:[native,path.join(repo,'src/x64/Debug'),
  path.join(repo,'src/third_party/opencascade/install/vs2022-x64/win64/vc14/bind'),process.env.PATH].join(path.delimiter)};
const counters={through:0,blind:0,pending:0};

function execute(command,log){
  fs.mkdirSync(path.dirname(log),{recursive:true});
  return new Promise((resolve,reject)=>{
    const descriptor=fs.openSync(log,'w');
    const child=spawn(probe,command,{cwd:path.dirname(probe),env,windowsHide:true,stdio:['ignore',descriptor,descriptor]});
    let timedOut=false;
    const timer=setTimeout(()=>{timedOut=true;child.kill();},timeout*1000);
    child.once('error',error=>{clearTimeout(timer);fs.closeSync(descriptor);reject(error);});
    child.once('exit',code=>{
      clearTimeout(timer);
      fs.closeSync(descriptor);
      if(timedOut)reject(Error('Native Work exceeded '+timeout+' seconds'));
      else resolve(code);
    });
  });
}

const resultRows=[];
let next=0;
async function replay(model){
  const cache=caches.get(String(model.id));
  const row={id:model.id,name:model.name,status:'not-run'};
  if(!cache?.available)return {...row,reason:'缺少通过验证的 Classify 缓存'};
  try{
    const source=path.join(tube,'origin',model.name);
    const before=hash(source);
    if(before!==cache.originalCad.sha256||before!==model.sourceSha256)
      throw Error('当前原始 CAD 与 Classify 或预览来源不一致');
    for(const input of Object.values(cache.files)){
      if(hash(input.path)!==input.sha256)throw Error('Classify 缓存已变化 '+input.path);
    }
    const item=String(model.id);
    const report=path.join(run,'Reports',model.name,'part-1.json');
    const data=path.join(run,'Penetration',model.name,'part-1.json');
    if(path.basename(model.name)!==model.name)throw Error('Invalid model filename');
    const code=await execute(['--penetration-checkpoint',cache.directory,run,model.name,'part-1'],
      path.join(run,'logs',item+'.log'));
    if(hash(source)!==before)throw Error('运行前后原始 CAD 摘要有变化');
    if(!fs.existsSync(report))throw Error('Native Work did not write a report (exit '+code+')');
    const nativeReport=read(report),stage=nativeReport.stages?.penetration;
    if(![0,2].includes(code)||!stage?.checkpointCadUnchanged)
      throw Error(nativeReport.error??stage?.diagnostics?.join('；')??'切透判定执行失败');
    const outputData=read(data);
    if(outputData.stage!=='penetration'||outputData.success!==true||!Array.isArray(outputData.spans)||
        outputData.complete!==(code===0))throw Error('Native Work report and output disagree');
    const geometryFile=path.join(pathRoot,decodeURIComponent(model.dataUrl.slice('/data/'.length)));
    const preview=fs.existsSync(geometryFile)?read(geometryFile):null;
    const faceIds=new Set((preview?.faces??[]).map(face=>String(face.faceId)));
    const verifiedMesh=preview?.geometry?.completeModel&&
      preview.run?.upstreamCheckpoint?.files?.['split.icaxbrep']?.sha256===cache.files['split.icaxbrep'].sha256&&
      outputData.spans.every(span=>faceIds.has(span.faceId));
    for(const kind of Object.keys(counters))counters[kind]+=outputData.counts[kind];
    return {...row,status:stage.status,dataUrl:url(data),
      geometryUrl:verifiedMesh?'/data/PathAnalysis/'+model.dataUrl.slice('/data/'.length):null,
      previewStatus:verifiedMesh?'passed':'failed',previewReason:verifiedMesh?'':'缺少同一 Classify BREP 的预览',
      counts:outputData.counts,
      reason:outputData.complete?'':'保留待定区间，点选查看依据',
      artifacts:[{label:'判定结果 JSON',url:url(data)},{label:'原生运行报告',url:url(report)}],
      provenance:{run:runId,source:model.name,sourceSha256:before}};
  }catch(error){
    return {...row,status:'failed',reason:error.message};
  }
}

await Promise.all(Array.from({length:Math.min(concurrency,models.length)},async()=>{
  while(next<models.length){
    const row=await replay(models[next++]);
    resultRows.push(row);
    console.log(`${resultRows.length}/${models.length} ${row.id} ${row.status} ${JSON.stringify(row.counts??row.reason)}`);
  }
}));
resultRows.sort((a,b)=>a.id-b.id);

for(const [name,digest] of Object.entries(producer))
  if(hash(path.join(native,name))!==digest)throw Error('Native producer changed during the run: '+name);
const fixtures=path.join(output,'Fixtures');
const fixtureSummary=path.join(fixtures,'summary.json');
const verification=fs.existsSync(fixtureSummary)?read(fixtureSummary):null;
const fixtureRows=(verification?.fixtures??[]).map(fixture=>{
  const file=path.join(fixtures,fixture.name+'.json');
  const data=read(file);
  return {id:fixture.name,name:'验证样本 · '+fixture.name,status:data.complete?'passed':'partial',
    dataUrl:url(file),geometryUrl:url(file),counts:data.counts,fixturePassed:fixture.passed,
    reason:fixture.passed?'目标几何判定已通过验证':'目标几何判定仍有差异',
    artifacts:[{label:'判定结果 JSON',url:url(file)},{label:'样本验证明细',url:url(fixtureSummary)}]};
});
const summary={stage:'penetration',runId,producer,counts:counters,models:resultRows,fixtures:fixtureRows,
  verifiedFixtures:verification?.passed??null};
write(path.join(run,'summary.json'),summary);
write(path.join(output,'manifest.json'),{...summary,models:[...resultRows,...fixtureRows]});
console.log('Published '+url(path.join(output,'manifest.json')));
if(resultRows.some(row=>row.status==='failed'))process.exitCode=1;
