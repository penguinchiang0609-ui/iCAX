import test from 'node:test';
import assert from 'node:assert/strict';
import * as THREE from '../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {syncMachineIK,finishMachineIK,flushMachinePoseEdits,disposeMachineIK} from '../../apps/five-axis-cam/webpage/machineIK.mjs';
import {selectTool} from '../../apps/five-axis-cam/webpage/workspaceModel.mjs';
import {encodeMachineIKResult,decodeMachineIKTarget} from './FiveAxisMachineIKPDOTestCodec.mjs';

const row=m=>m.clone().transpose().toArray();
const vector=m=>new THREE.Vector3().setFromMatrixPosition(m).toArray();
const until=async predicate=>{const end=Date.now()+3000;while(!predicate()){if(Date.now()>end)throw Error('queued gesture condition timed out');await new Promise(r=>setTimeout(r,2));}};
const near=(actual,expected)=>{assert.equal(actual.length,expected.length);for(let i=0;i<actual.length;i++)assert.ok(Math.abs(actual[i]-expected[i])<1e-6,`${i}: ${actual[i]} != ${expected[i]}`);};

function fixture(t){
 const previousFrame=globalThis.requestAnimationFrame,previousCancel=globalThis.cancelAnimationFrame;
 globalThis.requestAnimationFrame=callback=>setTimeout(callback,1);globalThis.cancelAnimationFrame=clearTimeout;
 const doc=new EventTarget();doc.defaultView=new EventTarget();doc.activeElement=null;
 const object=new THREE.Object3D(),proxy=new THREE.Object3D(),calls=[],history=[],events=[],terminal=new Map();object.matrixAutoUpdate=false;
 let actual=new THREE.Matrix4(),active=null,mutation=0,saves=0;
 const joint={entityId:'tip',type:'revolute',axis:[0,0,1],position:0,lower:-Math.PI,upper:Math.PI};
 const model={entityId:'machine',enabled:true,elements:[{entityId:'machine',kind:'machine'},{entityId:'tip',kind:'part',parentEntityId:'machine'}],joints:[joint]};
 const reader={pollingPaused:false,setPollingPaused(value){this.pollingPaused=value;},setPollInterval(){},async poll(){apply();}};
 const view={scene:{domain:{expected:{epoch:'queue',mutationSequence:'0'},setups:[],placements:[],workpieces:[]},machines:[model]},fiveAxisArea:'simulation',fiveMachine:{machineId:'machine'},fiveMachineSelectedPartId:'tip',fiveMachineIK:{machineId:'machine',tip:'tip',mode:'translate',motionMode:'IK'},activeAreaId:'main',areas:{main:{viewReader:reader}}};selectTool(view,'machine-instances');
 const f={view,calls,history,events,reader,holdPDO:false,holdEnd:false,holdBegin:false,pendingEnd:false,accept:(target)=>target};
 const apply=()=>{object.matrix.copy(actual);object.updateMatrixWorld(true);};apply();
 const vp=view.viewport={sceneObjects:new Map([['tip',object]]),pointerHeld:false,
  setTransformManipulator(options){this.options=options;this.transformManipulator??={proxy,finish:()=>{}};this.transformManipulator.options=options;if(!this.pointerHeld){proxy.position.fromArray(options.position);proxy.quaternion.fromArray(options.quaternion);proxy.scale.set(1,1,1);proxy.updateMatrixWorld(true);}},
  clearTransformManipulator(){this.transformManipulator=null;},setTransformPreviews(entries){for(const [id,values]of entries)if(id==='tip'){object.matrix.fromArray(values);object.updateMatrixWorld(true);}},clearTransformPreviews(){apply();}};
 const pdo={enabled:true,async withWriteDescriptor(slot,writer){if(!active)return false;const buffer=new ArrayBuffer(slot.payloadSize),size=writer(buffer);active.pending=decodeMachineIKTarget(buffer.slice(0,size));active.writes++;return true;},async withReadDescriptor(slot,read){
  const run=active;if(!run||f.holdPDO)return null;
  if(run.pending){const target=run.pending;run.pending=null;let requested;
   if(run.motionMode==='FK'){joint.position=target.jointPosition;requested=joint.type==='prismatic'?new THREE.Matrix4().makeTranslation(joint.position,0,0):new THREE.Matrix4().makeRotationZ(joint.position);}
   else requested=new THREE.Matrix4().set(...target.target);
   actual=f.accept(requested,run).clone();mutation++;run.processed=target.sequence;run.changed=true;run.output=encodeMachineIKResult({editToken:run.token,epoch:'queue',resultSequence:String(++run.version),processedSequence:run.processed,appliedSequence:run.processed,mutationSequence:String(mutation),status:'Ready',collisionStatus:'Clear',positions:[joint.position],tipWorld:row(actual),worldMatrices:row(actual)});}
  if(!run.output)return null;return read(run.output,{dataVersion:String(run.version)});
 }};
 const context={mount:{ownerDocument:doc,querySelector:()=>null,querySelectorAll:()=>[]},project:{projectId:'queue'},projectProxy:{state:{projectPath:'C:/fixture/queued.i5cam'},async autosave(){saves++;}},isCurrentProject:()=>true,actions:{refreshProjectHistoryControls(){},async refreshNativeProjectHistory(){}},sceneProxy:{pdo,async invoke(method,payload){
  calls.push({method,payload:structuredClone(payload)});
  if(method==='CamSimulation.BeginMachinePoseEdit'){
   assert.equal(active,null,'a new token cannot overlap the prior native transaction');active={token:payload.editToken,start:actual.clone(),startJoint:joint.position,motionMode:payload.motionMode,pending:null,processed:'0',version:0,writes:0,changed:false};events.push({kind:'begin',token:active.token});
   const descriptor={editToken:active.token,epoch:'queue',expected:{epoch:'queue',mutationSequence:String(mutation)},motionMode:payload.motionMode,jointIds:['tip'],positions:[joint.position],worldIds:['tip'],worldTransformIds:['tip'],tipWorld:row(actual),worldMatrices:row(actual),targetPDO:{id:'target',version:1,payloadSize:4096},resultPDO:{id:'result',version:1,payloadSize:4096}};
   if(f.holdBegin)await new Promise(resolve=>{f.releaseBegin=()=>{f.holdBegin=false;resolve();};});return descriptor;
  }
  assert.equal(method,'CamSimulation.EndMachinePoseEdit');if(terminal.has(payload.editToken))return terminal.get(payload.editToken);
  assert.equal(active?.token,payload.editToken);if(f.holdEnd&&!payload.cancel)await new Promise(resolve=>{f.releaseEnd=()=>{f.holdEnd=false;resolve();};});if(f.pendingEnd)return {pending:true};
  const run=active;if(!payload.cancel)assert.equal(payload.finalSequence,run.processed,'End must acknowledge the exact final PDO sample');else{actual=run.start.clone();joint.position=run.startJoint;}
  if(!payload.cancel&&run.changed)history.push(actual.clone());events.push({kind:'end',token:run.token,cancel:!!payload.cancel});active=null;
  const receipt={pending:false,changed:!payload.cancel&&run.changed,cancelled:!!payload.cancel,expected:{epoch:'queue',mutationSequence:String(mutation)}};terminal.set(run.token,receipt);if(f.loseEndAck&&!payload.cancel){f.loseEndAck=false;throw Error('controlled End response lost after native commit');}return receipt;
 }}};
 const sync=()=>{if(view.fiveMachineIK.mode)syncMachineIK(context,view,ops);};
 const ops={renderProject(){sync();},async refreshSceneState(){apply();sync();return true;},async refreshActiveAreaView(){apply();}};
 f.context=context;f.ops=ops;f.vp=vp;f.sync=sync;f.joint=joint;
 f.down=(axis='X',mode='translate')=>{view.fiveMachineIK.mode=mode;sync();assert.equal(vp.options.enabled,true);vp.pointerHeld=true;assert.notEqual(vp.options.onStart({axis,direction:[0,0,1]}),false,'the next pointer must be accepted locally');};
 f.move=(translation=[0,0,0],angle=0)=>{const p=view.fiveMachineIK.pointer;assert.ok(p);const position=new THREE.Vector3(),rotation=new THREE.Quaternion();p.downWorld.decompose(position,rotation,new THREE.Vector3());position.add(new THREE.Vector3(...translation));rotation.premultiply(new THREE.Quaternion().setFromAxisAngle(p.axisWorld,angle));proxy.position.copy(position);proxy.quaternion.copy(rotation);proxy.updateMatrixWorld(true);vp.options.onChange({translation,angle});};
 f.up=()=>{vp.pointerHeld=false;vp.options.onEnd({moved:true});};f.escape=()=>{vp.pointerHeld=false;vp.options.onCancel();};
 f.actual=()=>actual.clone();f.setActual=matrix=>{actual=matrix.clone();apply();sync();};f.state=()=>({active:active?.token,saves});sync();
 t.after(async()=>{f.releaseBegin?.();f.releaseEnd?.();f.pendingEnd=false;f.holdPDO=false;disposeMachineIK(view);await finishMachineIK(view);await view.fiveMachineIK.fileSaving;globalThis.requestAnimationFrame=previousFrame;globalThis.cancelAnimationFrame=previousCancel;});return f;
}

