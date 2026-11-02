import test from 'node:test';
import assert from 'node:assert/strict';
import * as THREE from '../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {setMachineCollisionFeedback,syncMachineCollisionFeedback,renderMachineCollisionFeedback,machineCollisionFeedbackTarget,machineCollisionBoxMarked,clearMachineCollisionFeedback,machineCollisionFeedbackGeometrySignature} from '../../apps/five-axis-cam/webpage/machineCollisionFeedback.mjs';
import {workspaceAlertItems,workspaceAlertTarget} from '../../apps/five-axis-cam/webpage/alerts.mjs';

function fixture(){
 const x=new THREE.Object3D(),z=new THREE.Object3D();x.position.set(300,20,50);x.rotation.z=Math.PI/2;z.position.set(600,-40,80);x.updateMatrixWorld(true);z.updateMatrixWorld(true);
 const a={entityId:'x',id:'disabled',name:'disabled box',enabled:false,pose:[0,0,0,0,0,0],geometry:{type:'box',size:[.1,.1,.1]}},b={entityId:'x',name:'X source box',pose:[.02,.03,.04,0,0,.2],geometry:{type:'box',size:[.2,.3,.4]}},c={entityId:'z',id:'z-box',name:'Z box',pose:[-.01,.02,0,0,0,0],geometry:{type:'box',size:[.1,.2,.3]}},other={...c,id:'z-other',name:'Z other',pose:[0,0,.4,0,0,0]};
 const machine={entityId:'machine',enabled:true,elements:[{entityId:'x',name:'X 横滑座'},{entityId:'z',name:'Z 滑枕'}],joints:[{entityId:'x',type:'prismatic',position:3},{entityId:'z',type:'prismatic',position:8}],collisions:[a,b,c,other]};
 const view={fiveAxisArea:'simulation',fiveMachine:{machineId:'machine'},fiveMachineSelectedPartId:'a',scene:{machines:[machine],domain:{expected:{epoch:'one'}}},viewport:{sceneObjects:new Map([['x',x],['z',z]])},fiveMachineColliders:{partId:'a',drafts:new Map()}};
 const first={ownerElementId:'x',ownerElementName:'X 横滑座',boxId:'',boxName:'X source box',boxIndex:1,representation:'Box',editable:true},second={ownerElementId:'z',ownerElementName:'Z 滑枕',boxId:'z-box',boxName:'Z box',boxIndex:0,representation:'Box',editable:true};
 const result={status:'Blocked',collisionStatus:'Blocked',reason:'碰撞盒间隙不足',collisionDiagnosticsComplete:true,collisionPairs:[{first,second}]};
 return {view,machine,x,z,result,first,second};
}

test('only exact reported boxes are red across owners, independently of selection or ordinary box visibility',()=>{
 const f=fixture(),before=[...f.x.matrixWorld.elements];assert.equal(setMachineCollisionFeedback(f.view,'machine',f.result),true);assert.equal(setMachineCollisionFeedback(f.view,'machine',f.result),false);
 syncMachineCollisionFeedback(f.view);const state=f.view.fiveMachineCollisionFeedback;assert.equal(state.overlays.size,2);assert.equal(f.x.children.length,1);assert.equal(f.z.children.length,1);
 const x=[...state.overlays.values()].find(o=>o.parent===f.x).object,z=[...state.overlays.values()].find(o=>o.parent===f.z).object;
 assert.deepEqual(x.position.toArray(),[20,30,40]);assert.deepEqual(x.scale.toArray(),[200,300,400]);assert.deepEqual(x.getWorldPosition(new THREE.Vector3()).toArray().map(v=>Math.round(v)),[270,40,90]);
 assert.equal(x.children[0].children[2].material.uniforms.color.value.getHex(),0xff4444);assert.equal(x.children[0].children[0].material.color.getHex(),0xff4444);assert.equal(x.children[0].children[3].visible,false);assert.ok(x.children[0].children[2].renderOrder>904);assert.equal(x.userData.boxIndex,1);assert.equal(z.userData.boxId,'z-box');assert.deepEqual(f.x.matrixWorld.elements,before);
 f.view.fiveMachineSelectedPartId='z';f.view.fiveMachineColliders.show=false;f.view.fiveMachineColliders.mode='bounds';syncMachineCollisionFeedback(f.view);assert.equal(state.validity,'Current');assert.equal(state.overlays.size,2);assert.equal(x.visible,true);assert.equal(z.visible,true);
 assert.equal(machineCollisionBoxMarked(f.view,'x',0),false);assert.equal(machineCollisionBoxMarked(f.view,'x',1),true);assert.equal(machineCollisionBoxMarked(f.view,'z','z-box'),true);assert.equal(machineCollisionBoxMarked(f.view,'z','z-other'),false);
 f.x.position.x+=70;f.x.updateMatrixWorld(true);assert.equal(x.getWorldPosition(new THREE.Vector3()).x,340);clearMachineCollisionFeedback(f.view);
});

