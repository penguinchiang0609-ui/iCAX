import test from 'node:test';
import assert from 'node:assert/strict';
import * as THREE from '../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {paintHeadAssembly,checkedHeadAssemblyPose} from '../../apps/five-axis-cam/webpage/cuttingHeadAssembly.mjs';
import {readFileSync} from 'node:fs';

test('native contact transforms place every head part in setup coordinates even when the nominal TCP frame is rotated',()=>{
 const parent=new THREE.Group(),head=new THREE.Group();parent.add(head);
 for(let i=0;i<2;i++){const part=new THREE.Group();part.matrixAutoUpdate=false;head.add(part);}
 const pose={position:new THREE.Vector3(20,-30,40),rotation:new THREE.Quaternion().setFromAxisAngle(new THREE.Vector3(0,1,0),.7)};
 parent.position.copy(pose.position);parent.quaternion.copy(pose.rotation);
 const matrices=[new THREE.Matrix4().makeRotationZ(.4).setPosition(12,34,56),new THREE.Matrix4().makeRotationX(-.8).setPosition(-15,20,60)];
 const checked={frame:'Setup',partTransforms:matrices.map(m=>m.clone().transpose().toArray())};
 assert.equal(paintHeadAssembly(head,pose,checked).source,'NativeCollision');parent.updateMatrixWorld(true);
 for(let i=0;i<2;i++)for(let k=0;k<16;k++)assert(Math.abs(head.children[i].matrixWorld.elements[k]-matrices[i].elements[k])<1e-10);
 assert.equal(head.userData.poseSource,'NativeCollision');assert.equal(paintHeadAssembly(head,pose,{frame:'Setup',partTransforms:[]}).valid,false);assert.equal(head.visible,false);
});

test('native keypose and interval witnesses agree numerically with displayed part transforms',()=>{
 const report=JSON.parse(readFileSync(new URL('../../../output/acceptance/selected-collision-20261102/full-head/yoke-contact.json',import.meta.url),'utf8')).report;
 let count=0;
 for(const row of [...report.keyposeChecks,...report.interpolationChecks]){
  if(!row.headPose||row.status==='Skipped')continue;
  const cursor=Number(row.index)+(Number(row.fraction)||0),pose=checkedHeadAssemblyPose(report,cursor);assert(pose,`missing checked head at ${cursor}`);
  for(let i=0;i<pose.partTransforms.length;i++)for(let k=0;k<16;k++)assert(Math.abs(pose.partTransforms[i][k]-row.headPose.partTransforms[i][k])<1e-7,`native/display mismatch at ${cursor}, part ${i}, matrix ${k}`);
  count++;
 }assert(count>0);
});

test('failed keyposes are never bridged, while a valid neighboring keypose can still be displayed',()=>{
 const identity=new THREE.Matrix4().toArray(),node=(parent,type,axis)=>({parent,type,axis,origin:identity});
 const nodes=[node(-1,'fixed',[0,0,1]),node(0,'prismatic',[1,0,0]),node(1,'prismatic',[0,1,0]),node(2,'prismatic',[0,0,1]),node(3,'revolute',[1,0,0]),node(0,'fixed',[0,0,1])];
 const report={headCoverage:{scope:'CompleteSelectedHeadAssembly',motion:'KeyposeJointInterpolationWithOriginalTCP',nodes,headNodes:[4],support:5,tool:4,tcp:[0,0,-100],keyposes:[{frameIndex:'0',reason:'CAM_HEAD_LIMIT_NO_SOLUTION'},{frameIndex:'2',joints:[0,0,0,0,.4,0]}]},frames:[{position:[0,0,0]},{position:[20,0,0]},{position:[20,10,30]}],segments:[]};
 assert.equal(checkedHeadAssemblyPose(report,0),null);assert.equal(checkedHeadAssemblyPose(report,1.5),null);assert(checkedHeadAssemblyPose(report,2));
});

test('between keyposes, rotary joints interpolate while the TCP stays on the original path without another solve',()=>{
 const identity=new THREE.Matrix4().transpose().toArray();
 const node=(parent,type,axis)=>({parent,type,axis,origin:identity});
 const nodes=[node(-1,'fixed',[0,0,1]),node(0,'prismatic',[1,0,0]),node(1,'prismatic',[0,1,0]),node(2,'prismatic',[0,0,1]),node(3,'revolute',[1,0,0]),node(0,'fixed',[0,0,1])];
 const report={headCoverage:{scope:'CompleteSelectedHeadAssembly',motion:'KeyposeJointInterpolationWithOriginalTCP',nodes,headNodes:[4],support:5,tool:4,tcp:[0,0,-100]},frames:[{position:[0,0,0]},{position:[20,10,30]}],segments:[{index:'0',fraction:0,headPose:{frame:'Setup',motion:[{fraction:0,joints:[0,0,0,0,0,0]},{fraction:1,joints:[0,0,0,0,Math.PI/2,0]}]}}]};
 for(const fraction of [.25,.5,.75,1]){
  const pose=checkedHeadAssemblyPose(report,fraction);assert(pose);const matrix=new THREE.Matrix4().set(...pose.partTransforms[0]),tcp=new THREE.Vector3(0,0,-100).applyMatrix4(matrix);
  assert(tcp.distanceTo(new THREE.Vector3(20,10,30).multiplyScalar(fraction))<1e-10);
  assert(new THREE.Vector3(0,0,1).transformDirection(matrix).distanceTo(new THREE.Vector3(0,-Math.sin(Math.PI/2*fraction),Math.cos(Math.PI/2*fraction)))<1e-10);
 }
});