test('released X Y Z pointers queue as three isolated native transactions and preserve every displacement',async t=>{
 const f=fixture(t),s=f.view.fiveMachineIK;f.holdEnd=true;f.down('X');await s.inflight;f.move([2,0,0]);f.up();await until(()=>!!f.releaseEnd);
 f.down('Y');f.move([0,3,0]);f.up();f.down('Z');f.move([0,0,4]);f.up();assert.equal(s.pointerQueue.length,2);assert.equal(f.calls.filter(x=>x.method.includes('Begin')).length,1);
 f.releaseEnd();await flushMachinePoseEdits(f.context,f.view,f.ops);assert.equal(f.history.length,3);near(vector(f.history[0]),[2,0,0]);near(vector(f.history[1]),[2,3,0]);near(vector(f.history[2]),[2,3,4]);assert.deepEqual(f.events.map(x=>x.kind),['begin','end','begin','end','begin','end']);assert.equal(s.gesture,null);assert.equal(s.pointerQueue.length,0);assert.equal(f.reader.pollingPaused,false);
});

test('Escape for a waiting pointer cancels only that local gesture and leaves the prior released native edit committed',async t=>{
 const f=fixture(t),s=f.view.fiveMachineIK;f.holdEnd=true;f.down('X');await s.inflight;f.move([2,0,0]);f.up();await until(()=>!!f.releaseEnd);
 f.down('Y');f.move([0,3,0]);f.escape();assert.equal(s.pointerQueue.length,0);assert.equal(s.gesture.cancel,undefined);f.releaseEnd();await flushMachinePoseEdits(f.context,f.view,f.ops);
 assert.equal(f.history.length,1);near(vector(f.actual()),[2,0,0]);assert.equal(f.events.filter(x=>x.kind==='begin').length,1);assert.equal(f.events.find(x=>x.kind==='end').cancel,false);
});

