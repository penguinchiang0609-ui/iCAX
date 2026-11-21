import test from 'node:test';
import assert from 'node:assert/strict';
import * as THREE from '../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {collisionDiagnosis,collisionHeadBoxes,renderCollisionDiagnosis,paintCollisionDiagnostics,disposeCollisionDiagnostics,focusCollisionDiagnosis,collisionRepairActivity,renderCollisionRepair} from '../../apps/five-axis-cam/webpage/collisionDiagnostics.mjs';
import {collisionIssues} from '../../apps/five-axis-cam/webpage/collisionProgress.mjs';
import {repairCollision,jumpCollision,collisionLabel} from '../../apps/five-axis-cam/webpage/simulationCollision.mjs';

function fixture(){
 const boxes=[{index:'0',center:[0,0,4],halfExtents:[1,2,3],axes:[[0,1,0],[-1,0,0],[0,0,1]],source:'Head'},{index:'1',center:[0,0,10],halfExtents:[3,3,3],axes:[[1,0,0],[0,1,0],[0,0,1]],source:'AdditionalEnvelope'}];
 const witness={kind:'NearestPoints',frame:'Setup',headBoxIndex:'0',headPoint:[3,2,1],obstaclePoint:[3,2,.95],separationMm:.05,surfaceDistanceMm:.05,clearanceMm:.1,containment:false};
 const frames=[0,10].map(x=>({position:new THREE.Vector3(x,0,1),rotation:new THREE.Quaternion(),direction:new THREE.Vector3(0,0,1),target:new THREE.Vector3(x,0,0),intentId:'cut',order:3,role:'Cut'}));
 const report={validity:'Current',representation:'BoxCompoundMesh',headBoxCount:'2',headBoxes:boxes,status:'Collision',collisionCount:'1',uncheckedCount:'0',segments:[{index:'0',status:'Collision',fraction:.25,obstacleId:'plate',witness}]};
 const head=new THREE.Group(),headMesh=new THREE.Mesh(new THREE.BoxGeometry(2,2,20),new THREE.MeshStandardMaterial({color:0x7799aa}));head.add(headMesh);const obstacle=new THREE.Mesh(new THREE.BoxGeometry(100,10,1),new THREE.MeshStandardMaterial({color:0xaa8866})),scene=new THREE.Scene();scene.add(head,obstacle);
 const view={scene:{intentToolpaths:[{id:'cut',name:'外轮廓'}],domain:{expected:{epoch:'e',mutationSequence:'2'},activeSetupId:'setup',setups:[{id:'setup',simulationCheck:report}],fixtureUnits:[{id:'unit',name:'支撑组',partIds:['plate']}],fixtures:[{id:'plate',name:'支撑板 2'}],placements:[],workpieces:[]}},viewport:{scene,sceneObjects:new Map([['plate',obstacle]]),renderer:{domElement:{clientHeight:600,getBoundingClientRect:()=>({width:1000,height:600})}},perspectiveFov:45,getCameraState:()=>({radius:1000,target:{x:0,y:0,z:0},theta:1,phi:1}),setCameraState(value){this.cameraState=value;}}};
 const state={frames,cursor:.25,collisionReport:report,collisionKey:'report-1',group:head,collisionDetailXray:true},context={isCurrentProject:()=>true,mount:{querySelector:()=>null},projectProxy:{state:{projectPath:'C:/test/local-repair.i5cam'},async autosave(){context.saves++;}},saves:0,calls:[],sceneProxy:{async invoke(method,payload){context.calls.push({method,payload});return {status:'Completed',epoch:'e',taskId:'repair',result:{status:'Repaired',changed:true,changedIntentIds:['cut'],changedRapidCount:'1',maxDeviationDeg:15}};}}},ops={renderProject(){},appendProjectLog(){},async refreshSceneState(){context.refreshes=(context.refreshes||0)+1;}};
 return {view,state,context,ops,boxes,witness,report,headMesh,obstacle};
}

