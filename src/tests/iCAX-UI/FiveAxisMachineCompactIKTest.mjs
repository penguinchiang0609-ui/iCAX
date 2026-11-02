import test from 'node:test';
import assert from 'node:assert/strict';
import * as THREE from '../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {selectMachineAxis,selectMachinePoseTarget,renderMachineIK,renderMachineIKTransform,machineIKField,finishMachinePoseHudEdit,flushMachinePoseEdits,machineIKCommand,machinePoseSpaceChanged,disposeMachineIK} from '../../apps/five-axis-cam/webpage/machineIK.mjs';
import {selectTool} from '../../apps/five-axis-cam/webpage/workspaceModel.mjs';

const near=(actual,expected,tolerance=1e-9)=>{assert.equal(actual.length,expected.length);actual.forEach((value,i)=>assert.ok(Math.abs(value-expected[i])<tolerance,`${i}: ${value} != ${expected[i]}`));};
const point=(matrix,p)=>new THREE.Vector3(...p).applyMatrix4(matrix).toArray();
const world=()=>new THREE.Matrix4().makeRotationZ(Math.PI/2).setPosition(10,20,30);
const setWorld=(object,matrix)=>{object.matrix.copy(matrix);object.matrixWorldNeedsUpdate=true;object.updateWorldMatrix(true,false);};

function fixture(t){
 const parent=new THREE.Object3D(),tip=new THREE.Object3D();parent.matrixAutoUpdate=tip.matrixAutoUpdate=false;setWorld(parent,new THREE.Matrix4());setWorld(tip,world());
 const machine={entityId:'machine',enabled:true,elements:[{entityId:'machine',kind:'machine'},{entityId:'parent',parentEntityId:'machine',kind:'part'},{entityId:'tip',parentEntityId:'parent',kind:'part',name:'tip'}],joints:['parent','tip'].map(entityId=>({entityId,type:'revolute',axis:[0,0,1],position:0,lower:-Math.PI,upper:Math.PI}))};
 const requests=[],saved=[],view={fiveAxisArea:'simulation',fiveMachineSelectedPartId:'tip',fiveMachine:{machineId:'machine'},fivePlacementSpace:'world',scene:{machines:[machine],intentToolpaths:[],domain:{expected:{epoch:'scene',mutationSequence:'0'},setups:[],placements:[],workpieces:[]}},fiveMachineIK:{mode:'translate',motionMode:'IK',preference:'IK',machineId:'machine',tip:'tip',drafts:new Map()}};
 selectTool(view,'machine-instances');
 view.viewport={sceneObjects:new Map([['parent',parent],['tip',tip]]),transformPayloads:new Map(),transformPreviews:new Map(),setTransformSpace(space){this.space=space;},clearTransformManipulator(){this.transformManipulator=null;}};
 const context={mount:{querySelector:()=>null,querySelectorAll:()=>[]},isCurrentProject:()=>true,project:{projectId:'project'},projectProxy:{state:{projectPath:'C:/fixture/compact-ik.i5cam'},async autosave(){saved.push(true);return {};}},actions:{refreshProjectHistoryControls(){},async refreshNativeProjectHistory(){}},sceneProxy:{async invoke(method,request){
  assert.equal(method,'CamSimulation.MoveMachineIK','the compact input keeps the existing checked IK command');requests.push(structuredClone(request));setWorld(tip,new THREE.Matrix4().set(...request.target));
  return {status:'Ready',collisionStatus:'Clear',positions:{},expected:{epoch:'scene',mutationSequence:String(requests.length)}};
 }}};
 const ops={renderProject(){}};
 t.after(()=>{for(const p of view.fiveMachineIK.drafts.values())clearTimeout(p.timer);});
 return {view,machine,parent,tip,context,ops,requests,saved};
}

async function accept(f,value){
 assert.equal(machineIKField(f.context,f.view,'ik-delta',value,f.ops),true);assert.equal(f.view.fiveMachineIK.pose.timer,null,'typing must not schedule a native mutation');
 assert.equal(finishMachinePoseHudEdit(f.context,f.view,false,f.ops),true);await flushMachinePoseEdits(f.context,f.view,f.ops);
}

