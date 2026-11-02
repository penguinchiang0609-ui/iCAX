import test from 'node:test';
import assert from 'node:assert/strict';
import * as THREE from '../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {selectTool} from '../../apps/five-axis-cam/webpage/workspaceModel.mjs';
import {syncMachineIK,finishMachineIK,flushMachinePoseEdits} from '../../apps/five-axis-cam/webpage/machineIK.mjs';
import {encodeMachineIKResult,decodeMachineIKTarget} from './FiveAxisMachineIKPDOTestCodec.mjs';

globalThis.requestAnimationFrame=callback=>setTimeout(callback,1);
globalThis.cancelAnimationFrame=clearTimeout;
const matrix=x=>[1,0,0,x,0,1,0,0,0,0,1,0,0,0,0,1];
const until=async predicate=>{const deadline=Date.now()+1500;while(!predicate()){if(Date.now()>deadline)throw Error('condition timeout');await new Promise(resolve=>setTimeout(resolve,2));}};
function fixture(){
 const calls=[],paused=[],writes=[],processed=[];let polls=0,saves=0,history=0,refreshes=0,hold=true,busy=false,pending=null,result=null,version=0n,mutation=1n,accepted=0,beginRelease=null;
 const m={entityId:'machine',enabled:true,elements:[{entityId:'tip',parentEntityId:'machine'}],joints:[{entityId:'tip',type:'prismatic',position:0}]};
 const object=new THREE.Object3D(),proxy=new THREE.Object3D(),status={textContent:''},document={activeElement:null};
 const view={scene:{domain:{expected:{epoch:'epoch',mutationSequence:'1'}},machines:[m]},fiveAxisArea:'simulation',fiveMachine:{machineId:'machine'},fiveMachineSelectedPartId:'tip',fiveMachineIK:{mode:'translate',machineId:'machine',tip:'tip'},activeAreaId:'main',areas:{main:{viewReader:{setPollInterval(){},setPollingPaused(value){paused.push(value);},async poll(){polls++;}}}},viewport:{sceneObjects:new Map([['tip',object]]),setTransformManipulator(config){this.config=config;this.transformManipulator={proxy};},setTransformPreviews(entries){for(const [,values] of entries){object.matrix.fromArray(values);object.matrix.decompose(object.position,object.quaternion,object.scale);object.updateMatrixWorld(true);}},clearTransformPreviews(){}}};selectTool(view,'machine-instances');
 const f={view,calls,paused,writes,processed,status,rejectTarget:()=>false,holdBegin:false};
 const descriptor={editToken:'',epoch:'epoch',expected:{epoch:'epoch',mutationSequence:'1'},jointIds:['tip'],positions:[0],worldIds:['tip'],worldTransformIds:['tip'],worldMatrices:matrix(0),targetPDO:{id:'input',version:1,payloadSize:1024},resultPDO:{id:'output',version:1,payloadSize:4096}};
 const pdo={enabled:true,async withWriteDescriptor(slot,writer){if(busy)return false;const buffer=new ArrayBuffer(slot.payloadSize),size=writer(buffer),target=decodeMachineIKTarget(buffer.slice(0,size));assert.equal(slot.dataVersion,target.sequence);writes.push(target);pending=target;return true;},async withReadDescriptor(slot,read){
  if(!hold&&pending){const target=pending;pending=null;processed.push(target);const rejected=f.rejectTarget(target),fatal=rejected==='Invalidated';if(!rejected){accepted=target.target[3];mutation++;}
   result={editToken:target.editToken,epoch:target.epoch,resultSequence:String(++version),processedSequence:target.sequence,appliedSequence:rejected?result?.appliedSequence||'0':target.sequence,mutationSequence:String(mutation),status:rejected||'Ready',collisionStatus:rejected?'Blocked':'Clear',reason:rejected?'target rejected':'',positions:fatal?[]:[accepted],tipWorld:fatal?[]:matrix(accepted),worldMatrices:fatal?[]:matrix(accepted)};
  }
  if(!result)return null;const bytes=encodeMachineIKResult(result);return read(bytes.buffer.slice(bytes.byteOffset,bytes.byteOffset+bytes.byteLength),{dataVersion:String(version)});
 }};
 const context={mount:{ownerDocument:document,querySelector:selector=>selector==='[data-machine-ik-status]'?status:null,querySelectorAll:()=>[]},project:{projectId:'project'},projectProxy:{state:{projectPath:'C:/test/ik.i5cam'},async autosave(){saves++;}},isCurrentProject:()=>true,actions:{refreshProjectHistoryControls(){},async refreshNativeProjectHistory(){history++;}},sceneProxy:{pdo,async invoke(method,payload){calls.push({method,payload:structuredClone(payload)});
  if(method==='CamSimulation.BeginMachinePoseEdit'){if(f.holdBegin)await new Promise(resolve=>beginRelease=resolve);if(f.beginError)throw Error(f.beginError);return {...descriptor,editToken:payload.editToken};}
  assert.equal(method,'CamSimulation.EndMachinePoseEdit','pointer samples must never invoke MoveMachineIK');if(!payload.cancel)assert.ok(BigInt(payload.finalSequence)<=BigInt(result?.processedSequence||0),'End must follow the final PDO ACK');
  return {changed:accepted!==0,cancelled:payload.cancel,expected:{epoch:'epoch',mutationSequence:String(mutation)}};
 }}};
 const ops={renderProject(){},async refreshSceneState(){refreshes++;}};syncMachineIK(context,view,ops);
 Object.assign(f,{context,move:x=>{proxy.position.x=x;proxy.updateMatrixWorld(true);view.viewport.config.onChange();},release:()=>{hold=false;beginRelease?.();},setBusy:value=>busy=value});
 Object.defineProperties(f,{polls:{get:()=>polls},saves:{get:()=>saves},history:{get:()=>history},refreshes:{get:()=>refreshes},actualX:{get:()=>object.position.x}});return f;
}