test('a queued local translation uses the constrained orientation in the next native descriptor',async t=>{
 const f=fixture(t),s=f.view.fiveMachineIK;f.view.fivePlacementSpace='local';f.holdPDO=true;let firstToken;
 f.accept=(target,run)=>run.token===firstToken?new THREE.Matrix4().makeRotationZ(Math.PI/4).setPosition(0,0,1):target;
 f.down('Z');await s.inflight;firstToken=s.gesture.token;f.move([0,0,1]);f.up();f.down('X');f.move([5,0,0]);f.up();assert.equal(f.history.length,0);
 f.holdPDO=false;await flushMachinePoseEdits(f.context,f.view,f.ops);assert.equal(f.history.length,2,JSON.stringify({calls:f.calls,events:f.events,message:s.message,queue:s.pointerQueue.length}));near(vector(f.actual()),[5/Math.sqrt(2),5/Math.sqrt(2),1]);const direction=new THREE.Vector3(1,0,0).transformDirection(f.actual());near(direction.toArray(),[Math.SQRT1_2,Math.SQRT1_2,0]);
});

test('queued local rotations compose around the accepted current axis and retain the nonzero tip pivot',async t=>{
 const f=fixture(t),s=f.view.fiveMachineIK;f.setActual(new THREE.Matrix4().makeTranslation(10,20,30));f.view.fivePlacementSpace='local';f.holdPDO=true;let firstToken;
 f.accept=(target,run)=>run.token===firstToken?new THREE.Matrix4().makeRotationZ(Math.PI/4).setPosition(10,20,30):target;
 f.down('Z','rotate');await s.inflight;firstToken=s.gesture.token;f.move([0,0,0],Math.PI/2);f.up();f.down('X','rotate');f.move([0,0,0],Math.PI/6);f.up();f.holdPDO=false;
 await flushMachinePoseEdits(f.context,f.view,f.ops);assert.equal(f.history.length,2);const expected=new THREE.Matrix4().makeRotationZ(Math.PI/4).multiply(new THREE.Matrix4().makeRotationX(Math.PI/6)).setPosition(10,20,30);near(f.actual().toArray(),expected.toArray());
});