test('structured references drive direct edit and ribbon alerts without reading translated reason text',()=>{
 const f=fixture();f.result.reason='任意语言的说明，没有部件名称';setMachineCollisionFeedback(f.view,'machine',f.result);
 const first=machineCollisionFeedbackTarget(f.view,0),second=machineCollisionFeedbackTarget(f.view,1);assert.equal(first.ownerElementId,'x');assert.equal(first.boxIndex,1);assert.equal(first.draftIndex,1);assert.equal(first.boxName,'X source box');assert.equal(second.boxId,'z-box');
 const html=renderMachineCollisionFeedback(f.view);assert.match(html,/相关碰撞盒/);assert.match(html,/five.machine.collision-edit:0/);assert.match(html,/X 横滑座 · X source box/);assert.doesNotMatch(html,/disabled box|Z other|five.machine.collision-recheck/);
 const alerts=workspaceAlertItems(f.view);assert.equal(alerts.length,2);assert.equal(workspaceAlertTarget(f.view,alerts[0].id).commandId,'five.machine.collision-edit:0');
 clearMachineCollisionFeedback(f.view);
});

test('unnamed source boxes use the native raw-index fallback name while preserving name validation',()=>{
 const f=fixture();delete f.machine.collisions[1].name;f.first.boxName='碰撞盒 2';setMachineCollisionFeedback(f.view,'machine',f.result);syncMachineCollisionFeedback(f.view);
 assert.equal(machineCollisionFeedbackTarget(f.view,0).boxName,'碰撞盒 2');assert.equal(machineCollisionFeedbackTarget(f.view,0).boxIndex,1);assert.equal(machineCollisionBoxMarked(f.view,'x',1),true);assert.equal(f.view.fiveMachineCollisionFeedback.overlays.size,2);clearMachineCollisionFeedback(f.view);
 f.first.boxName='碰撞盒 1';setMachineCollisionFeedback(f.view,'machine',f.result);assert.equal(machineCollisionFeedbackTarget(f.view,0),null);assert.equal(machineCollisionBoxMarked(f.view,'x',1),false);clearMachineCollisionFeedback(f.view);
});

test('pose and geometry edits retire the certificate and dispose red GPU resources while selection alone does not',()=>{
 for(const mutate of [f=>f.machine.joints[0].position++,f=>f.machine.collisions[1].geometry.size[0]=.25,f=>f.view.fiveMachineColliders.drafts.set('x',{dirty:true,boxes:[{id:'draft',size:[1,2,3]}]}),f=>f.view.scene.domain.expected.epoch='two']){
  const f=fixture();setMachineCollisionFeedback(f.view,'machine',f.result);syncMachineCollisionFeedback(f.view);const object=[...f.view.fiveMachineCollisionFeedback.overlays.values()][0].object;let disposed=false;object.geometry.addEventListener('dispose',()=>disposed=true);mutate(f);assert.equal(syncMachineCollisionFeedback(f.view),true);assert.equal(f.view.fiveMachineCollisionFeedback.validity,'Stale');assert.equal(f.x.children.length+f.z.children.length,0);assert.equal(disposed,true);assert.equal(machineCollisionFeedbackTarget(f.view,0),null);assert.equal(machineCollisionBoxMarked(f.view,'x',1),false);assert.match(renderMachineCollisionFeedback(f.view),/碰撞定位已过期/);clearMachineCollisionFeedback(f.view);
 }
});