test('pointer flood overwrites both snapshots and directly paints accepted PDO matrices without per-point SDO/View reads',async()=>{
 const f=fixture(),s=f.view.fiveMachineIK;f.view.viewport.config.onStart();await s.inflight;
 for(let x=1;x<=100;x++)f.move(x);await until(()=>f.writes.length===1);assert.equal(f.writes[0].target[3],100);assert.equal(f.calls.length,1);assert.deepEqual(f.paused,[true]);assert.equal(f.polls,0);
 for(let x=101;x<=200;x++){f.move(x);if(x%25===0)await new Promise(resolve=>setTimeout(resolve,3));}
 assert.equal(f.processed.length,0);f.release();await until(()=>s.lastResult?.processedSequence===String(s.gesture.stream.sequence));
 assert.equal(f.actualX,200);assert.equal(f.view.scene.machines[0].joints[0].position,200);assert.equal(f.processed.length,1,'native consumes only latest published snapshot');assert.equal(f.polls,0);assert.equal(f.saves,0);
 f.view.viewport.config.onEnd();await finishMachineIK(f.view);await s.fileSaving;
 assert.deepEqual(f.calls.map(c=>c.method),['CamSimulation.BeginMachinePoseEdit','CamSimulation.EndMachinePoseEdit']);assert.equal(f.polls,1);assert.equal(f.history,1);assert.equal(f.saves,1);assert.equal(s.gesture,null);assert.equal(s.lastStreamStats.processedSequence,'200');assert.ok(s.lastStreamStats.overwrittenTargets>150);assert.deepEqual(f.paused,[true,false]);
});

test('pointerup and immediate Save wait for Begin, a busy input slot and the final output acknowledgement',async()=>{
 const f=fixture(),s=f.view.fiveMachineIK;f.holdBegin=true;f.setBusy(true);f.view.viewport.config.onStart();f.move(1);f.move(3);f.view.viewport.config.onEnd();const flushing=flushMachinePoseEdits(f.context,f.view,{});await until(()=>f.calls.length===1);f.release();await until(()=>s.gesture?.stream?.stats.busyWrites>0);
 assert.equal(f.calls.length,1);f.setBusy(false);await flushing;assert.deepEqual(f.processed.map(t=>t.target[3]),[3]);assert.equal(f.calls[1].payload.finalSequence,'1');assert.equal(f.saves,1);assert.equal(s.fileDirty,false);assert.equal(s.gesture,null);
});

test('Esc cancels the same scope without waiting for an unprocessed IK target',async()=>{
 const f=fixture(),s=f.view.fiveMachineIK;f.view.viewport.config.onStart();await s.inflight;f.move(1);await until(()=>f.writes.length>0);f.move(2);f.view.viewport.config.onCancel();await finishMachineIK(f.view);
 assert.equal(f.calls.at(-1).payload.cancel,true);assert.equal(f.processed.length,0);assert.equal(f.refreshes,1);assert.equal(f.saves,0);assert.equal(s.gesture,null);
});

