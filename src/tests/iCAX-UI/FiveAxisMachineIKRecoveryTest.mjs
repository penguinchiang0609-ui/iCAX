import test from 'node:test';
import assert from 'node:assert/strict';
import * as THREE from '../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {syncMachineIK,finishMachineIK,machineIKCommand,machineIKField,finishMachinePoseField,renderMachineIK} from '../../apps/five-axis-cam/webpage/machineIK.mjs';
import {selectTool} from '../../apps/five-axis-cam/webpage/workspaceModel.mjs';
import {encodeMachineIKResult,decodeMachineIKTarget} from './FiveAxisMachineIKPDOTestCodec.mjs';

const turn=()=>new Promise(resolve=>setImmediate(resolve));
const row=matrix=>matrix.clone().transpose().toArray();
const world=q=>new THREE.Matrix4().makeRotationZ(q);

function fixture(t){
 const originalFrame=globalThis.requestAnimationFrame,originalCancel=globalThis.cancelAnimationFrame,originalDocument=globalThis.document;
 const frames=new Map();let frameID=0;
 globalThis.requestAnimationFrame=callback=>{const id=++frameID;frames.set(id,callback);return id;};
 globalThis.cancelAnimationFrame=id=>frames.delete(id);
 const doc=new EventTarget();doc.defaultView=new EventTarget();doc.activeElement=null;globalThis.document=doc;
 t.after(()=>{globalThis.requestAnimationFrame=originalFrame;globalThis.cancelAnimationFrame=originalCancel;globalThis.document=originalDocument;});
 const object=new THREE.Object3D(),calls=[],receipts=new Map(),reader={pollingPaused:false,pauses:[],setPollingPaused(value){this.pollingPaused=value;this.pauses.push(value);},setPollInterval(){},async poll(){applyNative();}};
 const joint={entityId:'c',jointName:'C',type:'revolute',axis:[0,0,1],position:0,lower:-Math.PI,upper:Math.PI};
 const machine={entityId:'machine',enabled:true,elements:[{entityId:'machine',kind:'machine'},{entityId:'c',parentEntityId:'machine',kind:'part',name:'C'}],joints:[joint]};
 const view={fiveAxisArea:'simulation',fiveMachineSelectedPartId:'c',scene:{machines:[machine],intentToolpaths:[],domain:{expected:{epoch:'scene',mutationSequence:'0'},setups:[],placements:[],workpieces:[]}},fiveMachine:{machineId:'machine'},activeAreaId:'main',areas:{main:{viewReader:reader}},fiveMachineIK:{mode:'rotate',motionMode:'FK',machineId:'machine',tip:'c',drafts:new Map()}};
 selectTool(view,'machine-instances');
 const native={position:0,start:0,status:'Ready',reason:'',version:0,sequence:0,pendingCancel:0,descriptor:null,output:null,saves:0};
 const applyNative=()=>{joint.position=native.position;object.matrix.copy(world(native.position));object.matrixAutoUpdate=false;object.updateMatrixWorld(true);};applyNative();
 const vp=view.viewport={sceneObjects:new Map([['c',object]]),setTransformManipulator(options){this.options=options;this.transformManipulator={proxy:new THREE.Object3D(),finish(){}};this.transformManipulator.proxy.matrix.copy(object.matrix);this.transformManipulator.proxy.matrixAutoUpdate=false;this.transformManipulator.proxy.matrix.decompose(this.transformManipulator.proxy.position,this.transformManipulator.proxy.quaternion,this.transformManipulator.proxy.scale);},setTransformPreviews(matrices){for(const [id,values]of matrices)if(id==='c'){object.matrix.fromArray(values);object.updateMatrixWorld(true);}},clearTransformPreviews(){applyNative();},clearTransformManipulator(){}};
 const pdo={enabled:true,async withWriteDescriptor(descriptor,writer){
  const buffer=new ArrayBuffer(descriptor.payloadSize),length=writer(buffer),target=decodeMachineIKTarget(buffer.slice(0,length));native.sequence=Number(target.sequence);
  if(native.status==='Ready')native.position=target.jointPosition;
  const unavailable=['Error','Invalidated'].includes(native.status),d=native.descriptor,accepted=['Ready','Approximated'].includes(native.status);
  native.output=encodeMachineIKResult({editToken:d.editToken,epoch:d.epoch,resultSequence:String(++native.version),processedSequence:String(native.sequence),appliedSequence:accepted?String(native.sequence):'0',mutationSequence:'0',status:native.status,collisionStatus:accepted?'Clear':native.status==='Collision'?'Blocked':'Unchecked',reason:native.reason,positions:unavailable?[]:[native.position],tipWorld:unavailable?[]:row(world(native.position)),worldMatrices:unavailable?[]:row(world(native.position)),collisionPairs:native.collisionPairs||[],collisionDiagnosticsComplete:native.collisionDiagnosticsComplete===true});return true;
 },async withReadDescriptor(descriptor,read){return native.output?read(native.output,{dataVersion:String(native.version)}):null;}};
 const context={mount:{ownerDocument:doc,querySelector:()=>null,querySelectorAll:()=>[]},project:{projectId:'project'},projectProxy:{state:{projectPath:'C:/fixture/recovery.i5cam'},autosave:async()=>{native.saves++;return {};}},isCurrentProject:()=>true,actions:{refreshProjectHistoryControls(){},async refreshNativeProjectHistory(){}},sceneProxy:{pdo,async invoke(method,request){
  calls.push({method,request:structuredClone(request)});
  if(method==='CamSimulation.BeginMachinePoseEdit'){
   native.start=native.position;native.sequence=0;native.version=0;native.output=null;
   return native.descriptor={editToken:request.editToken,epoch:'scene',motionMode:request.motionMode,jointIds:['c'],worldIds:['c'],worldTransformIds:['c'],targetPDO:{id:'target',version:1,payloadSize:4096},resultPDO:{id:'result',version:1,payloadSize:4096}};
  }
  if(method==='CamSimulation.EndMachinePoseEdit'){
   if(receipts.has(request.editToken))return receipts.get(request.editToken);
   if(request.cancel&&native.pendingCancel-->0)return {pending:true};
   if(request.cancel)native.position=native.start;
   const receipt={pending:false,cancelled:!!request.cancel,changed:!request.cancel&&native.position!==native.start,expected:{epoch:'scene',mutationSequence:'0'}};receipts.set(request.editToken,receipt);return receipt;
  }
  if(method==='CamSimulation.MoveMachineIK'){native.position=.15;applyNative();return {status:'Ready',collisionStatus:'Clear',positions:{c:native.position},expected:{epoch:'scene',mutationSequence:'0'}};}
  throw Error(method);
 }}};
 const ops={renderProject(){},async refreshSceneState(){applyNative();return true;}};
 const sync=()=>syncMachineIK(context,view,ops);
 const begin=async(options)=>{sync();assert.equal(vp.options.enabled,true,'new gesture must be usable');assert.notEqual(vp.options.onStart(options),false);await view.fiveMachineIK.inflight;};
 const change=async(angle)=>{vp.options.onChange({translation:[0,0,0],angle});const [id,callback]=frames.entries().next().value;frames.delete(id);callback();await view.fiveMachineIK.gesture?.stream?.pulsing;await turn();await view.fiveMachineIK.finishing;};
 const idle=()=>{const s=view.fiveMachineIK;assert.equal(s.gesture,null);assert.equal(s.dragging,false);assert.equal(s.inflight,null);assert.equal(s.finishing,null);assert.equal(s.blocked,false);assert.equal(reader.pollingPaused,false);};
 t.after(async()=>{const s=view.fiveMachineIK;if(s.gesture){s.gesture.cancel=true;native.pendingCancel=0;await finishMachineIK(view);}await s.fileSaving;});
 return {view,context,ops,native,calls,reader,begin,change,sync,idle,vp,joint};
}

