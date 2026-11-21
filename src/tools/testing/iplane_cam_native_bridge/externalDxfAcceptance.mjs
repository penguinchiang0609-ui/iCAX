// No UI or business fixtures: the production SDK calls the real framework
// bridge/runtime and product DLL, importing the user's existing external DXFs.
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {copyFileSync,existsSync,mkdirSync,readFileSync,readdirSync,statSync,unlinkSync,writeFileSync} from 'node:fs';
import {dirname,resolve} from 'node:path';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
import {connectApplication} from '../../../iCAX-UI/SDK/runtime.mjs';

const root=resolve(dirname(fileURLToPath(import.meta.url)),'../../../..');
const output=resolve(root,'output/tests/iplane-cam-external');mkdirSync(output,{recursive:true});
const executable=resolve(process.env.ICAX_PLANE_NATIVE_BRIDGE||resolve(root,'output/tests/iplane-cam-native/runtime/iPlaneCAMNativeBridge.exe'));
const source=resolve(root,'TestData/Plane/steel_frame_7_parts_2_nests_and_dstv_tests/vanthanbk/plate_dxf');
assert.ok(existsSync(executable),'Build and stage the native product bridge first.');
const files=readdirSync(source).filter(name=>/\.dxf$/i.test(name)).sort();assert.equal(files.length,7);
const importedDirectory=resolve(output,'imported-source-copies');mkdirSync(importedDirectory,{recursive:true});
for(const name of files)copyFileSync(resolve(source,name),resolve(importedDirectory,name));
const child=spawn(executable,['--data-dir',resolve(output,'native-user-data')],{cwd:dirname(executable),windowsHide:true,stdio:['pipe','pipe','pipe']});
let seq=0,closed=false,polling=false,stopping=false,app,timer;
const pending=new Map(),listeners=new Map(),stderr=[],checks=[],calls=[];
const report={pass:false,scope:'Production SDK -> existing FrontendBridge -> ApplicationRuntime -> iPlaneCAM ECS/SDO -> PlaneNestingServices -> iPlaneNesting; external DXF source files copied solely for save/reopen independence verification.',files:files.map(name=>({name,sha256:createHash('sha256').update(readFileSync(resolve(source,name))).digest('hex')}))};
child.stderr.on('data',bytes=>stderr.push(bytes.toString()));
child.on('exit',code=>{closed=true;for(const p of pending.values()){clearTimeout(p.timer);p.reject(Error('Native bridge exited '+code+': '+stderr.join('')));}pending.clear();});
createInterface({input:child.stdout}).on('line',line=>{
  let response;try{response=JSON.parse(line);}catch{stderr.push(line);return;}
  const p=pending.get(String(response.id));if(!p)return;clearTimeout(p.timer);pending.delete(String(response.id));
  response.ok?p.resolve(response.result):p.reject(Error(response.error));
});
function rpc(action,parameters={}){
  if(closed)return Promise.reject(Error('Bridge is closed'));
  const id=String(++seq);
  return new Promise((resolve,reject)=>{const timer=setTimeout(()=>{pending.delete(id);reject(Error('Native bridge timeout: '+action));},120000);pending.set(id,{resolve,reject,timer});child.stdin.write(JSON.stringify({id,action,...parameters})+'\n');});
}
async function poll(){
  if(stopping||polling)return;polling=true;
  try{for(const frame of (await rpc('poll')).frames??[])listeners.get(frame.channelId)?.forEach(listener=>listener(frame));}
  catch(error){if(!stopping)stderr.push(error.message);}
  finally{polling=false;if(!stopping)timer=setTimeout(poll,5);}
}
const bridge={
  async getApplicationChannelId(){return (await rpc('application-channel')).channelId;},
  async registerProductChannel(productId){return (await rpc('register-product',{productId})).channelId;},
  async registerSceneChannel(projectId,sceneId){return (await rpc('register-scene',{projectId,sceneId})).channelId;},
  subscribeSDOFrames(channel,listener){if(!listeners.has(channel))listeners.set(channel,new Set());listeners.get(channel).add(listener);return ()=>listeners.get(channel)?.delete(listener);},
  async postSDOFrame(frame){await rpc('post',{frame:{...frame,callId:String(frame.callId),methodCode:String(frame.methodCode)}});},
  async requestResource(request){const result=await rpc('resource',{request});return {status:result.status,headers:result.headers,body:Uint8Array.from(Buffer.from(result.bodyBase64,'base64')).buffer};},
};
const sleep=ms=>new Promise(resolve=>setTimeout(resolve,ms));
function passed(name){checks.push(name);}
try{
  app=await connectApplication({bridge,app:{sdo:{timeoutMs:120000}}});void poll();
  const application=await app.getState();assert.equal(application.products[0].productId,'icax.iplane-cam');
  const product=await app.startProduct('icax.iplane-cam');
  const opened=await product.openProjectCatalog('',{projectName:'外部 DXF 排样与工程验收'});
  const project=opened.projectProxy,scene=opened.sceneProxy;
  const query=()=>scene.invoke('PlaneScene.ReadState',{}, {timeoutMs:120000});
  const mutate=async(method,payload)=>{const state=await query();const result=await scene.invoke(method,{...payload,expectedRevision:state.revision},{timeoutMs:120000});calls.push({method,payload,result});return result;};
  await assert.rejects(()=>product.invoke('PlanePart.Import',{sourcePath:resolve(importedDirectory,files[0])}),/PLANE_SCENE_CHANNEL_REQUIRED/);
  passed('Framework product starts from its manifest and rejects domain edits on the product channel');
  for(const name of files)await mutate('PlanePart.Import',{sourcePath:resolve(importedDirectory,name),name,quantity:1});
  let state=await query();report.importedParts=state.iplaneCam.parts;assert.equal(state.iplaneCam.parts.length,7);
  assert.equal(new Set(state.iplaneCam.parts.map(part=>part.id)).size,7);
  // The source plates have four circular holes AND a small rectangular slot;
  // the filename names only the circles. Inspect the actual contours.
  assert.equal(state.iplaneCam.parts.filter(part=>part.name.startsWith('BasePlate')).length,3);
  assert.ok(state.iplaneCam.parts.filter(part=>part.name.startsWith('BasePlate')).every(part=>part.holeCount===5));
  assert.equal(state.iplaneCam.drawings.length,0);
  passed('Seven existing external plate/gusset DXFs import into distinct ECS parts, preserving holes and independent drawing state');
  await mutate('PlaneStock.CreateRectangle',{name:'300 × 300 库存',width:300,height:300,quantity:4});
  await mutate('PlaneStock.Import',{sourcePath:resolve(importedDirectory,files.find(name=>name.startsWith('BasePlate'))),name:'带五内孔的外部库存板',quantity:1});
  state=await query();assert.equal(state.iplaneCam.stocks.length,2);assert.ok(state.iplaneCam.stocks.some(stock=>stock.holeCount===5));
  await mutate('PlaneNesting.SetParameters',{partGap:2,stockMargin:3,rotationStepDeg:90,timeLimitSeconds:10,allowMirror:false});
  const started=await mutate('PlaneNesting.Run',{});let task;
  const deadline=Date.now()+120000;
  do{task=await scene.invoke('PlaneNesting.ReadTask',{taskId:started.taskId});if(!['Running','Pending','Queued','CancelRequested'].includes(task.status))break;await sleep(20);}while(Date.now()<deadline);
  assert.ok(['Completed','Partial'].includes(task.status),JSON.stringify(task));assert.ok(task.resultId);
  const result=await scene.invoke('PlaneNesting.ReadReport',{resultId:task.resultId});report.result=result;
  assert.equal(result.valid,true);assert.equal(Number(result.required),7);assert.equal(Number(result.placed),7);assert.equal(result.unplaced.length,0);
  assert.ok(result.boards.length>0);assert.equal(new Set(result.boards.map(board=>board.id)).size,result.boards.length);
  passed('Rectangular and DXF stock feed the real nesting service; all seven demanded parts are placed with conserved physical board identities');
  const projectFile=resolve(output,'external-dxf.ipcam');await project.save(projectFile);assert.ok(statSync(projectFile).size>0);
  const originalIds=(await query()).iplaneCam.parts.map(part=>part.id).sort();
  await product.closeProjectCatalog(opened.catalog.catalogId);
  // Remove only the exact test copies created above, never original inputs.
  for(const name of files)unlinkSync(resolve(importedDirectory,name));
  const reopened=await app.openProject(projectFile),restored=await reopened.sceneProxy.invoke('PlaneScene.ReadState');
  assert.deepEqual(restored.iplaneCam.parts.map(part=>part.id).sort(),originalIds);
  assert.equal(restored.iplaneCam.stocks.length,2);assert.ok(restored.iplaneCam.results.some(item=>item.id===task.resultId));
  const restoredReport=await reopened.sceneProxy.invoke('PlaneNesting.ReadReport',{resultId:task.resultId});
  assert.equal(restoredReport.placed,result.placed);assert.equal(restoredReport.utilization,result.utilization);assert.deepEqual(restoredReport.boards,result.boards);
  report.projectFile={path:projectFile,bytes:statSync(projectFile).size,sha256:createHash('sha256').update(readFileSync(projectFile)).digest('hex')};
  passed('Framework .ipcam save/close/reopen restores ECS identities, physical boards and reports after all imported test source copies are removed');
  await reopened.productProxy.closeProjectCatalog(reopened.catalog.catalogId);
  report.pass=true;
}finally{
  stopping=true;clearTimeout(timer);app?.dispose();
  if(!closed){try{await rpc('exit');}finally{child.stdin.end();}}
  Object.assign(report,{checks,calls,stderr});writeFileSync(resolve(output,'report.json'),JSON.stringify(report,null,2));
}
console.log(JSON.stringify({pass:report.pass,checks:checks.length,output}));