test('a rejected target is acknowledged and stays adjustable; only accepted poses appear in the scene',async()=>{
 const f=fixture(),s=f.view.fiveMachineIK;f.rejectTarget=t=>t.target[3]===2?'Blocked':false;f.release();f.view.viewport.config.onStart();await s.inflight;f.move(1);await until(()=>s.lastResult?.processedSequence==='1');f.move(2);await until(()=>s.lastResult?.processedSequence==='2');
 assert.equal(f.actualX,1);assert.equal(s.dragging,true);assert.equal(s.blocked,false);assert.match(f.status.textContent,/rejected/);f.move(3);await until(()=>s.lastResult?.processedSequence==='3');assert.equal(f.actualX,3);f.view.viewport.config.onEnd();await finishMachineIK(f.view);await s.fileSaving;assert.equal(f.saves,1);
});

test('a second pointer press cannot replace an unacknowledged finishing gesture',async()=>{
 const f=fixture(),s=f.view.fiveMachineIK;f.view.viewport.config.onStart();await s.inflight;const token=s.gesture.token;f.move(1);f.view.viewport.config.onEnd();assert.equal(f.view.viewport.config.onStart(),false);assert.equal(s.gesture.token,token);f.move(2);f.release();await finishMachineIK(f.view);await s.fileSaving;assert.deepEqual(f.processed.map(t=>t.target[3]),[1]);assert.equal(f.calls.at(-1).payload.editToken,token);
});

test('external configuration invalidation cancels instead of committing or painting obsolete matrices',async()=>{
 const f=fixture(),s=f.view.fiveMachineIK;f.rejectTarget=()=>'Invalidated';f.release();f.view.viewport.config.onStart();await s.inflight;f.move(1);await until(()=>s.gesture===null);await s.finishing;assert.equal(f.calls.at(-1).payload.cancel,true);assert.equal(f.actualX,0);assert.equal(f.saves,0);assert.equal(f.refreshes,1);assert.match(s.message,/rejected/);
});

test('accepted PDO gesture persists even if final View or history refresh fails',async()=>{
 for(const failing of ['view','history']){const f=fixture(),s=f.view.fiveMachineIK,previews=new Set(),paint=f.view.viewport.setTransformPreviews;f.view.viewport.setTransformPreviews=entries=>{for(const [id]of entries)previews.add(id);paint(entries);};f.view.viewport.clearTransformPreviews=ids=>{for(const id of ids)previews.delete(id);};f.release();if(failing==='view')f.view.areas.main.viewReader.poll=async()=>{throw Error('view read failed');};else f.context.actions.refreshNativeProjectHistory=async()=>{throw Error('history read failed');};f.view.viewport.config.onStart();await s.inflight;f.move(1);f.view.viewport.config.onEnd();await finishMachineIK(f.view);await s.fileSaving;assert.equal(f.calls.length,2);assert.equal(f.saves,1);assert.equal(s.gesture,null);assert.equal(s.fileDirty,false);assert.match(s.message,/read failed/);assert.equal(f.paused.at(-1),false);assert.equal(previews.size,0,'failed presentation must not mask later View or Undo poses');}
});

test('failed Begin responses and a failed PDO initialization reliably cancel their token and resume the reader',async()=>{
 for(const failing of ['begin','pdo']){const f=fixture(),s=f.view.fiveMachineIK;if(failing==='begin')f.beginError='Begin timed out';else f.context.sceneProxy.pdo.enabled=false;
  f.view.viewport.config.onStart();await finishMachineIK(f.view);assert.deepEqual(f.calls.map(c=>c.method),['CamSimulation.BeginMachinePoseEdit','CamSimulation.EndMachinePoseEdit']);assert.equal(f.calls.at(-1).payload.cancel,true);assert.equal(s.gesture,null);assert.equal(f.paused.at(-1),false);assert.equal(f.saves,0);assert.equal(f.refreshes,1);
 }
});

test('a gesture pauses hidden readers in its own project and preserves readers already paused by another owner',async()=>{
 const f=fixture(),events=[],s=f.view.fiveMachineIK;
 f.view.areas.hidden={viewReader:{pollingPaused:false,setPollingPaused(value){events.push(['hidden',value]);}}};
 f.view.areas.alreadyPaused={viewReader:{pollingPaused:true,setPollingPaused(value){events.push(['alreadyPaused',value]);}}};
 f.release();f.view.viewport.config.onStart();await s.inflight;assert.deepEqual(events,[['hidden',true]]);f.move(1);f.view.viewport.config.onEnd();await finishMachineIK(f.view);await s.fileSaving;assert.deepEqual(events,[['hidden',true],['hidden',false]]);assert.deepEqual(f.paused,[true,false]);
});
