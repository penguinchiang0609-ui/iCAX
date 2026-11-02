import test from 'node:test';
import assert from 'node:assert/strict';
import * as THREE from '../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {canAdjustMachinePart,canRotateMachinePart,selectMachinePoseTarget,machineIKCommand,machineIKField,flushMachinePoseEdits,renderMachineIK,renderMachineIKTransform,syncMachineIK} from '../../apps/five-axis-cam/webpage/machineIK.mjs';
import {commandAvailability} from '../../apps/five-axis-cam/webpage/commandAvailability.mjs';
import {selectTool} from '../../apps/five-axis-cam/webpage/workspaceModel.mjs';

function fixture(nodes,selected=nodes.at(-1)[0]){
 const elements=[{entityId:'machine',kind:'machine'},...nodes.map(([id,parent,type])=>({entityId:id,parentEntityId:parent,kind:'part',name:id}))];
 const joints=nodes.map(([id,parent,type,limits])=>({entityId:id,jointName:id,type,axis:[0,0,1],position:0,lower:-1,upper:1,...limits}));
 const machine={entityId:'machine',enabled:true,elements,joints};
 const calls=[],view={fiveAxisArea:'simulation',fiveMachineSelectedPartId:selected,fiveMachine:{machineId:'machine'},scene:{machines:[machine],intentToolpaths:[],domain:{expected:{epoch:'scene',mutationSequence:'0'},setups:[],placements:[],workpieces:[]}},fiveMachineIK:{mode:'rotate',ikMode:'rotate',preference:'IK',motionMode:'IK',machineId:'machine',tip:selected,drafts:new Map()}};
 view.viewport={sceneObjects:new Map(elements.map(e=>[e.entityId,new THREE.Object3D()])),setTransformManipulator(options){this.options=options;this.transformManipulator={};},clearTransformManipulator(){this.transformManipulator=null;}};
 selectTool(view,'machine-instances');
 const context={mount:{querySelector:()=>null,querySelectorAll:()=>[]},isCurrentProject:()=>true,sceneProxy:{invoke(){calls.push('invoke');throw Error('unexpected native invocation');}},actions:{refreshProjectHistoryControls(){calls.push('history');}}};
 const ops={renderProject(){calls.push('render');}};
 return {view,machine,calls,context,ops};
}
const tc=()=>fixture([
 ['bed','machine','fixed'],['X','bed','prismatic'],['Y','X','prismatic'],['Z','Y','prismatic'],['head','Z','fixed'],
 ['A','bed','revolute'],['C','A','revolute']
],'head');
const fieldIndices=html=>[...html.matchAll(/data-five-machine-field="ik-pose:(\d)"/g)].map(m=>Number(m[1]));
const hasRotate=html=>html.includes('data-five-command="five.machine-ik.rotate"');
const motionSwitches=html=>[...html.matchAll(/<button\b[^>]*data-transform-switch="(FK|IK)"[^>]*>/g)].map(([tag,id])=>({id,command:tag.match(/data-five-command="([^"]+)"/)?.[1],pressed:/aria-pressed="true"/.test(tag),disabled:/\sdisabled(?:[\s=>]|$)/.test(tag),tooltip:tag.match(/title="([^"]*)"/)?.[1],tag}));

test('TC fixed head follows XYZ ancestors and never inherits rotation from the sibling cradle',()=>{
 const {view,machine,context,ops}=tc();
 assert.equal(canAdjustMachinePart(view),true,'fixed attachment remains an IK translation target');
 assert.equal(canRotateMachinePart(view),false);
 machine.joints.find(j=>j.entityId==='A').position=.6;machine.joints.find(j=>j.entityId==='C').position=.8;
 assert.equal(canRotateMachinePart(view),false,'sibling joint positions cannot change the head capability');
 selectMachinePoseTarget(view);
 assert.equal(view.fiveMachineIK.motionMode,'IK');assert.equal(view.fiveMachineIK.mode,'translate','stale rotation preference must become a usable mode');
 assert.equal(commandAvailability(view,'five.machine-ik.translate').disabled,false);
 assert.equal(commandAvailability(view,'five.machine-ik.rotate').disabled,true);
 assert.deepEqual(fieldIndices(renderMachineIK(view)),[0,1,2]);assert.equal(hasRotate(renderMachineIK(view)),false);
 const hud=renderMachineIKTransform(view);assert.deepEqual(fieldIndices(hud),[]);assert.equal(hasRotate(hud),false);assert.doesNotMatch(hud,/five-placement-hud|目标位置/);
 // A stale mode can also arrive from a previously selected target before sync.
 view.fiveMachineIK.mode='rotate';syncMachineIK(context,view,ops);
 assert.equal(view.fiveMachineIK.mode,'translate');assert.equal(view.viewport.options.mode,'translate','the manipulator must instantiate translation geometry, rather than rotation rings');
});

