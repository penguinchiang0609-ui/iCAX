import test from 'node:test';
import assert from 'node:assert/strict';
import * as THREE from '../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {encodeNestingGeometry} from '../../apps/tube-designer/webpage/nestingPreview.mjs';
import {syncSimulation,disposeSimulation} from '../../apps/five-axis-cam/webpage/trajectorySimulation.mjs';

function fixture(tool,imported){
 const positions=[0,0,0, 3,0,8, 0,3,8],preview={url:'resource://head-display/mesh',version:'1'};
 const headModel=imported?{kind:'Imported',calibrated:true,tcpLocalPosition:[1,2,3],beamLocalDirection:[0,0,-1],previewRef:preview}:{kind:'Builtin'};
 const setup={id:'setup',headModel,rapidSettings:{followHeightMm:2,collisionThresholdMm:.1},generatedGeometry:{status:'Current',transitions:[],operations:[{intentId:'cut',placementId:'placement',order:1,parts:[{role:'Cut',points:[[10,20,30],[15,20,30]],directions:[[0,0,1],[0,0,1]]}]}]}};
 const domain={expected:{epoch:'epoch',mutationSequence:'1'},activeSetupId:'setup',setups:[setup],placements:[{id:'placement',setupId:'setup',transform:[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1]}],fixtures:[]};
 const view={fiveAxisArea:'toolpath',fiveCamPage:'cutting',fiveCamInspector:tool==='collision'?'collision':'properties',fiveAxisUI:{selectedIntentId:'cut',tools:{toolpath:tool==='collision'?'select':tool}},scene:{intentToolpaths:[{id:'cut'}],domain},viewport:{id:'viewport',scene:new THREE.Scene(),sceneObjects:new Map()}};
 const context={mount:{querySelector:()=>null},isCurrentProject:()=>true,sceneProxy:{resources:{async get(url){assert.equal(url,preview.url);return new Response(encodeNestingGeometry({positions,indices:[0,1,2]}));}}}};
 return {view,context,domain,positions};
}

for(const tool of ['simulation','collision'])test(`${tool} shows built-in and imported head meshes without an attached nozzle sphere`,async()=>{
 const previous=globalThis.cancelAnimationFrame;globalThis.cancelAnimationFrame=()=>{};
 try{for(const imported of [false,true]){
  const {view,context,domain,positions}=fixture(tool,imported),before=JSON.stringify(domain);
  try{
   syncSimulation(context,view);
   for(let i=0;i<10&&imported&&!view.fiveSimulation.group?.getObjectByName('imported-cutting-head');++i)await new Promise(setImmediate);
   await Promise.resolve();
   const state=view.fiveSimulation,group=state.group,meshes=[];assert.ok(group?.visible);group.traverse(o=>{if(o.isMesh)meshes.push(o);});
   assert.equal(group.getObjectByName('nozzle-tcp'),undefined);assert.equal(meshes.some(o=>o.geometry.type==='SphereGeometry'),false);
   assert.equal(meshes.length,imported?1:3);assert.deepEqual(group.position.toArray(),[10,20,32]);assert.deepEqual(group.quaternion.toArray(),[0,0,0,1]);
   assert.deepEqual(Array.from(state.laser.geometry.attributes.position.array),[0,0,0,0,0,-2]);assert.equal(state.laser.visible,tool==='simulation');
   if(imported){const mesh=group.getObjectByName('imported-cutting-head');assert.deepEqual(Array.from(mesh.geometry.attributes.position.array),positions);assert.deepEqual(mesh.position.toArray(),[-1,-2,-3]);}
   assert.equal(JSON.stringify(domain),before,'display loading preserves head calibration, paths and all engineering inputs');
  }finally{disposeSimulation(view);}
 }}finally{globalThis.cancelAnimationFrame=previous;}
});

test('a rigid imported body cannot be rotated to pretend to execute a tilted tool axis',async()=>{
 const previous=globalThis.cancelAnimationFrame;globalThis.cancelAnimationFrame=()=>{};
 const {view,context,domain}=fixture('simulation',true);
 domain.setups[0].headModel.motionType='Rigid';
 domain.setups[0].generatedGeometry.operations[0].parts[0].directions=[[.2,0,.98],[.2,0,.98]];
 const before=JSON.stringify(domain);
 try{
  syncSimulation(context,view);for(let i=0;i<10&&!view.fiveSimulation.group?.getObjectByName('imported-cutting-head');i++)await new Promise(setImmediate);
  syncSimulation(context,view);
  assert.equal(view.fiveSimulation.group.visible,false);assert.match(view.fiveSimulation.message,/刚性刀头没有旋转轴/);assert.equal(JSON.stringify(domain),before);
 }finally{disposeSimulation(view);globalThis.cancelAnimationFrame=previous;}
});
