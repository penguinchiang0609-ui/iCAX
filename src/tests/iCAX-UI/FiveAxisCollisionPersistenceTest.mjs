import test from 'node:test';
import assert from 'node:assert/strict';
import {checkCollision} from '../../apps/five-axis-cam/webpage/simulationCollision.mjs';
import {machiningTaskTimeoutMs} from '../../apps/five-axis-cam/webpage/operationTiming.mjs';
import {encodeTaskProgress} from '../../apps/five-axis-cam/protocol/taskProgressPDOProtocol.mjs';

function fixture(status='Completed',save=async()=>{}) {
  let current=true,refreshes=0,saves=0;
  const context={isCurrentProject:()=>current,projectProxy:{state:{projectPath:'C:/test/collision.i5cam'},async autosave(path){saves++;assert.equal(path,this.state.projectPath);return save();}},sceneProxy:{async invoke(method){assert.equal(method,'CamSimulation.CheckCollision');return {status,epoch:'epoch',taskId:'task'};}}};
  const view={scene:{domain:{expected:{epoch:'epoch',mutationSequence:'1'},activeSetupId:'setup'}}},state={};
  const ops={renderProject(){},appendProjectLog(){},async refreshSceneState(){refreshes++;}};
  return {context,view,state,ops,current:()=>current,setCurrent(value){current=value;},get saves(){return saves;},get refreshes(){return refreshes;}};
}

test('completed exact collision report waits for the shared autosave before publishing its refreshed state',async()=>{
  let release;const f=fixture('Completed',()=>new Promise(resolve=>release=resolve));
  const pending=checkCollision(f.context,f.view,f.state,f.ops,f.current);
  while(!release)await new Promise(resolve=>setImmediate(resolve));
  assert.equal(f.saves,1);assert.equal(f.refreshes,0);assert.equal(f.view.pending,true);
  release();assert.equal(await pending,true);assert.equal(f.refreshes,1);assert.equal(f.state.checking,false);assert.equal(f.view.pending,false);
});

test('cancelled checks do not save or publish an incomplete report',async()=>{
  const f=fixture('Cancelled');assert.equal(await checkCollision(f.context,f.view,f.state,f.ops,f.current),false);
  assert.equal(f.saves,0);assert.equal(f.refreshes,0);assert.equal(f.state.message,'碰撞检测已取消');
});

test('a committed completed check saves its captured project after a switch without refreshing another page',async()=>{
  const f=fixture();f.context.sceneProxy.invoke=async()=>{f.setCurrent(false);return {status:'Completed'};};
  assert.equal(await checkCollision(f.context,f.view,f.state,f.ops,f.current),false);assert.equal(f.saves,1);assert.equal(f.refreshes,0);
});

test('autosave failure remains visible while the completed geometric report can still be read',async()=>{
  const f=fixture('Completed',async()=>{throw Error('disk unavailable');});
  assert.equal(await checkCollision(f.context,f.view,f.state,f.ops,f.current),true);
  assert.equal(f.saves,1);assert.equal(f.refreshes,1);assert.match(f.state.message,/自动保存失败.*disk unavailable/);
});

test('cold collider preparation and report publication use the machining task timeout',async()=>{
  const f=fixture(),calls=[];f.context.sceneProxy.pdo={enabled:true,async withReadDescriptor(_,reader){return reader(encodeTaskProgress({taskId:'task',epoch:'epoch',sequence:1,status:'Completed'}).buffer,{dataVersion:'1'});}};
  f.context.sceneProxy.invoke=async(method,_request,options)=>{calls.push(method);assert.equal(options.timeoutMs,machiningTaskTimeoutMs);return {status:method==='CamSimulation.CheckCollision'?'Running':'Completed',epoch:'epoch',taskId:'task',progressPDO:{id:'1'}};};
  assert.equal(await checkCollision(f.context,f.view,f.state,f.ops,f.current),true);
  assert.deepEqual(calls,['CamSimulation.CheckCollision','CamSimulation.QueryTask']);assert.equal(f.saves,1);
});

