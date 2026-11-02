import test from 'node:test';
import assert from 'node:assert/strict';
import * as THREE from '../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {machineField,stopMachine,renderMachineDock,renderCamMachinePlayback} from '../../apps/five-axis-cam/webpage/machineSimulation.mjs';
import {simulationField,simulationCommand,pauseSimulation} from '../../apps/five-axis-cam/webpage/trajectorySimulation.mjs';
import {camMachinePlayback,machineViewportActive,camHeadElements} from '../../apps/five-axis-cam/webpage/machinePlaybackContext.mjs';
import {renderMachineTree} from '../../apps/five-axis-cam/webpage/machineTree.mjs';
import {workpieceViewSnapshot,workpieceViewSnapshotApplied} from '../../apps/five-axis-cam/webpage/workpieceViewSnapshot.mjs';

const identity=[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1];
const translated=(x,y,z)=>[1,0,0,x,0,1,0,y,0,0,1,z,0,0,0,1];
function fixture(t){
 const cancel=globalThis.cancelAnimationFrame,doc=globalThis.document;
 globalThis.cancelAnimationFrame=()=>{};globalThis.document={activeElement:null,hidden:false};
 const setup={id:'setup',name:'管件装夹',machineSetup:{machineId:'machine',supportId:'chuck',toolId:'head',placement:translated(0,3,0),tcp:[0,0,0],beam:[0,0,-1]}};
 const domain={activeSetupId:'setup',setups:[setup],workpieces:[{id:'part',name:'管件'}],placements:[{id:'pipe',workpieceId:'part',setupId:'setup',transform:translated(5,1,2)}],fixtures:[{id:'fixture',name:'夹爪',setupId:'setup',transform:translated(1,2,0)}]};
 const scene=new THREE.Scene(),objects=new Map();
 for(const id of ['root','chuck','head','pipe','fixture']){const o=new THREE.Group();scene.add(o);objects.set(id,o);}
 const machine={entityId:'machine',name:'管材机床',elements:[{entityId:'root',kind:'machine',name:'床身'},{entityId:'chuck',parentEntityId:'root',name:'旋转卡盘'},{entityId:'head',parentEntityId:'root',name:'刀头'}],joints:[]};
 const report={validity:'Current',playbackAllowed:true,status:'Ready',collisionStatus:'Clear',configuration:setup.machineSetup,support:1,tool:2,modelKey:'model',
  nodes:[{id:'root',parent:-1,type:'fixed',axis:[0,0,1],origin:identity},{id:'chuck',parent:0,type:'revolute',axis:[1,0,0],origin:identity},{id:'head',parent:0,type:'prismatic',axis:[0,0,1],origin:translated(0,0,20)}],
  frames:[{joints:[0,0,0],durationSeconds:0,laser:false},{joints:[0,Math.PI/2,-2],durationSeconds:1,laser:true,motion:{interpolation:'QuinticNormalized',coefficients:[[0,0,0,0,0,0],[0,0,0,5*Math.PI,-7.5*Math.PI,3*Math.PI],[0,0,0,-20,30,-12]]}}]};
 const view={fiveAxisArea:'toolpath',activeAreaId:'toolpath',fiveAxisUI:{tools:{toolpath:'simulation',simulation:'machine-simulation'},selectionSnapshot:domain},scene:{domain,machines:[machine]},viewport:{scene,sceneObjects:objects},fiveViewportFrame:'cam-head'};
 view.fiveMachine={cursor:0,speed:1,originals:new Map(),report,machineId:'machine',setupKey:JSON.stringify([setup.id,setup.machineSetup]),snapshot:report};
 const context={isCurrentProject:()=>true,mount:{querySelector:()=>null}},ops={renderProject(){},appendProjectLog(){}};
 t.after(()=>{stopMachine(view,true);globalThis.cancelAnimationFrame=cancel;globalThis.document=doc;});
 return {view,setup,domain,objects,report,machine,context,ops};
}
const near=(actual,expected)=>actual.forEach((x,i)=>assert.ok(Math.abs(x-expected[i])<1e-10,`${i}: ${x} != ${expected[i]}`));
test('CAM and machine playback rotate both mounted bodies by the same certified polynomial without editing CAD',t=>{
 const f=fixture(t),before=structuredClone(f.domain),originals=new Map([...f.objects].map(([id,o])=>[id,o.matrix.toArray()]));
 simulationField(f.context,f.view,'seek',.25,f.ops);
 const q=(10*.25**3-15*.25**4+6*.25**5)*Math.PI/2;
 // Independent analytic rotation about X, including support placement and each local part transform.
 for(const [id,[x,y,z]] of [['pipe',[5,4,2]],['fixture',[1,5,0]]])near(f.objects.get(id).position.toArray(),[x,y*Math.cos(q)-z*Math.sin(q),y*Math.sin(q)+z*Math.cos(q)]);
 const cam=new Map([...f.objects].map(([id,o])=>[id,o.matrix.toArray()]));
 assert.equal(f.view.fiveSimulation,undefined,'bound CAM must never create a second stationary-workpiece player');
 f.view.fiveAxisArea='simulation';machineField(f.context,f.view,'seek',.25,f.ops);
 for(const id of ['head','pipe','fixture'])near(f.objects.get(id).matrix.toArray(),cam.get(id));
 assert.deepEqual(f.domain,before);
 stopMachine(f.view,true);for(const [id,o] of f.objects){near(o.matrix.toArray(),originals.get(id));assert.equal(o.matrixAutoUpdate,true);}
});
test('standalone head playback drives workpiece and fixture on B around X and restores original CAD display',t=>{
 const f=fixture(t);delete f.setup.machineSetup;f.setup.headModel={motion:{independentHead:true}};
 Object.assign(f.report,{independentHead:true,headNodes:[2],configuration:{placement:translated(0,3,0),tcp:[0,0,0],beam:[0,0,-1]}});
 const s=f.view.fiveMachine;s.machineId='';s.setupKey=JSON.stringify([f.setup.id,undefined,f.setup.headModel.motion]);s.headGroup=new THREE.Group();s.headGroup.add(new THREE.Mesh(new THREE.BoxGeometry(1,1,1),new THREE.MeshBasicMaterial()));f.view.viewport.scene.add(s.headGroup);
 const before=structuredClone(f.domain),workpiece=f.objects.get('pipe').matrix.toArray();assert.equal(camMachinePlayback(f.view),true);
 simulationField(f.context,f.view,'seek',.5,f.ops);const angle=Math.PI/4;
 for(const [id,[x,y,z]] of [['pipe',[5,4,2]],['fixture',[1,5,0]]])near(f.objects.get(id).position.toArray(),[x,y*Math.cos(angle)-z*Math.sin(angle),y*Math.sin(angle)+z*Math.cos(angle)]);
 assert.equal(s.headGroup.visible,true);assert.deepEqual(f.domain,before);stopMachine(f.view,true);near(f.objects.get('pipe').matrix.toArray(),workpiece);assert.equal(s.headGroup.visible,false);
});
test('missing and stale bound-machine plans block seek and play; old CAM commands cannot bypass the owner',async t=>{
 const f=fixture(t);f.view.fiveMachine.snapshotKey=JSON.stringify([f.view.fiveMachine.setupKey,f.view.scene.machines]);
 for(const report of [null,{...f.report,validity:'Stale'},{...f.report,placementPreview:true}]){
  f.view.fiveMachine.report=report;
  simulationField(f.context,f.view,'seek',.5,f.ops);assert.equal(f.view.fiveMachine.cursor,0);
  assert.equal(await simulationCommand(f.context,f.view,'play',f.ops),false);assert.notEqual(f.view.fiveMachine.playing,true);
  assert.equal(f.view.fiveSimulation,undefined);assert.equal(f.objects.get('pipe').position.length(),0);
 }
 f.view.fiveMachine.playing=true;pauseSimulation(f.view);assert.equal(f.view.fiveMachine.playing,false);
});
test('the mechanical tree and CAM dock show persisted support relationships and escape customer names',t=>{
 const f=fixture(t);f.domain.workpieces[0].name='<管件>';
 const tree=renderMachineTree(f.view,f.machine),chuck=tree.indexOf('data-machine-node="chuck"'),pipe=tree.indexOf('data-machine-mount="pipe"'),head=tree.indexOf('data-machine-node="head"');
 assert.ok(chuck<pipe&&pipe<head);assert.match(tree,/工件 · &lt;管件&gt;/);assert.match(tree,/夹具 · 夹爪/);
 assert.match(renderMachineDock(f.view),/&lt;管件&gt;、夹爪 → 旋转卡盘/);
 f.setup.machineSetup.supportId='head';assert.ok(renderMachineTree(f.view,f.machine).indexOf('data-machine-node="head"')<renderMachineTree(f.view,f.machine).indexOf('data-machine-mount="pipe"'));
});
test('bound CAM finishes the current job through the shared player and respects the next-setup boundary',async t=>{
 const f=fixture(t),request=globalThis.requestAnimationFrame,callbacks=new Map();let serial=0,ended=0;
 globalThis.requestAnimationFrame=callback=>{callbacks.set(++serial,callback);return serial;};
 t.after(()=>globalThis.requestAnimationFrame=request);
 f.view.fiveMachine.snapshotKey=JSON.stringify([f.view.fiveMachine.setupKey,f.view.scene.machines]);
 f.view.fiveJobPlayback={onEnd(){ended++;f.view.fiveJobPlayback.boundary=true;}};
 assert.equal(await simulationCommand(f.context,f.view,'play',f.ops),true);
 let time=1;while(callbacks.size){const [id,callback]=callbacks.entries().next().value;callbacks.delete(id);callback(time);time+=100;}
 assert.equal(ended,1);assert.equal(f.view.fiveMachine.cursor,1);assert.equal(f.view.fiveMachine.playing,false);
 assert.equal(await simulationCommand(f.context,f.view,'play',f.ops),false);assert.equal(ended,1);
});
test('returning from another inspected machine uses the job binding and retires its old playback',t=>{
 const f=fixture(t);f.view.fiveMachine.machineId='other-machine';
 renderMachineDock(f.view);assert.equal(f.view.fiveMachine.machineId,'machine');assert.equal(f.view.fiveMachine.report,null);assert.equal(f.view.fiveMachine.snapshotKey,null);
});
test('bound CAM uses machine coordinates and preserves a separate editing camera on return',t=>{
 const f=fixture(t),editingCamera={radius:15},machineCamera={radius:600};let camera=editingCamera;
 Object.assign(f.view.viewport,{getCameraState:()=>camera,setCameraState:value=>camera=value,fitViewToViewport:()=>true});
 f.view.fiveViewportFrame='setup';f.view.fiveFrameCameras={'cam-head':machineCamera};
 const snapshot={rows:[{data:{localToWorldMatrix:translated(0,9,0),setupPlacementTransform:translated(0,1,0)}}]};
 assert.equal(workpieceViewSnapshot(null,f.view,'toolpath',snapshot),snapshot);workpieceViewSnapshotApplied(null,f.view,'toolpath');assert.equal(camera,machineCamera);
 f.view.fiveAxisUI.tools.toolpath='keypoint';assert.equal(camMachinePlayback(f.view),false);assert.equal(machineViewportActive(f.view),false);
 const editing=workpieceViewSnapshot(null,f.view,'toolpath',snapshot);assert.deepEqual(editing.rows[0].data.localToWorldMatrix,translated(0,1,0));workpieceViewSnapshotApplied(null,f.view,'toolpath');assert.equal(camera,editingCamera);
 delete f.setup.machineSetup;f.view.fiveAxisUI.tools.toolpath='simulation';assert.equal(camMachinePlayback(f.view),false);
});
test('CAM keeps the terminal rotary mechanism, excluding the linear axes and the work-support branch',t=>{
 const f=fixture(t),m=f.machine;
 m.elements=[{entityId:'root',kind:'machine'},...['slide','mount','B','head','chuck'].map((id,i)=>({entityId:id,kind:'part',parentEntityId:({slide:'root',mount:'slide',B:'mount',head:'B',chuck:'root'})[id],sourceIndex:i}))];
 m.joints=[{entityId:'slide',type:'prismatic'},{entityId:'mount',type:'fixed'},{entityId:'B',type:'revolute'},{entityId:'head',type:'revolute'}];
 assert.deepEqual(camHeadElements(f.view).map(e=>e.entityId),['mount','B','head']);
 f.setup.headModel={assembly:{source:'MachineInstance',tool:1,nodes:[{},{}]}};
 assert.deepEqual(camHeadElements(f.view).map(e=>e.entityId),['B','head']);
 const html=renderCamMachinePlayback(f.view);assert.match(html,/切割头仿真/);assert.doesNotMatch(html,/规划机床运动|查看运动指令|导出运动指令|自动淡化遮挡/);
});
test('hiding the complete CAM head mechanism preserves workpiece display and restores visibility on exit',async t=>{
 const f=fixture(t);await simulationCommand(f.context,f.view,'head-toggle',f.ops);machineField(f.context,f.view,'seek',.25,f.ops);
 assert.equal(f.objects.get('head').visible,false);assert.equal(f.objects.get('pipe').visible,true);assert.equal(f.objects.get('fixture').visible,true);
 stopMachine(f.view,true);assert.equal(f.objects.get('head').visible,true);
});