test('fatal PDO cancellation restores the last native state and the next FK gesture succeeds',async t=>{
 const f=fixture(t);await f.begin();f.native.status='Error';f.native.reason='controlled fatal channel failure';await f.change(.4);f.idle();assert.equal(f.native.position,0);assert.equal(f.view.fiveMachineIK.lastResult.status,'Error');assert.ok(f.calls.find(c=>c.method==='CamSimulation.EndMachinePoseEdit').request.cancel);
 f.native.status='Ready';f.native.reason='';await f.begin();await f.change(.2);await finishMachineIK(f.view);f.idle();assert.equal(f.native.position,.2);await f.view.fiveMachineIK.fileSaving;assert.equal(f.native.saves,1);
});

test('an unchecked budget result remains unchecked and accepts a later checked target in the same gesture',async t=>{
 const f=fixture(t);await f.begin();f.native.status='Unchecked';f.native.reason='COLLISION_DISTANCE_CANCELLED';await f.change(.4);assert.ok(f.view.fiveMachineIK.gesture);assert.equal(f.view.fiveMachineIK.blocked,false);assert.equal(f.native.position,0);assert.equal(f.view.fiveMachineIK.lastResult.collisionStatus,'Unchecked');assert.equal(f.calls.filter(c=>c.method==='CamSimulation.EndMachinePoseEdit').length,0);
 f.native.status='Ready';f.native.reason='';await f.change(.1);await finishMachineIK(f.view);f.idle();assert.equal(f.native.position,.1);
});