test('temporary empty navigation scene retains the diagnosis until the same scene becomes ready again',()=>{
 const f=fixture();setMachineCollisionFeedback(f.view,'machine',f.result);syncMachineCollisionFeedback(f.view);const scene=f.view.scene;f.view.scene=null;
 assert.equal(syncMachineCollisionFeedback(f.view),false);assert.equal(f.view.fiveMachineCollisionFeedback.validity,'Current');assert.equal(machineCollisionFeedbackTarget(f.view,0),null);assert.equal(setMachineCollisionFeedback(f.view,'machine',f.result),false);assert.equal(f.view.fiveMachineCollisionFeedback.validity,'Current');assert.doesNotMatch(renderMachineCollisionFeedback(f.view),/碰撞定位已过期/);
 f.view.scene=scene;syncMachineCollisionFeedback(f.view);assert.equal(f.view.fiveMachineCollisionFeedback.validity,'Current');assert.equal(f.view.fiveMachineCollisionFeedback.overlays.size,2);assert.equal(machineCollisionFeedbackTarget(f.view,0).ownerElementId,'x');
 f.view.scene={machines:[]};syncMachineCollisionFeedback(f.view);assert.equal(f.view.fiveMachineCollisionFeedback.validity,'Stale');clearMachineCollisionFeedback(f.view);
});

test('clear unchecked invalidated and failed outcomes cannot leave old collision marks or edit links',()=>{
 for(const result of [{status:'Ready',collisionStatus:'Clear'},{status:'Unchecked',collisionStatus:'Unchecked'},{status:'Invalidated',collisionStatus:'Blocked'},{status:'Error',collisionStatus:'Blocked'}]){
  const f=fixture();setMachineCollisionFeedback(f.view,'machine',f.result);syncMachineCollisionFeedback(f.view);assert.equal(setMachineCollisionFeedback(f.view,'machineId',{status:'Idle'}),false);assert.equal(f.view.fiveMachineCollisionFeedback.overlays.size,2);assert.equal(setMachineCollisionFeedback(f.view,'machine',result),true);assert.equal(renderMachineCollisionFeedback(f.view),'');assert.equal(f.x.children.length+f.z.children.length,0);assert.deepEqual(workspaceAlertItems(f.view),[]);
 }
});

test('late diagnostics cannot apply indices after box edits; own-mesh endpoints never masquerade as editable boxes',()=>{
 const f=fixture(),captured=machineCollisionFeedbackGeometrySignature(f.view,'machine');f.machine.collisions[1].geometry.size[0]=.27;setMachineCollisionFeedback(f.view,'machine',f.result,{geometrySignature:captured});syncMachineCollisionFeedback(f.view);assert.equal(f.view.fiveMachineCollisionFeedback.validity,'Stale');assert.equal(f.view.fiveMachineCollisionFeedback.overlays.size,0);assert.equal(machineCollisionFeedbackTarget(f.view,0),null);clearMachineCollisionFeedback(f.view);
 f.result.collisionPairs[0].second={ownerElementId:'z',ownerElementName:'真实刀头',boxIndex:-1,boxName:'',representation:'OwnMesh',editable:false};f.result.collisionDiagnosticsComplete=false;setMachineCollisionFeedback(f.view,'machine',f.result);syncMachineCollisionFeedback(f.view);assert.equal(f.view.fiveMachineCollisionFeedback.overlays.size,1);assert.equal(machineCollisionFeedbackTarget(f.view,1),null);assert.match(renderMachineCollisionFeedback(f.view),/真实模型/);assert.match(renderMachineCollisionFeedback(f.view),/尚未完成定位/);assert.match(renderMachineCollisionFeedback(f.view),/data-five-command="five.machine.collision-recheck"[^>]*>重新检查<\/button>/);clearMachineCollisionFeedback(f.view);
});