test('unavailable rotation commands and angle fields cannot start native work or create a dirty pose',async()=>{
 const {view,context,ops,calls}=tc();selectMachinePoseTarget(view);renderMachineIK(view);
 const pose=view.fiveMachineIK.pose,before=[...pose.values],revision=pose.revision;
 await machineIKCommand(context,view,'rotate',ops);
 for(const i of [3,4,5])assert.equal(machineIKField(context,view,'ik-pose:'+i,'30',ops),true);
 assert.equal(view.fiveMachineIK.mode,'translate');assert.equal(view.fiveMachineIK.pose,pose);assert.deepEqual(pose.values,before);
 assert.equal(pose.dirty,false);assert.equal(pose.revision,revision);assert.equal(pose.timer,undefined);assert.deepEqual(calls,[]);
});

test('fixed children of real rotary chains retain target rotation across RC, LC and IR structures',async t=>{
 const cases=[
  ['RC fixed support',[['bed','machine','fixed'],['C','bed','continuous',{lower:undefined,upper:undefined}],['support','C','fixed']]],
  ['LC fixed laser head',[['X','machine','prismatic'],['Y','X','prismatic'],['Z','Y','prismatic'],['C','Z','revolute'],['A','C','revolute'],['head','A','fixed']]],
  ['IR fixed laser head',Array.from({length:6},(_,i)=>['J'+(i+1),i?'J'+i:'machine','revolute']).concat([['head','J6','fixed']])]
 ];
 for(const [name,nodes]of cases)await t.test(name,async()=>{
  const {view,context,ops,calls}=fixture(nodes);assert.equal(canRotateMachinePart(view),true);selectMachinePoseTarget(view);
  assert.equal(view.fiveMachineIK.mode,'rotate');assert.equal(commandAvailability(view,'five.machine-ik.rotate').disabled,false);
  assert.deepEqual(fieldIndices(renderMachineIK(view)),[0,1,2,3,4,5]);assert.equal(hasRotate(renderMachineIKTransform(view)),true);
  await machineIKCommand(context,view,'rotate',ops);assert.equal(view.fiveMachineIK.mode,'rotate');assert.deepEqual(calls,['render']);
 });
});

test('locked rotary joints and fixed roots do not advertise target rotation',()=>{
 const locked=fixture([['C','machine','revolute',{lower:.2,upper:.2}],['support','C','fixed']]);
 assert.equal(canRotateMachinePart(locked.view),false);assert.equal(commandAvailability(locked.view,'five.machine-ik.rotate').disabled,true);
 assert.deepEqual(fieldIndices(renderMachineIK(locked.view)),[0,1,2]);
 for(const selected of ['bed','machine']){
  const {view}=fixture([['bed','machine','fixed']],selected);assert.equal(canAdjustMachinePart(view),false);assert.equal(canRotateMachinePart(view),false);
  assert.equal(commandAvailability(view,'five.machine-ik.rotate').disabled,true);selectMachinePoseTarget(view);assert.equal(view.fiveMachineIK.mode,null);
  assert.equal(renderMachineIKTransform(view),'');
 }
});

function staleDraft(view,edited){
 const object=view.viewport.sceneObjects.get('head');object.rotation.set(0,0,.3);object.updateWorldMatrix(true,false);
 const pose={key:'machine:head',tip:'head',machineId:'machine',revision:3,values:['12','0','0','30','0','0'],dirty:true,edited:new Set(edited),editKey:'ik-pose:3',error:'previous unreachable rotation',requested:{values:[12,0,0,30,0,0],revision:3}};
 view.fiveMachineIK.drafts.set(pose.key,pose);view.fiveMachineIK.pose=pose;return pose;
}

test('a previous angle-only failure is cleared before flush and cannot block unrelated commands',async()=>{
 const {view,context,ops,calls}=tc(),pose=staleDraft(view,[3]);
 await flushMachinePoseEdits(context,view,ops);
 assert.equal(pose.dirty,false);assert.equal(pose.error,'');assert.equal(pose.editKey,null);assert.equal(pose.requested,null);
 assert.deepEqual([...pose.edited],[]);pose.values.slice(3).map(Number).forEach((v,i)=>assert.ok(Math.abs(v-[0,0,.3*180/Math.PI][i])<1e-12));
 assert.deepEqual(calls,[],'discarding an impossible old angle must not submit or save a machine mutation');
});

