import test from 'node:test';
import assert from 'node:assert/strict';
import * as THREE from '../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {headAssemblyPose,rigidHeadDirectionSupported} from '../../apps/five-axis-cam/webpage/cuttingHeadAssembly.mjs';
import {machineForward} from '../../apps/five-axis-cam/webpage/machineKinematics.mjs';
import {chooseCuttingHeadFile} from '../../apps/five-axis-cam/webpage/cuttingHeadImport.mjs';

const origin=(x=0,y=0,z=0,angle=0)=>new THREE.Matrix4().makeRotationZ(angle).setPosition(x,y,z).transpose().toArray();
const node=(name,parent,axis,type='revolute',matrix=origin())=>({name,joint:name,parent,axis,type,origin:matrix,lower:-1.5,upper:1.5,position:0});
const assembly=(axes)=>({kind:'Articulated',source:'Imported',nodes:[node('mount',-1,[0,0,1],'fixed'),...axes.map((axis,i)=>node(`joint-${i}`,i,axis,'revolute',origin(8,-3,-45,.17*i)))],tool:axes.length,tcp:[3,2,-40],beam:[0,0,-1]});
const direction=(a,values)=>new THREE.Vector3(...a.beam).transformDirection(machineForward(a,values)[a.tool]).negate().toArray();

test('arbitrary declared joint axes and origins drive the head without AB/AC name inference',()=>{
 for(const axes of [[[1,0,0],[0,1,0]],[[0,0,1],[1,0,0]],[[.7,.7,0],[0,0,1]]]){
  const a=assembly(axes);let previous={};
  for(const values of [[0,.3,-.4],[0,.6,.2],[0,-.7,.4],[0,0,0]]){
   const target=direction(a,values),result=headAssemblyPose(a,target,previous);assert.equal(result.valid,true,JSON.stringify({axes,values,result}));
   const actual=new THREE.Vector3(...a.beam).transformDirection(result.world[a.tool]);assert.ok(actual.distanceTo(new THREE.Vector3(...target).negate())<1e-7);
   assert.ok(new THREE.Vector3(...a.tcp).applyMatrix4(result.world[a.tool]).length()<1e-8);
   assert.ok(result.values.slice(1).every(v=>v>=-1.5&&v<=1.5));previous=result.angles;
  }
 }
});
test('joint limits and a single rotary axis cannot be bypassed by rotating the whole head',()=>{
 const a=assembly([[1,0,0]]);assert.equal(headAssemblyPose(a,[0,-Math.sin(.5),Math.cos(.5)]).valid,true);
 assert.equal(headAssemblyPose(a,[.3,0,Math.sqrt(.91)]).valid,false);
 a.nodes[1].lower=-.1;a.nodes[1].upper=.1;assert.equal(headAssemblyPose(a,[0,-Math.sin(.5),Math.cos(.5)]).valid,false);
 a.nodes[1].lower=.5;a.nodes[1].upper=1;assert.equal(headAssemblyPose(a,[0,0,1]).valid,false);
 const supported=headAssemblyPose(a,[0,-Math.sin(.8),Math.cos(.8)]);assert.equal(supported.valid,true);assert.ok(supported.values[1]>=.5&&supported.values[1]<=1);
});
test('a rigid head supports fixed XYZ cutting and rejects a tilted tool axis',()=>{
 const a=assembly([]);a.kind='Rigid';assert.equal(headAssemblyPose(a,[0,0,1]).valid,true);assert.equal(headAssemblyPose(a,[.1,0,1]).valid,false);
 assert.equal(rigidHeadDirectionSupported({rotation:new THREE.Quaternion()}),true);
 assert.equal(rigidHeadDirectionSupported({rotation:new THREE.Quaternion().setFromAxisAngle(new THREE.Vector3(1,0,0),.2)}),false);
});
test('head selection offers joint descriptions and rigid geometry as distinct formats',async()=>{
 let dialog;const path=await chooseCuttingHeadFile({appProxy:{bridge:{async openFileDialog(options){dialog=options;return 'head.sdf';}}}});
 assert.equal(path,'head.sdf');assert.deepEqual(dialog.filters[1].extensions,['urdf','sdf']);assert.deepEqual(dialog.filters[2].extensions,['step','stp','iges','igs','stl','obj']);
});