test('stale diagnostics offer a read-only recheck action in the panel and global alert',()=>{
 const f=fixture();setMachineCollisionFeedback(f.view,'machine',f.result);f.machine.collisions[1].geometry.size[0]=.25;const html=renderMachineCollisionFeedback(f.view);
 assert.match(html,/data-five-command="five.machine.collision-recheck"[^>]*>重新检查<\/button>/);const alerts=workspaceAlertItems(f.view);assert.equal(alerts.length,1);assert.equal(alerts[0].severity,'warning');assert.equal(workspaceAlertTarget(f.view,alerts[0].id).commandId,'five.machine.collision-recheck');
 f.view.pending=true;assert.match(renderMachineCollisionFeedback(f.view),/data-five-command="five.machine.collision-recheck"[^>]*disabled/);clearMachineCollisionFeedback(f.view);
});

test('bound workpiece and fixture changes retire own-mesh collision diagnoses while selection and other setups do not',()=>{
 const identity=[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1];
 const domain=()=>({expected:{epoch:'one'},activeSetupId:'bound',setups:[{id:'bound',machineSetup:{machineId:'machine',placement:[...identity]}},{id:'other',machineSetup:{machineId:'other-machine'}}],placements:[{id:'work-placement',setupId:'bound',workpieceId:'work',transform:[...identity]},{id:'other-placement',setupId:'other',workpieceId:'other-work',transform:[...identity]}],workpieces:[{id:'work',geometryRef:{url:'part',version:1},enabled:true},{id:'other-work',geometryRef:{url:'unrelated',version:1}}],fixtures:[{id:'fixture',setupId:'bound',unitId:'unit',enabled:true,collisionEnabled:true,transform:[...identity],geometryRef:{url:'fixture',version:1}}],fixtureUnits:[{id:'unit',setupId:'bound',enabled:true,partIds:['fixture']}]});
 for(const mutate of [d=>d.placements[0].transform[3]=20,d=>d.workpieces[0].geometryRef.version++,d=>d.workpieces[0].enabled=false,d=>d.fixtures[0].transform[7]=20,d=>d.fixtures[0].geometryRef.version++,d=>d.fixtures[0].collisionEnabled=false,d=>d.fixtureUnits[0].enabled=false,d=>d.setups[0].machineSetup.placement[3]=10]){
  const f=fixture();f.view.scene.domain=domain();f.result.collisionPairs[0].second={ownerElementId:'fixture',ownerElementName:'fixture',boxIndex:-1,representation:'OwnMesh',editable:false};setMachineCollisionFeedback(f.view,'machine',f.result);syncMachineCollisionFeedback(f.view);assert.equal(f.view.fiveMachineCollisionFeedback.overlays.size,1);mutate(f.view.scene.domain);syncMachineCollisionFeedback(f.view);assert.equal(f.view.fiveMachineCollisionFeedback.validity,'Stale');assert.equal(f.view.fiveMachineCollisionFeedback.overlays.size,0);clearMachineCollisionFeedback(f.view);
 }
 const f=fixture();f.view.scene.domain=domain();setMachineCollisionFeedback(f.view,'machine',f.result);syncMachineCollisionFeedback(f.view);f.view.scene.domain.activeSetupId='other';f.view.scene.domain.activePlacementId='other-placement';f.view.scene.domain.placements[1].transform[3]=100;f.view.scene.domain.workpieces[1].geometryRef.version++;syncMachineCollisionFeedback(f.view);assert.equal(f.view.fiveMachineCollisionFeedback.validity,'Current');assert.equal(f.view.fiveMachineCollisionFeedback.overlays.size,2);clearMachineCollisionFeedback(f.view);
});