test('cancel while the selected preparation receipt is in flight never starts collision checking',async()=>{
  const f=fixture(),calls=[];let release;
  f.view.scene.domain.setups=[{id:'setup',generatedGeometry:{operations:[{intentId:'cut',status:'Stale'}]}}];
  f.context.sceneProxy.invoke=async(method,request)=>{calls.push({method,request});if(method==='CamPlan.Generate')return new Promise(r=>release=()=>r({status:'Cancelled',epoch:'epoch',taskId:'preparation'}));assert.equal(method,'CamPlan.CancelTask');return {cancelled:true};};
  const pending=checkCollision(f.context,f.view,f.state,f.ops,f.current,undefined,{intentId:'cut'});
  while(!release)await new Promise(r=>setImmediate(r));
  f.state.collisionCancelRequested=true;release();assert.equal(await pending,false);
  assert.deepEqual(calls.map(c=>c.method),['CamPlan.Generate','CamPlan.CancelTask']);assert.equal(f.state.message,'碰撞检测已取消');assert.equal(f.view.pending,false);assert.equal(f.saves,0);
});

test('retry after a lost selected preparation receipt uses the same captured command and expected revision',async()=>{
  const f=fixture(),requests=[];
  f.view.scene.domain.setups=[{id:'setup',generatedGeometry:{operations:[{intentId:'cut',status:'Stale'}]}}];
  f.context.sceneProxy.invoke=async(method,request)=>{assert.equal(method,'CamPlan.Generate');requests.push(structuredClone(request));if(requests.length===1)throw Error('lost acknowledgement');return {status:'Cancelled'};};
  assert.equal(await checkCollision(f.context,f.view,f.state,f.ops,f.current,undefined,{intentId:'cut'}),false);
  f.view.scene.domain.expected.mutationSequence='2';
  assert.equal(await checkCollision(f.context,f.view,f.state,f.ops,f.current,undefined,{intentId:'cut'}),false);
  assert.deepEqual(requests[1],requests[0]);assert.equal(requests[1].expected.mutationSequence,'1');assert.equal(f.state.collisionPreparationRequest,null);
});

test('global, cut and rapid checks send distinct native scopes',async()=>{
  const f=fixture(),requests=[];
  f.view.scene.domain.setups=[{id:'setup',generatedGeometry:{operations:[{intentId:'cut',status:'Current'}]}}];
  f.context.sceneProxy.invoke=async(method,request)=>{assert.equal(method,'CamSimulation.CheckCollision');requests.push(request);return {status:'Completed'};};
  for(const scope of [null,{intentId:'cut'},{from:'cut',to:'next'}])assert.equal(await checkCollision(f.context,f.view,f.state,f.ops,f.current,undefined,scope),true);
  const scopes=requests.map(({commandId,expected,...scope})=>scope);
  assert.deepEqual(scopes,[{setupId:'setup'},{setupId:'setup',intentId:'cut'},{setupId:'setup',from:'cut',to:'next'}]);
});

test('an unconfirmed single-path receipt cannot be reused by the global button',async()=>{
  const f=fixture();let calls=0;
  f.view.scene.domain.setups=[{id:'setup',generatedGeometry:{operations:[{intentId:'cut',status:'Current'}]}}];
  f.context.sceneProxy.invoke=async()=>{calls++;throw Error('lost acknowledgement');};
  await checkCollision(f.context,f.view,f.state,f.ops,f.current,undefined,{intentId:'cut'});
  const request=structuredClone(f.state.collisionRequest);
  assert.equal(await checkCollision(f.context,f.view,f.state,f.ops,f.current),false);
  assert.equal(calls,1);assert.deepEqual(f.state.collisionRequest,request);assert.match(f.state.message,/单条检测/);
});

test('an unconfirmed global receipt cannot turn a single-path retry into a global check',async()=>{
  const f=fixture();let calls=0;
  f.context.sceneProxy.invoke=async()=>{calls++;throw Error('lost acknowledgement');};
  await checkCollision(f.context,f.view,f.state,f.ops,f.current);
  const request=structuredClone(f.state.collisionRequest);
  assert.equal(await checkCollision(f.context,f.view,f.state,f.ops,f.current,undefined,{intentId:'cut'}),false);
  assert.equal(calls,1);assert.deepEqual(f.state.collisionRequest,request);assert.match(f.state.message,/顶部碰撞检测/);
});
