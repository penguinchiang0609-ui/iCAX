import test from 'node:test';
import assert from 'node:assert/strict';
import * as THREE from '../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {machinePoseSpaceChanged,renderMachineIK,renderMachineIKTransform,machineIKField,finishMachinePoseField,flushMachinePoseEdits,beginMachinePoseHudEdit,finishMachinePoseHudEdit} from '../../apps/five-axis-cam/webpage/machineIK.mjs';
import {selectTool} from '../../apps/five-axis-cam/webpage/workspaceModel.mjs';

const near=(actual,expected,tolerance=1e-9)=>{assert.equal(actual.length,expected.length);actual.forEach((value,i)=>assert.ok(Math.abs(Number(value)-expected[i])<tolerance,`${i}: ${value} != ${expected[i]}`));};
const row=matrix=>matrix.clone().transpose().toArray();
const rotatedWorld=(degrees,x,y,z)=>new THREE.Matrix4().makeRotationZ(degrees*Math.PI/180).setPosition(x,y,z);
const setWorld=(object,matrix)=>{object.matrix.copy(matrix);object.matrixWorldNeedsUpdate=true;object.updateWorldMatrix(true,false);};

function fixture(t){
 const parent=new THREE.Object3D(),tip=new THREE.Object3D();
 parent.matrixAutoUpdate=false;setWorld(parent,rotatedWorld(90,10,20,30));
 // Parent local (2,3,4), local Rz=30 gives world (7,22,34), Rz=120.
 tip.matrixAutoUpdate=false;setWorld(tip,rotatedWorld(120,7,22,34));
 const machine={entityId:'machine',enabled:true,elements:[{entityId:'machine',kind:'machine'},{entityId:'parent',parentEntityId:'machine',kind:'part'},{entityId:'tip',parentEntityId:'parent',kind:'part',name:'head'}],joints:[{entityId:'parent',type:'revolute',axis:[0,0,1],lower:-Math.PI,upper:Math.PI,position:0}]};
 const trace=[],requests=[],view={fiveAxisArea:'simulation',fiveMachineSelectedPartId:'tip',fiveMachine:{machineId:'machine'},fivePlacementSpace:'world',scene:{machines:[machine],intentToolpaths:[],domain:{expected:{epoch:'scene',mutationSequence:'0'},setups:[],placements:[],workpieces:[]}},fiveMachineIK:{mode:'translate',motionMode:'IK',machineId:'machine',tip:'tip',drafts:new Map()}};
 selectTool(view,'machine-instances');
 view.viewport={sceneObjects:new Map([['parent',parent],['tip',tip]]),transformPreviews:new Map(),transformPayloads:new Map(),setTransformSpace(space){trace.push({type:'space',space});this.space=space;}};
 const context={mount:{querySelector:()=>null,querySelectorAll:()=>[]},isCurrentProject:()=>true,project:{projectId:'project'},projectProxy:{state:{projectPath:'C:/fixture/space.i5cam'},async autosave(){trace.push({type:'save'});return {};}},actions:{refreshProjectHistoryControls(){},async refreshNativeProjectHistory(){}},sceneProxy:{async invoke(method,request){
  assert.equal(method,'CamSimulation.MoveMachineIK');requests.push(structuredClone(request));trace.push({type:'move',request});
  setWorld(tip,new THREE.Matrix4().set(...request.target));
  return {status:'Ready',collisionStatus:'Clear',positions:{},expected:{epoch:'scene',mutationSequence:String(requests.length)}};
 }}};
 const ops={renderProject(){trace.push({type:'render'});}};
 t.after(()=>{for(const draft of view.fiveMachineIK.drafts.values())clearTimeout(draft.timer);});
 return {view,machine,parent,tip,context,ops,trace,requests};
}