test('a joint-limit keypose shares the issue list and editor without a fabricated obstacle',async()=>{
 const f=fixture();f.report.keyposeChecks=[{index:'0',frameIndex:'0',fraction:0,status:'Unchecked',reason:'CAM_HEAD_LIMIT_NO_SOLUTION',location:{intentId:'cut',frameIndex:'0',pointId:'start',order:'3',label:'起点'}}];f.report.interpolationChecks=[{index:'0',firstFrameIndex:'0',lastFrameIndex:'1',status:'Skipped',reason:'CAM_HEAD_INTERVAL_SKIPPED_KEYPOSE'}];f.state.selectedCollisionCheck={kind:'keypose',key:'0'};
 const html=renderCollisionDiagnosis(f.view,f.state);assert.match(html,/限位内无解/);assert.match(html,/调整起点/);assert.doesNotMatch(html,/↔|发生干涉的检测盒|未命名检测对象/);
 assert.equal(collisionIssues(f.state).length,1);assert.match(collisionLabel(f.state),/1 处限位内无解/);assert.doesNotMatch(collisionLabel(f.state),/1 处碰撞/);
 assert.equal(await repairCollision(f.context,f.view,f.state,f.ops,()=>true),true);assert.equal(f.context.calls[0].payload.keyposeFrameIndex,'0');
});

test('issue navigation visits structured keyposes and intervals instead of thousands of sample records',()=>{
 const f=fixture();f.report.keyposeChecks=[{index:'0',frameIndex:'0',fraction:0,status:'Collision',location:{intentId:'cut',label:'起点'}}];f.report.interpolationChecks=[{index:'0',firstFrameIndex:'0',lastFrameIndex:'1',fraction:.5,status:'Collision',first:{intentId:'cut',label:'起点'},last:{intentId:'cut',label:'终点'}}];
 f.report.collisionCount='9000';f.state.selectedCollisionCheck={kind:'keypose',key:'0'};assert.match(collisionLabel(f.state),/2 处碰撞/);assert.doesNotMatch(collisionLabel(f.state),/9000/);
 jumpCollision(f.state,true);assert.deepEqual(f.state.selectedCollisionCheck,{kind:'interval',key:'0'});assert.equal(f.state.cursor,.5);jumpCollision(f.state,true);assert.deepEqual(f.state.selectedCollisionCheck,{kind:'keypose',key:'0'});
});

test('a neighboring issue is reported as pending rather than declaring the selected point unreachable',()=>{
 const {view,state}=fixture();state.repairResult={status:'Unresolved',changed:false,unresolved:[{index:'0',reason:'CAM_REPAIR_NEIGHBOR_KEYPOINT_NOT_CLEAR'}]};
 const html=renderCollisionRepair(view,state);assert.match(html,/相邻关键点也有碰撞或尚未通过检测/);assert.match(html,/先调整相邻问题/);assert.doesNotMatch(html,/不可达|保持邻点姿态固定/);
});

test('diagnostics render native compound boxes, exact witness points and the selected obstacle without changing source geometry',()=>{
 const f=fixture(),{view,state:s,headMesh,obstacle}=f,headMaterial=headMesh.material,obstacleMaterial=obstacle.material,sourceGeometry=obstacle.geometry,original=Array.from(sourceGeometry.attributes.position.array);let disposed=0;sourceGeometry.addEventListener('dispose',()=>disposed++);
 const d=collisionDiagnosis(view,s);assert.equal(d.boxIndex,0);assert.equal(d.obstacle.name,'支撑组 · 支撑板 2');assert.deepEqual(d.pose.position.toArray(),[2.5,0,1]);
 paintCollisionDiagnostics(view,s);const group=s.collisionDiagnosticOverlay;group.updateMatrixWorld(true);const box=group.getObjectByName('collision-head-box-0');assert.deepEqual(box.getWorldPosition(new THREE.Vector3()).toArray(),[2.5,0,5]);assert.equal(box.userData.headBoxIndex,0);assert.equal(box.children.filter(o=>o.isMesh).length,1);assert.equal(group.getObjectByName('collision-head-box-1').children.filter(o=>o.isMesh).length,0);
 assert.deepEqual(group.getObjectByName('collision-head-witness').position.toArray(),f.witness.headPoint);assert.deepEqual(group.getObjectByName('collision-obstacle-witness').position.toArray(),f.witness.obstaclePoint);assert.equal(headMesh.material.opacity,.45);assert.equal(obstacle.material.opacity,.22);
 assert.equal(focusCollisionDiagnosis(view,s),true);assert.ok(view.viewport.cameraState.radius<1000);assert.match(renderCollisionDiagnosis(view,s),/碰撞盒间隙不足/);assert.match(renderCollisionDiagnosis(view,s),/要求至少 0.1 mm/);assert.match(renderCollisionDiagnosis(view,s),/支撑组 · 支撑板 2/);
 disposeCollisionDiagnostics(s);assert.equal(headMesh.material,headMaterial);assert.equal(obstacle.material,obstacleMaterial);assert.equal(obstacle.geometry,sourceGeometry);assert.deepEqual(Array.from(sourceGeometry.attributes.position.array),original);assert.equal(disposed,0);
});