test('pending End preserves the original token and returns without spinning or promoting queued history',async t=>{
 const f=fixture(t),s=f.view.fiveMachineIK;f.pendingEnd=true;f.down('X');await s.inflight;const token=s.gesture.token;f.move([1,0,0]);f.up();f.down('Y');f.move([0,2,0]);f.up();await until(()=>!s.finishing&&s.gesture?.cancel);
 const before=f.calls.length;await finishMachineIK(f.view);assert.equal(s.gesture.token,token);assert.equal(s.pointerQueue.length,1);assert.equal(f.calls.filter(x=>x.method.includes('Begin')).length,1);assert.ok(f.calls.length-before<=2,'one explicit retry must not loop indefinitely');assert.equal(f.history.length,0);assert.equal(f.reader.pollingPaused,true);
});

test('a Begin response arriving after the selected machine changed cancels its token before any target is sent',async t=>{
 const f=fixture(t),s=f.view.fiveMachineIK;f.holdBegin=true;f.down('X');f.move([3,0,0]);f.up();await until(()=>!!f.releaseBegin);
 f.view.scene.machines.push({entityId:'other',enabled:true,elements:[{entityId:'other-tip',parentEntityId:'other',kind:'part'}],joints:[{entityId:'other-tip',type:'prismatic',position:0}]});f.view.fiveMachine.machineId='other';f.view.fiveMachineSelectedPartId='other-tip';f.releaseBegin();await finishMachineIK(f.view);
 assert.equal(f.history.length,0);near(vector(f.actual()),[0,0,0]);assert.equal(f.events.at(-1).cancel,true);assert.equal(s.gesture,null);assert.equal(f.reader.pollingPaused,false);
});

test('an FK translation keeps its pointer-down base across an accepted PDO repaint and panel resync',async t=>{
 const f=fixture(t),s=f.view.fiveMachineIK;f.joint.type='prismatic';f.joint.axis=[1,0,0];f.joint.lower=-100;f.joint.upper=100;s.motionMode='FK';
 f.down('Z');await s.inflight;f.move([2,0,0]);await until(()=>Math.abs(f.actual().elements[12]-2)<1e-9);f.sync();
 f.move([3,0,0]);near(f.vp.transformManipulator.proxy.position.toArray(),[3,0,0]);await until(()=>Math.abs(f.actual().elements[12]-3)<1e-9);f.up();await flushMachinePoseEdits(f.context,f.view,f.ops);near(vector(f.actual()),[3,0,0]);assert.equal(f.history.length,1);
});

test('an FK rotation keeps its pointer-down quaternion across an accepted PDO repaint and panel resync',async t=>{
 const f=fixture(t),s=f.view.fiveMachineIK;s.motionMode='FK';f.down('Z','rotate');await s.inflight;f.move([0,0,0],.2);await until(()=>Math.abs(f.joint.position-.2)<1e-9);f.sync();
 f.move([0,0,0],.3);const expected=new THREE.Quaternion().setFromAxisAngle(new THREE.Vector3(0,0,1),.3);assert.ok(f.vp.transformManipulator.proxy.quaternion.angleTo(expected)<1e-7);await until(()=>Math.abs(f.joint.position-.3)<1e-9);f.up();await flushMachinePoseEdits(f.context,f.view,f.ops);near(f.actual().toArray(),new THREE.Matrix4().makeRotationZ(.3).toArray());assert.equal(f.history.length,1);
});

test('a committed End whose response is lost retains its terminal receipt and its file-save obligation',async t=>{
 const f=fixture(t),s=f.view.fiveMachineIK;f.loseEndAck=true;f.down('X');await s.inflight;f.move([2,0,0]);f.up();await flushMachinePoseEdits(f.context,f.view,f.ops);
 assert.equal(f.history.length,1,'the first End already committed one native transaction');near(vector(f.actual()),[2,0,0]);assert.equal(f.calls.filter(x=>x.method.includes('Begin')).length,1);assert.equal(s.gesture,null);
 assert.equal(f.state().saves,1,'a cached committed receipt returned to cancel-retry still requires persistence');assert.equal(s.fileDirty,false);
});