test('world and parent-local display use the selected top-level frame without world captions',async t=>{
 const f=fixture(t);let panel=renderMachineIK(f.view),hud=renderMachineIKTransform(f.view);
 near(f.view.fiveMachineIK.pose.values,[7,22,34,0,0,120]);assert.equal(f.view.fiveMachineIK.pose.space,'world');
 assert.doesNotMatch(panel+hud,/世界坐标/);assert.doesNotMatch(hud,/five-placement-hud|目标位置|目标角度/);
 await machinePoseSpaceChanged(f.context,f.view,'local',f.ops);
 panel=renderMachineIK(f.view);hud=renderMachineIKTransform(f.view);
 near(f.view.fiveMachineIK.pose.values,[2,3,4,0,0,30]);assert.equal(f.view.fiveMachineIK.pose.space,'local');
 near(f.view.fiveMachineIK.pose.frame.elements,f.parent.matrixWorld.elements);assert.doesNotMatch(panel+hud,/世界坐标/);
 await machinePoseSpaceChanged(f.context,f.view,'world',f.ops);renderMachineIK(f.view);
 near(f.view.fiveMachineIK.pose.values,[7,22,34,0,0,120]);assert.equal(f.requests.length,0,'changing only display coordinates must not move the machine');
});

test('a local numeric translation is submitted in world coordinates using the captured rotated parent',async t=>{
 const f=fixture(t);await machinePoseSpaceChanged(f.context,f.view,'local',f.ops);renderMachineIK(f.view);
 machineIKField(f.context,f.view,'ik-pose:0','5',f.ops);finishMachinePoseField(f.context,f.view,'ik-pose:0',f.ops);
 await f.view.fiveMachineIK.editTask;await f.view.fiveMachineIK.fileSaving;
 assert.equal(f.requests.length,1);near(f.requests[0].target,row(rotatedWorld(120,7,25,34)));
 assert.equal(f.view.fiveMachineIK.pose.dirty,false);assert.equal(f.trace.filter(e=>e.type==='save').length,1);
});

test('a local numeric angle is composed with the physical parent rotation',async t=>{
 const f=fixture(t);await machinePoseSpaceChanged(f.context,f.view,'local',f.ops);renderMachineIK(f.view);
 machineIKField(f.context,f.view,'ik-pose:5','60',f.ops);finishMachinePoseField(f.context,f.view,'ik-pose:5',f.ops);
 await flushMachinePoseEdits(f.context,f.view,f.ops);
 assert.equal(f.requests.length,1);near(f.requests[0].target,row(rotatedWorld(150,7,22,34)));
});

test('queued local targets keep their world interpretation when the parent or current display frame changes',async t=>{
 const f=fixture(t);await machinePoseSpaceChanged(f.context,f.view,'local',f.ops);renderMachineIK(f.view);
 let release;f.view.fiveMachineIK.inflight=new Promise(resolve=>release=resolve);
 machineIKField(f.context,f.view,'ik-pose:0','5',f.ops);finishMachinePoseField(f.context,f.view,'ik-pose:0',f.ops);
 const task=f.view.fiveMachineIK.editTask;
 setWorld(f.parent,rotatedWorld(-45,100,200,300));f.view.fivePlacementSpace='world';
 release();await task;await f.view.fiveMachineIK.fileSaving;
 assert.equal(f.requests.length,1);near(f.requests[0].target,row(rotatedWorld(120,7,25,34)));
});

test('switching coordinates flushes and saves the old world draft before rebuilding local values',async t=>{
 const f=fixture(t);renderMachineIK(f.view);machineIKField(f.context,f.view,'ik-pose:0','8',f.ops);
 await machinePoseSpaceChanged(f.context,f.view,'local',f.ops);
 assert.equal(f.requests.length,1);near(f.requests[0].target,row(rotatedWorld(120,8,22,34)));
 const saveIndex=f.trace.findIndex(e=>e.type==='save'),localIndex=f.trace.findIndex(e=>e.type==='space'&&e.space==='local');
 assert.ok(saveIndex>=0&&localIndex>saveIndex,'the old-frame edit must be persisted before the display frame changes');
 assert.equal(f.view.fiveMachineIK.spaceChanging,false);assert.equal(f.view.fiveMachineIK.drafts.size,0);renderMachineIK(f.view);
 near(f.view.fiveMachineIK.pose.values,[2,2,4,0,0,30]);assert.equal(f.view.fiveMachineIK.pose.space,'local');
});