test('clearing an old angle failure preserves and resubmits a valid translation with the physical orientation',async t=>{
 const {view,context,ops}=tc(),pose=staleDraft(view,[0,3]),targets=[];
 view.fiveMachineIK.binding={context,ops};
 context.sceneProxy.invoke=async(method,args)=>{
  assert.equal(method,'CamSimulation.MoveMachineIK');targets.push(args.target);
  return {status:'Ready',collisionStatus:'Clear',positions:{},expected:{epoch:'scene',mutationSequence:'1'}};
 };
 t.after(()=>clearTimeout(pose.timer));
 renderMachineIK(view);
 assert.equal(view.fiveMachineIK.pose,pose);assert.equal(pose.values[0],'12');assert.deepEqual([...pose.edited],[0]);assert.equal(pose.dirty,true);assert.equal(pose.error,'');assert.ok(pose.timer,'the retained translation is requeued through the existing edit queue');
 await flushMachinePoseEdits(context,view,ops);
 assert.equal(targets.length,1);assert.equal(targets[0][3],12);assert.equal(pose.dirty,false);assert.equal(pose.error,'');
 const expected=new THREE.Matrix4().makeRotationZ(.3);expected.setPosition(12,0,0);const row=expected.transpose().toArray();
 targets[0].forEach((value,i)=>assert.ok(Math.abs(value-row[i])<1e-12,'target preserves the actual fixed head orientation'));
});

test('floating FK and IK switches change the selected joint interaction without changing native positions',async()=>{
 const {view,machine,context,ops,calls}=tc();view.fiveMachineSelectedPartId='Y';selectMachinePoseTarget(view);
 machine.joints.find(j=>j.entityId==='Y').position=.45;const before=structuredClone(machine.joints);
 for(const [command,active]of [['fk','FK'],['ik','IK'],['fk','FK']]){
  await machineIKCommand(context,view,command,ops);
  assert.equal(view.fiveMachineIK.motionMode,active);assert.equal(view.fiveMachineIK.mode,'translate');
  const html=renderMachineIKTransform(view),switches=motionSwitches(html);
  assert.match(html,/class="five-motion-switch"/);assert.deepEqual(switches.map(s=>s.id),['FK','IK']);
  assert.deepEqual(switches.map(s=>s.command),['five.machine-ik.fk','five.machine-ik.ik']);
  assert.deepEqual(switches.filter(s=>s.pressed).map(s=>s.id),[active]);assert.ok(switches.every(s=>!s.disabled));
  assert.ok(switches.every(s=>!s.tag.includes('data-transform-mode')&&!s.tag.includes('aria-keyshortcuts')),'motion switches must not claim W/E transform shortcuts');
  assert.equal(hasRotate(html),false,'XYZ ancestors still cannot advertise target rotation');
  assert.deepEqual(machine.joints,before,'choosing FK or IK only changes interaction state');
 }
 assert.deepEqual(calls,['render','render','render'],'switching reuses the existing command path without native mutation or history writes');
});

test('floating motion switches retain the existing IK transform choice and physical FK axis',async()=>{
 const {view,machine,context,ops,calls}=tc();view.fiveMachineSelectedPartId='A';selectMachinePoseTarget(view);
 const before=structuredClone(machine.joints);
 await machineIKCommand(context,view,'rotate',ops);await machineIKCommand(context,view,'fk',ops);
 assert.equal(view.fiveMachineIK.mode,'rotate');assert.deepEqual(motionSwitches(renderMachineIKTransform(view)).filter(s=>s.pressed).map(s=>s.id),['FK']);
 await machineIKCommand(context,view,'ik',ops);assert.equal(view.fiveMachineIK.mode,'rotate');assert.equal(hasRotate(renderMachineIKTransform(view)),true);
 await machineIKCommand(context,view,'translate',ops);await machineIKCommand(context,view,'fk',ops);assert.equal(view.fiveMachineIK.mode,'rotate','FK follows the physical rotary joint');
 await machineIKCommand(context,view,'ik',ops);assert.equal(view.fiveMachineIK.mode,'translate','returning to IK remembers its own transform choice');
 assert.deepEqual(machine.joints,before);assert.ok(calls.every(call=>call==='render'));
});

test('floating fixed-part FK switch is visibly unavailable while its upstream IK remains usable',async()=>{
 const {view,context,ops,calls}=tc();selectMachinePoseTarget(view);
 let switches=motionSwitches(renderMachineIKTransform(view));assert.equal(switches.length,2);
 const fk=switches.find(s=>s.id==='FK'),ik=switches.find(s=>s.id==='IK');
 assert.equal(fk.disabled,true);assert.match(fk.tooltip,/独立.*关节/);assert.equal(ik.disabled,false);assert.equal(ik.pressed,true);
 await machineIKCommand(context,view,'fk',ops);assert.equal(view.fiveMachineIK.motionMode,'IK');assert.equal(view.fiveMachineIK.mode,'translate');assert.deepEqual(calls,[]);
 view.pending=true;switches=motionSwitches(renderMachineIKTransform(view));assert.ok(switches.every(s=>s.disabled),'busy refresh must block mode switching');
});
