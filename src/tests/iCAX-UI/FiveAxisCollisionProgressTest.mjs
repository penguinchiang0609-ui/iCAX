import test from 'node:test';
import assert from 'node:assert/strict';
import * as THREE from '../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {acceptCollisionProgress,collisionActivity,collisionIssues,disposeCollisionProgress,paintCollisionProgress,renderCollisionActivity} from '../../apps/five-axis-cam/webpage/collisionProgress.mjs';

const frames=()=>Array.from({length:11},(_,i)=>({position:new THREE.Vector3(i,2*i,3),rotation:new THREE.Quaternion(),order:i<7?1:'1 → 2',role:i<7?'Cut':'Rapid'}));
const task=(p,status='Running')=>({status,progress:'30',collisionProgress:p});

test('percentage-only progress never invents an inspected pose or a segment count',()=>{
  const s={frames:frames(),cursor:8,checkStage:'checking'};acceptCollisionProgress(s,{status:'Running',progress:'62'});
  assert.equal(s.cursor,8);assert.equal(s.checkExactProgress,null);assert.equal(s.checkProgress,62);
  assert.doesNotMatch(collisionActivity(s).detail,/第.*段/);assert.doesNotMatch(renderCollisionActivity(s),/已检查 6/);
});

test('keypoint precheck reports static controls before continuous intervals without inventing a moving segment',()=>{
  const s={frames:frames(),cursor:8,checkStage:'checking'};
  acceptCollisionProgress(s,task({stage:'Keypoints',completedKeypointCount:'2',totalKeypointCount:'5',completedSegmentCount:'0',totalSegmentCount:'10',fullSegmentCount:'10'}));
  assert.equal(s.cursor,8);assert.equal(s.checkExactProgress.current,null);assert.match(collisionActivity(s).stage,/关键点/);assert.match(collisionActivity(s).count,/2 \/ 5 个关键点/);
  acceptCollisionProgress(s,task({stage:'Intervals',completedKeypointCount:'5',totalKeypointCount:'5',completedSegmentCount:'1',totalSegmentCount:'10',fullSegmentCount:'10',currentSegmentIndex:'1'}));
  assert.equal(s.checkExactProgress.keypoints,undefined);assert.equal(s.cursor,1);assert.match(collisionActivity(s).count,/1 \/ 10 个关键点区间/);
});

test('retry progress uses native original indices and truthful query-local counts',()=>{
  const s={frames:frames(),cursor:0,checkStage:'checking'};acceptCollisionProgress(s,task({completedSegmentCount:'1',totalSegmentCount:'3',fullSegmentCount:'10',currentSegmentIndex:'7',lastCompletedSegmentIndex:'2'}));
  assert.equal(s.cursor,7);assert.deepEqual(s.checkExactProgress,{completed:1,total:3,full:10,current:7,last:2,location:undefined});
  assert.match(collisionActivity(s).detail,/刀路 1 → 2 · 空移/);assert.match(collisionActivity(s).count,/1 \/ 3 个关键点区间/);
});

test('parallel completions count work independently of trajectory order and display only an active segment',()=>{
  const s={frames:frames(),cursor:0,checkStage:'checking'};
  acceptCollisionProgress(s,task({stage:'Intervals',completedSegmentCount:'4',totalSegmentCount:'10',fullSegmentCount:'10',currentSegmentIndex:'1',lastCompletedSegmentIndex:'8',workerCount:'4',activeTaskCount:'3'}));
  assert.equal(s.cursor,1);assert.equal(s.checkExactProgress.completed,4);assert.equal(s.checkExactProgress.last,8);
  assert.match(collisionActivity(s).count,/4 \/ 10 个关键点区间.*同时检测 3 处/);assert.match(collisionActivity(s).pose,/并行检测的一段/);
  acceptCollisionProgress(s,task({stage:'Keypoints',completedSegmentCount:'0',totalSegmentCount:'10',fullSegmentCount:'10',completedKeypointCount:'4',totalKeypointCount:'5',workerCount:'4',activeTaskCount:'1'}));
  assert.equal(s.checkExactProgress.current,null);assert.match(collisionActivity(s).count,/4 \/ 5 个关键点.*同时检测 1 处/);
});

test('mismatched full trajectory or out-of-range native index cannot move the tool',()=>{
  const s={frames:frames(),cursor:4};acceptCollisionProgress(s,task({completedSegmentCount:0,totalSegmentCount:8,fullSegmentCount:99,currentSegmentIndex:0}));
  assert.equal(s.checkExactProgress,null);assert.equal(s.cursor,4);
  acceptCollisionProgress(s,task({completedSegmentCount:0,totalSegmentCount:10,fullSegmentCount:10,currentSegmentIndex:99}));assert.equal(s.checkExactProgress.current,null);assert.equal(s.cursor,4);
});

test('completed task uses the last native index without animating another segment',()=>{
  const s={frames:frames(),cursor:0};acceptCollisionProgress(s,task({completedSegmentCount:3,totalSegmentCount:3,fullSegmentCount:10,currentSegmentIndex:9,lastCompletedSegmentIndex:8},'Completed'));
  assert.equal(s.checkExactProgress.current,null);assert.equal(s.cursor,8);
});

test('active sweep geometry is the reported segment endpoints and disappears when no current native segment exists',()=>{
  const s={frames:frames(),checking:true,checkExactProgress:{current:3}},view={viewport:{scene:new THREE.Scene()}};
  paintCollisionProgress(view,s);assert.equal(s.checkOverlay.index,3);assert.equal(s.checkOverlay.group.userData.poseSource,'NativeCollisionTaskSegmentStart');
  const line=s.checkOverlay.group.getObjectByName('collision-current-sweep');assert.deepEqual(Array.from(line.geometry.attributes.position.array),[3,6,3,4,8,3]);
  s.checkExactProgress.current=null;paintCollisionProgress(view,s);assert.equal(s.checkOverlay.group.visible,false);disposeCollisionProgress(s);assert.equal(view.viewport.scene.children.length,0);
});

test('a missing connector is not drawn as an inspected sweep',()=>{
  const s={frames:frames(),checking:true,checkExactProgress:{current:3}},view={viewport:{scene:new THREE.Scene()}};s.frames[4].breakBefore=true;
  paintCollisionProgress(view,s);assert.equal(s.checkOverlay.group.getObjectByName('collision-current-sweep'),undefined);assert.ok(s.checkOverlay.group.getObjectByName('collision-current-start'));disposeCollisionProgress(s);
});

test('issue groups retain distinct collision and unchecked results without converting either to clear',()=>{
  const s={frames:frames(),collisionReport:{segments:[{index:'1',status:'Collision'},{index:'2',status:'Collision'},{index:'3',status:'Unchecked'},{index:'4',status:'Clear'},{index:'7',status:'Unchecked'}]}};
  assert.deepEqual(collisionIssues(s).map(x=>[x.index,x.last,x.count,x.status]),[[1,2,2,'Collision'],[3,3,1,'Unchecked'],[7,7,1,'Unchecked']]);
});