test('containment is explained as internal overlap without inventing coincident points or penetration depth',()=>{
 const f=fixture();Object.assign(f.witness,{kind:'Contained',containment:true,separationMm:0,surfaceDistanceMm:12,headPoint:[1,2,3],obstaclePoint:[1,2,15]});
 const html=renderCollisionDiagnosis(f.view,f.state);assert.match(html,/进入障碍物内部/);assert.match(html,/不代表穿透深度/);paintCollisionDiagnostics(f.view,f.state);assert.deepEqual(f.state.collisionDiagnosticOverlay.getObjectByName('collision-witness-link').geometry.attributes.position.array,new Float32Array([1,2,3,1,2,15]));disposeCollisionDiagnostics(f.state);
});

test('missing or invalid native box and witness data never produce an invented hit box or contact point',()=>{
 const f=fixture();delete f.report.headBoxes;delete f.report.segments[0].witness;assert.deepEqual(collisionHeadBoxes(f.report),[]);paintCollisionDiagnostics(f.view,f.state);assert.equal(f.state.collisionDiagnosticOverlay.getObjectByName('collision-head-witness'),undefined);assert.equal(f.state.collisionDiagnosticOverlay.getObjectByName('collision-detection-boxes').children.length,0);assert.match(renderCollisionDiagnosis(f.view,f.state),/未提供几何见证点/);disposeCollisionDiagnostics(f.state);
 f.report.headBoxes=f.boxes;f.boxes[0].axes=[[1,0,0],[1,0,0],[0,0,1]];assert.deepEqual(collisionHeadBoxes(f.report),[]);
});

test('checking and stale reports clear diagnosis presentation and restore temporary materials',()=>{
 const f=fixture();paintCollisionDiagnostics(f.view,f.state);f.state.checking=true;paintCollisionDiagnostics(f.view,f.state);assert.equal(f.state.collisionDiagnosticOverlay,null);assert.equal(f.headMesh.material.opacity,1);assert.equal(f.obstacle.material.opacity,1);assert.equal(collisionDiagnosis(f.view,f.state),null);f.state.checking=false;f.report.validity='Stale';assert.equal(renderCollisionDiagnosis(f.view,f.state),'');
});

test('result grouping separates adjacent different obstacles and different head boxes',()=>{
 const f=fixture();f.state.frames.push(...f.state.frames);f.report.segments=[{index:'0',status:'Collision',obstacleId:'a',witness:{headBoxIndex:0}},{index:'1',status:'Collision',obstacleId:'b',witness:{headBoxIndex:0}},{index:'2',status:'Collision',obstacleId:'b',witness:{headBoxIndex:1}}];assert.deepEqual(collisionIssues(f.state).map(g=>[g.index,g.count]),[[0,1],[1,1],[2,1]]);
});