test('IK starts with only controls, selecting an axis exposes one compact value and leaves sidebar inputs intact',t=>{
 const f=fixture(t),panel=renderMachineIK(f.view),before=[...f.view.fiveMachineIK.pose.values];
 const initial=renderMachineIKTransform(f.view);assert.doesNotMatch(initial,/five-placement-hud|<input\b/);assert.match(initial,/five-motion-switch/);
 selectMachineAxis(f.view,'X');const selected=renderMachineIKTransform(f.view);
 assert.equal((selected.match(/<input\b/g)||[]).length,1);assert.match(selected,/data-five-machine-field="ik-delta"/);assert.match(selected,/aria-label="位移"/);assert.doesNotMatch(selected,/目标位置|目标角度|five-transform-caption|ik-pose:/);
 assert.equal((panel.match(/data-five-machine-field="ik-pose:/g)||[]).length,6);assert.deepEqual(f.view.fiveMachineIK.pose.values,before);assert.equal(f.requests.length,0);
});

test('the compact field is consumed even before an axis is available and never starts setup editing',t=>{
 const f=fixture(t);assert.equal(machineIKField(f.context,f.view,'ik-delta','12',f.ops),true);assert.equal(f.view.fiveMachineIK.pose,undefined);assert.equal(f.requests.length,0);
});

test('world and local translation use the frozen selected direction and commit only after confirmation',async t=>{
 for(const [space,expected]of [['world',[15,20,30]],['local',[10,25,30]]])await t.test(space,async t=>{
  const f=fixture(t);f.view.fivePlacementSpace=space;selectMachineAxis(f.view,'X');const direction=[...f.view.fiveMachineIK.hud.direction];
  machineIKField(f.context,f.view,'ik-delta','5',f.ops);assert.equal(f.requests.length,0);near(point(f.tip.matrixWorld,[0,0,0]),[10,20,30]);
  assert.deepEqual(f.view.fiveMachineIK.hud.direction,direction);finishMachinePoseHudEdit(f.context,f.view,false,f.ops);await flushMachinePoseEdits(f.context,f.view,f.ops);
  assert.equal(f.requests.length,1);near(point(f.tip.matrixWorld,[0,0,0]),expected);near(point(f.tip.matrixWorld,[1,0,0]),[expected[0],expected[1]+1,expected[2]]);assert.equal(f.saved.length,1);
  assert.equal(finishMachinePoseHudEdit(f.context,f.view,false,f.ops),false);await flushMachinePoseEdits(f.context,f.view,f.ops);assert.equal(f.requests.length,1,'repeated Enter without new typing is a no-op');
 });
});

test('world and local rotation are degrees about the selected origin and screen rotation captures its world axis',async t=>{
 const cases=[
  {space:'world',axis:'Z',direction:undefined,expectedX:[9,20,30],expectedY:[10,19,30]},
  {space:'local',axis:'X',direction:undefined,expectedX:[10,21,30],expectedY:[10,20,31]},
  {space:'local',axis:'E',direction:[0,0,-2],expectedX:[11,20,30],expectedY:[10,21,30]}
 ];
 for(const c of cases)await t.test(c.space+'/'+c.axis,async t=>{
  const f=fixture(t);f.view.fivePlacementSpace=c.space;f.view.fiveMachineIK.mode='rotate';selectMachineAxis(f.view,c.axis,c.direction);
  const frozen=[...f.view.fiveMachineIK.hud.direction];if(c.direction)c.direction[2]=1;assert.deepEqual(f.view.fiveMachineIK.hud.direction,frozen);
  assert.match(renderMachineIKTransform(f.view),/aria-label="角度"/);await accept(f,'90');near(point(f.tip.matrixWorld,[0,0,0]),[10,20,30]);near(point(f.tip.matrixWorld,[1,0,0]),c.expectedX);near(point(f.tip.matrixWorld,[0,1,0]),c.expectedY);assert.equal(f.requests.length,1);
 });
});

test('invalid compact text stays a draft and Escape restores the previous accepted value',async t=>{
 const f=fixture(t);selectMachineAxis(f.view,'X');machineIKField(f.context,f.view,'ik-delta','-',f.ops);assert.equal(f.requests.length,0);assert.match(f.view.fiveMachineIK.pose.error,/有效的位移/);
 assert.throws(()=>finishMachinePoseHudEdit(f.context,f.view,false,f.ops),/有效的位移/);assert.ok(f.view.fiveMachineIK.pose.hudEditing);
 assert.equal(finishMachinePoseHudEdit(f.context,f.view,true,f.ops),true);assert.equal(f.view.fiveMachineIK.hud.value,'0');assert.equal(f.view.fiveMachineIK.pose.dirty,false);assert.equal(f.requests.length,0);
 await accept(f,'2');assert.equal(f.requests.length,1);machineIKField(f.context,f.view,'ik-delta','bad',f.ops);finishMachinePoseHudEdit(f.context,f.view,true,f.ops);
 assert.equal(f.view.fiveMachineIK.hud.value,'2');assert.equal(f.view.fiveMachineIK.pose.dirty,false);near(point(f.tip.matrixWorld,[0,0,0]),[12,20,30]);assert.equal(f.requests.length,1);
});

test('native rejection keeps the accepted pose and Escape restores the prior compact value before a valid retry',async t=>{
 for(const [status,collisionStatus]of [['Unreachable','Unchecked'],['Collision','Blocked']])await t.test(status,async t=>{
  const f=fixture(t);selectMachineAxis(f.view,'X');await accept(f,'2');renderMachineIK(f.view);
  const acceptedWorld=[...f.tip.matrixWorld.elements],acceptedValues=[...f.view.fiveMachineIK.pose.values],invoke=f.context.sceneProxy.invoke;let reject=true;
  f.context.sceneProxy.invoke=async(method,request)=>{
   if(!reject)return invoke(method,request);
   assert.equal(method,'CamSimulation.MoveMachineIK');f.requests.push(structuredClone(request));
   return {status,collisionStatus,reason:'controlled rejected axis target',positions:{},expected:{epoch:'scene',mutationSequence:'1'}};
  };
  machineIKField(f.context,f.view,'ik-delta','7',f.ops);finishMachinePoseHudEdit(f.context,f.view,false,f.ops);
  await assert.rejects(flushMachinePoseEdits(f.context,f.view,f.ops),/controlled rejected axis target/);
  assert.equal(f.requests.length,2);assert.equal(f.saved.length,1);near(f.tip.matrixWorld.elements,acceptedWorld);
  assert.equal(f.view.fiveMachineIK.hud.value,'7');assert.ok(f.view.fiveMachineIK.pose.hudEditing,'the rejected confirmation retains its cancel snapshot');assert.equal(f.view.fiveMachineIK.pose.confirmingHud,undefined);
  f.view.error='controlled rejected axis target';assert.equal(finishMachinePoseHudEdit(f.context,f.view,true,f.ops),true);
  assert.equal(f.view.error,'');assert.equal(f.view.fiveMachineIK.hud.value,'2');assert.deepEqual(f.view.fiveMachineIK.pose.values,acceptedValues);assert.equal(f.view.fiveMachineIK.pose.error||'','');assert.equal(f.view.fiveMachineIK.pose.dirty,false);assert.equal(f.view.fiveMachineIK.pose.hudEditing,undefined);
  assert.equal(f.requests.length,2,'Escape never submits another target');near(f.tip.matrixWorld.elements,acceptedWorld);
  reject=false;await accept(f,'3');assert.equal(f.requests.length,3);assert.equal(f.saved.length,2);near(point(f.tip.matrixWorld,[0,0,0]),[13,20,30]);assert.equal(f.view.fiveMachineIK.pose.confirmingHud,undefined);
 });
});

test('manual numeric IK requests closest and displays the checked actual value instead of the unreachable request',async t=>{
 const f=fixture(t);selectMachineAxis(f.view,'X');
 f.context.sceneProxy.invoke=async(method,request)=>{
  assert.equal(method,'CamSimulation.MoveMachineIK');assert.equal(request.targetPolicy,'Closest');f.requests.push(structuredClone(request));
  const actual=world().setPosition(14,20,30);setWorld(f.tip,actual);
  return {status:'Approximated',collisionStatus:'Clear',reason:'已到达行程边界。',tipWorld:actual.clone().transpose().toArray(),positions:{},expected:{epoch:'scene',mutationSequence:'1'}};
 };
 await accept(f,'1000');assert.equal(f.requests.length,1);assert.equal(f.saved.length,1);assert.equal(f.view.fiveMachineIK.hud.value,'4');
 assert.deepEqual(f.view.fiveMachineIK.pose.values.slice(0,3).map(Number),[14,20,30]);assert.equal(f.view.fiveMachineIK.pose.dirty,false);assert.equal(f.view.fiveMachineIK.pose.hudEditing,undefined);
 assert.equal(f.view.fiveMachineIK.message,'已到达行程边界。');assert.match(renderMachineIKTransform(f.view),/value="4"/);
 assert.equal(finishMachinePoseHudEdit(f.context,f.view,false,f.ops),false);await flushMachinePoseEdits(f.context,f.view,f.ops);assert.equal(f.requests.length,1);
});

test('an approximate result without completed collision proof remains rejected',async t=>{
 const f=fixture(t);selectMachineAxis(f.view,'X');f.context.sceneProxy.invoke=async()=>({status:'Approximated',collisionStatus:'Unchecked',reason:'检查未完成',positions:{}});
 machineIKField(f.context,f.view,'ik-delta','1000',f.ops);finishMachinePoseHudEdit(f.context,f.view,false,f.ops);
 await assert.rejects(flushMachinePoseEdits(f.context,f.view,f.ops),/检查未完成/);assert.equal(f.saved.length,0);assert.ok(f.view.fiveMachineIK.pose.hudEditing);
});

test('sidebar pose, joint and installation fields retire the previous compact delta reference',t=>{
 const f=fixture(t);
 for(const field of ['joint:tip','origin:x']){selectMachineAxis(f.view,'X');assert.ok(f.view.fiveMachineIK.hud);assert.equal(machineIKField(f.context,f.view,field,'1',f.ops),false);assert.equal(f.view.fiveMachineIK.hud,null);}
 selectMachineAxis(f.view,'X');assert.equal(machineIKField(f.context,f.view,'ik-pose:0','13',f.ops),true);assert.equal(f.view.fiveMachineIK.hud,null);assert.equal(f.view.fiveMachineIK.pose.values[0],'13');assert.equal(f.requests.length,0);
});

test('changing mode, coordinate frame or selected target clears the previous compact axis reference',async t=>{
 const f=fixture(t);selectMachineAxis(f.view,'X');assert.ok(f.view.fiveMachineIK.hud);await machineIKCommand(f.context,f.view,'rotate',f.ops);assert.equal(f.view.fiveMachineIK.hud,null);
 selectMachineAxis(f.view,'Y');await machinePoseSpaceChanged(f.context,f.view,'local',f.ops);assert.equal(f.view.fiveMachineIK.hud,null);
 selectMachineAxis(f.view,'X');await machineIKCommand(f.context,f.view,'fk',f.ops);assert.equal(f.view.fiveMachineIK.hud,null);assert.equal(f.view.fiveMachineIK.motionMode,'FK');await machineIKCommand(f.context,f.view,'ik',f.ops);assert.equal(f.view.fiveMachineIK.hud,null);
 selectMachineAxis(f.view,'Z');f.view.fiveMachineSelectedPartId='parent';selectMachinePoseTarget(f.view);assert.equal(f.view.fiveMachineIK.hud,null);
 f.view.fiveMachineSelectedPartId='tip';selectMachinePoseTarget(f.view);selectMachineAxis(f.view,'X');disposeMachineIK(f.view);assert.equal(f.view.fiveMachineIK.hud,null);assert.equal(f.requests.length,0);
});

test('plane and free translation handles do not present a misleading scalar value',t=>{
 const f=fixture(t);for(const axis of ['XY','XZ','YZ','XYZ']){selectMachineAxis(f.view,'X');assert.ok(f.view.fiveMachineIK.hud);selectMachineAxis(f.view,axis);assert.equal(f.view.fiveMachineIK.hud,null);assert.doesNotMatch(renderMachineIKTransform(f.view),/<input\b|five-placement-hud/);}assert.equal(f.requests.length,0);
});