test('closest IK drag accepts an approximate PDO receipt and reports the actual axis angle',async t=>{
 const f=fixture(t);f.view.fiveMachineIK.motionMode='IK';await f.begin({axis:'Z'});
 assert.equal(f.calls.find(c=>c.method==='CamSimulation.BeginMachinePoseEdit').request.targetPolicy,'Closest');
 f.native.status='Approximated';f.native.reason='已到达行程边界。';f.native.position=.2;await f.change(.9);
 assert.ok(Math.abs(Number(f.view.fiveMachineIK.hud.value)-.2*180/Math.PI)<.001);assert.equal(f.joint.position,.2);assert.equal(f.view.fiveMachineIK.message,'已到达行程边界。');
 await finishMachineIK(f.view);f.idle();assert.equal(f.native.position,.2);await f.view.fiveMachineIK.fileSaving;assert.equal(f.native.saves,1);
});

test('a collision rejected target never becomes the native pose and retreat can succeed',async t=>{
 const f=fixture(t);await f.begin();f.native.status='Collision';f.native.reason='physical collision';await f.change(1);assert.equal(f.native.position,0);assert.equal(f.joint.position,0);assert.equal(f.view.fiveMachineIK.lastResult.collisionStatus,'Blocked');assert.equal(f.view.fiveMachineIK.blocked,false);
 f.native.status='Ready';f.native.reason='';await f.change(-.1);await finishMachineIK(f.view);f.idle();assert.equal(f.native.position,-.1);
});

test('structured collision PDO highlights the exact reported boxes and a later clear receipt removes them',async t=>{
 const f=fixture(t),machine=f.view.scene.machines[0];machine.collisions=['one','two','unused'].map((id,i)=>({id,entityId:'c',name:id,enabled:true,pose:[i*.1,0,0,0,0,0],geometry:{type:'box',size:[.08,.08,.08]}}));
 await f.begin();const endpoint=(boxId,boxIndex)=>({ownerElementId:'c',ownerElementName:'C',boxId,boxName:boxId,boxIndex,representation:'Box',editable:true});
 f.native.status='Collision';f.native.reason='controlled overlap';f.native.collisionPairs=[{first:endpoint('one',0),second:endpoint('two',1)}];f.native.collisionDiagnosticsComplete=true;await f.change(.5);
 const feedback=f.view.fiveMachineCollisionFeedback;assert.equal(feedback.validity,'Current');assert.equal(feedback.overlays.size,2);assert.deepEqual(feedback.refs.map(r=>r.boxId),['one','two']);assert.equal(f.native.position,0);
 f.native.status='Ready';f.native.collisionPairs=[];f.native.collisionDiagnosticsComplete=false;await f.change(.1);assert.ok(f.view.fiveMachineIK.gesture,f.view.fiveMachineIK.message);await f.view.fiveMachineIK.gesture.stream.pulse();assert.equal(!!f.view.fiveMachineCollisionFeedback,false);assert.equal(f.vp.sceneObjects.get('c').children.length,0);await finishMachineIK(f.view);f.idle();
});

test('pending cancellation keeps the token owned until existing End adjustment retries get a terminal receipt',async t=>{
 const f=fixture(t);await f.begin();f.native.status='Invalidated';f.native.reason='controlled invalidation';f.native.pendingCancel=4;await f.change(.3);assert.ok(f.view.fiveMachineIK.gesture);assert.equal(f.reader.pollingPaused,true);assert.match(renderMachineIK(f.view),/data-five-command="five.machine-ik.close"/);
 await machineIKCommand(f.context,f.view,'close',f.ops);assert.ok(f.view.fiveMachineIK.gesture,'pending cancellation cannot be presented as completed');assert.equal(f.view.fiveMachineIK.mode,'rotate','retry remains accessible');
 f.native.pendingCancel=0;await machineIKCommand(f.context,f.view,'close',f.ops);f.idle();assert.equal(f.native.position,0);assert.equal(f.view.fiveMachineIK.mode,null);assert.equal(f.native.saves,0);
});

test('numeric IK input can be retried after a cancelled gesture without an old failure draft disabling it',async t=>{
 const f=fixture(t);await f.begin();f.native.status='Error';f.native.reason='controlled fatal failure';await f.change(.4);f.idle();
 machineIKField(f.context,f.view,'ik-pose:0','15',f.ops);finishMachinePoseField(f.context,f.view,'ik-pose:0',f.ops);await f.view.fiveMachineIK.editTask;await f.view.fiveMachineIK.fileSaving;
 assert.equal(f.native.position,.15);assert.equal(f.view.fiveMachineIK.pose.dirty,false);assert.equal(f.view.fiveMachineIK.pose.error,'');assert.equal(f.view.fiveMachineIK.editTask,null);assert.equal(f.view.fiveMachineIK.inflight,null);
});