test('repair UI is scoped to the current path and reports native local counts without fabricated percentages',()=>{
 const f=fixture();let html=renderCollisionRepair(f.view,f.state);assert.match(html,/自动修复本条刀路/);assert.match(html,/优先调整 IJK/);assert.match(html,/沿原 IJK 方向移动 XYZ/);f.state.cursor=1;html=renderCollisionRepair(f.view,f.state);assert.match(html,/collision-repair-path" disabled/);
 f.state.repairTask={repairProgress:{stage:'Verifying',completedIssueCount:'0',totalIssueCount:'1',candidateCount:'7',completedSegmentCount:'12',totalSegmentCount:'30'}};assert.match(collisionRepairActivity(f.state).count,/12 \/ 30/);assert.match(collisionRepairActivity(f.state).candidates,/实际已尝试 7/);assert.doesNotMatch(collisionRepairActivity(f.state).detail,/整条/);
 f.state.repairResult={status:'Repaired',changed:true,changedIntentIds:['cut'],geometryDeferred:true};f.report.collisionCount='8';html=renderCollisionRepair(f.view,f.state);assert.match(html,/关键点修正已保存，请复检本条/);assert.doesNotMatch(html,/全部通过|全局复检|完整复检/);
});

test('local repair passes exactly the chosen segment and saves one accepted native edit while other collisions remain',async()=>{
 const f=fixture();f.state.repairDeviationDeg=30;assert.equal(await repairCollision(f.context,f.view,f.state,f.ops,()=>true),true);assert.equal(f.context.calls.length,1);assert.equal(f.context.calls[0].method,'CamSimulation.AutoRepairCollision');assert.equal(f.context.calls[0].payload.segmentIndex,'0');assert.equal(f.context.calls[0].payload.maxDeviationDeg,30);assert.equal(f.context.saves,1);assert.equal(f.context.refreshes,1);assert.equal(f.state.repairResult.status,'Repaired');assert.equal(f.state.repairing,false);assert.equal(f.view.pending,false);assert.equal(f.report.status,'Collision','remaining collisions are not converted to Clear by the UI');
});

test('unresolved or cancelled local searches do not save, refresh or manufacture native history',async()=>{
 for(const status of ['Completed','Cancelled']){const f=fixture();f.context.sceneProxy.invoke=async()=>({status,epoch:'e',taskId:'repair',result:{status:'Unresolved',changed:false,unresolved:[{index:'0',intentId:'cut',reason:'NoSafeCandidate'}]}});assert.equal(await repairCollision(f.context,f.view,f.state,f.ops,()=>true),false);assert.equal(f.context.saves,0);assert.equal(f.context.refreshes,undefined);assert.equal(f.report.status,'Collision');}
});

test('cut local repair accepts a numeric decimal limit and explicitly scopes its authorization',async()=>{
 const f=fixture();assert.match(renderCollisionRepair(f.view,f.state),/inputmode="decimal"[^>]*value="15"/);
 f.state.repairDeviationDraft='7.5';assert.equal(await repairCollision(f.context,f.view,f.state,f.ops,()=>true),true);
 assert.equal(f.context.calls[0].payload.maxDeviationDeg,7.5);assert.equal(f.context.calls[0].payload.localPoseDeviationAuthorized,true);
 assert.deepEqual(f.view.scene.domain.expected,{epoch:'e',mutationSequence:'2'});
});

test('invalid local limits never start or save a native edit',async()=>{
 for(const value of ['', '-1', '180.1', 'NaN']){const f=fixture();f.state.repairDeviationDraft=value;
  assert.equal(await repairCollision(f.context,f.view,f.state,f.ops,()=>true),false);assert.equal(f.context.calls.length,0);assert.equal(f.context.saves,0);assert.notEqual(f.view.pending,true);assert.match(f.state.message,/180°/);
 }
});

test('repair requests retain both tolerances for rapid paths and permit position-only adjustment',async()=>{
 const f=fixture();f.state.frames[0].role='Rapid';f.state.repairDeviationDraft='0';f.state.repairTranslationDraft='2.5';
 const html=renderCollisionRepair(f.view,f.state);assert.match(html,/data-five-simulation-field="repair-deviation"/);assert.match(html,/data-five-simulation-field="repair-position"/);
 assert.equal(await repairCollision(f.context,f.view,f.state,f.ops,()=>true),true);
 assert.equal(f.context.calls[0].payload.maxDeviationDeg,0);assert.equal(f.context.calls[0].payload.maxTranslationMm,2.5);
});

test('a native commit acknowledged after a project switch saves only its captured project and does not refresh another page',async()=>{
 const f=fixture();assert.equal(await repairCollision(f.context,f.view,f.state,f.ops,()=>false),true);assert.equal(f.context.saves,1);assert.equal(f.context.refreshes,undefined);
});

test('an applied local repair displays only its current actual head boxes with no previous pose, ghost or acceptance action',()=>{
 const f=fixture(),s=f.state;s.repairResult={status:'Repaired',changed:true,changedIntentIds:['cut']};s.cursor=1;
 paintCollisionDiagnostics(f.view,s);const overlay=s.collisionDiagnosticOverlay;assert.equal(overlay.userData.kind,'AppliedLocalRepair');assert.deepEqual(overlay.position.toArray(),s.frames[1].position.toArray());assert.equal(overlay.children.length,2);assert.ok(overlay.children.every(o=>o.material.color.getHex()===0x71b9ae));assert.equal(overlay.getObjectByName('collision-head-witness'),undefined);assert.equal(f.headMesh.material.opacity,1);assert.equal(renderCollisionDiagnosis(f.view,s),'');assert.doesNotMatch(renderCollisionRepair(f.view,s),/对比|接受|修复前/);s.checking=true;paintCollisionDiagnostics(f.view,s);assert.equal(s.collisionDiagnosticOverlay,null);
});