test('rapid coordinate clicks apply only the latest choice after the original-frame edit is flushed once',async t=>{
 for(const clicks of [['local','world','local'],['local','world','local','world']])await t.test(clicks.join(' → '),async t=>{
  const f=fixture(t);renderMachineIK(f.view);machineIKField(f.context,f.view,'ik-pose:0','8',f.ops);
  let release,entered;const gate=new Promise(resolve=>release=resolve),started=new Promise(resolve=>entered=resolve),invoke=f.context.sceneProxy.invoke;
  f.context.sceneProxy.invoke=async(...args)=>{entered();await gate;return invoke(...args);};
  const change=machinePoseSpaceChanged(f.context,f.view,clicks[0],f.ops);await started;
  for(const space of clicks.slice(1))await machinePoseSpaceChanged(f.context,f.view,space,f.ops);
  const latest=clicks.at(-1);
  assert.equal(f.view.fiveMachineIK.spaceChanging,true);assert.equal(f.view.fiveMachineIK.nextSpace,latest);
  assert.equal(f.view.fivePlacementSpace,'world');assert.equal(f.view.viewport.space,'world','the old frame stays visible until its edit is committed');
  assert.equal(f.view.fiveMachineIK.pose.space,'world');assert.equal(f.view.fiveMachineIK.pose.values[0],'8');
  release();await change;
  assert.equal(f.requests.length,1,'coalescing display-frame clicks must not duplicate the pending native move');near(f.requests[0].target,row(rotatedWorld(120,8,22,34)));
  assert.equal(f.trace.filter(e=>e.type==='save').length,1);assert.equal(f.view.fivePlacementSpace,latest);assert.equal(f.view.viewport.space,latest);
  assert.equal(f.view.fiveMachineIK.spaceChanging,false);assert.equal(f.view.fiveMachineIK.nextSpace,undefined);
  renderMachineIK(f.view);near(f.view.fiveMachineIK.pose.values,latest==='local'?[2,2,4,0,0,30]:[8,22,34,0,0,120]);
  if(latest==='world')assert.equal(f.trace.some(e=>e.type==='space'&&e.space==='local'),false,'an obsolete local request must never be applied');
 });
});

test('unconfirmed floating input blocks a coordinate switch without changing or submitting its draft',async t=>{
 const f=fixture(t);renderMachineIK(f.view);assert.equal(beginMachinePoseHudEdit(f.view),true);
 machineIKField(f.context,f.view,'ik-pose:0','9',f.ops);const draft=f.view.fiveMachineIK.pose,values=[...draft.values];
 await assert.rejects(machinePoseSpaceChanged(f.context,f.view,'local',f.ops),/确认或取消/);
 assert.equal(f.view.fivePlacementSpace,'world');assert.equal(f.view.viewport.space,'world');assert.equal(f.view.fiveMachineIK.pose,draft);
 assert.deepEqual(draft.values,values);assert.ok(draft.hudEditing);assert.equal(f.requests.length,0);
 finishMachinePoseHudEdit(f.context,f.view,true,f.ops);
});

test('a space transition cannot accept a new input in the previous coordinate frame',t=>{
 const f=fixture(t);renderMachineIK(f.view);const draft=f.view.fiveMachineIK.pose,values=[...draft.values],revision=draft.revision;
 f.view.fiveMachineIK.spaceChanging=true;assert.equal(machineIKField(f.context,f.view,'ik-pose:0','99',f.ops),true);
 assert.deepEqual(draft.values,values);assert.equal(draft.revision,revision);assert.equal(draft.dirty,false);assert.equal(draft.timer,undefined);assert.equal(f.requests.length,0);
});

test('missing physical parent coordinates reject local mode instead of treating it as world',async t=>{
 const f=fixture(t);renderMachineIK(f.view);const draft=f.view.fiveMachineIK.pose;f.view.viewport.sceneObjects.delete('parent');
 await assert.rejects(machinePoseSpaceChanged(f.context,f.view,'local',f.ops),/父坐标尚未就绪/);
 assert.equal(f.view.fivePlacementSpace,'world');assert.equal(f.view.viewport.space,'world');assert.equal(f.view.fiveMachineIK.pose,draft);
 assert.equal(f.view.fiveMachineIK.spaceChanging,false);assert.equal(f.requests.length,0);
});
