import test from 'node:test';
import assert from 'node:assert/strict';
import * as THREE from '../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {encodeNestingGeometry} from '../../apps/tube-designer/webpage/nestingPreview.mjs';
import {syncSimulation,disposeSimulation,renderSimulation,renderHeadSettings,simulationField,simulationCommand} from '../../apps/five-axis-cam/webpage/trajectorySimulation.mjs';

function fixture(tool='simulation'){
 const head={kind:'Imported',name:'Other selected display head',collisionSource:'MachineInstance',collisionMachineId:'machine',machineHeadName:'Bound cutting head',calibrated:true,tcpLocalPosition:[2,3,4],beamLocalDirection:[1,0,0],previewRef:{url:'resource://other-head',version:'1'},collisionModel:{boxCount:'1',boxes:[{center:[7,8,9],halfExtents:[1,2,3],axes:[[0,1,0],[-1,0,0],[0,0,1]],frame:'CalibratedTCP'}]}};
 const setup={id:'setup',headModel:head,rapidSettings:{followHeightMm:2,collisionThresholdMm:.1},generatedGeometry:{status:'Current',transitions:[],operations:[{intentId:'cut',placementId:'placement',order:1,parts:[{role:'Cut',points:[[10,20,30],[15,20,30]],directions:[[0,0,1],[0,0,1]]}]}]}};
 const domain={expected:{epoch:'epoch',mutationSequence:'1'},activeSetupId:'setup',setups:[setup],placements:[{id:'placement',setupId:'setup',transform:[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1]}],fixtures:[]};
 const view={fiveAxisArea:'toolpath',fiveCamPage:'cutting',fiveCamInspector:tool==='collision'?'collision':'properties',fiveAxisUI:{selectedIntentId:'cut',tools:{toolpath:tool==='collision'?'select':tool}},scene:{intentToolpaths:[{id:'cut'}],domain,machines:[{entityId:'machine',name:'Bound machine'}]},viewport:{id:'viewport',scene:new THREE.Scene(),sceneObjects:new Map()}};
 const calls=[],positions=[1,2,3, 4,2,3, 1,5,3];
 const context={mount:{querySelector:()=>null},isCurrentProject:()=>true,sceneProxy:{resources:{async get(url){calls.push(url);assert.equal(url,'resource://bound-head');return new Response(encodeNestingGeometry({positions,indices:[0,1,2]}));}},async invoke(){assert.fail('a bound head must be edited through machine settings');}}};
 return {head,view,context,calls,positions};
}
async function browser(run){const previous=globalThis.cancelAnimationFrame;globalThis.cancelAnimationFrame=()=>{};try{await run();}finally{globalThis.cancelAnimationFrame=previous;}}
const nearVector=(actual,expected)=>actual.forEach((value,index)=>assert.ok(Math.abs(value-expected[index])<1e-10,`${actual} differs from ${expected}`));

for(const tool of ['simulation','collision'])test(`${tool} displays the bound machine preview and its effective calibration before a selected head`,()=>browser(async()=>{
 const f=fixture(tool);f.head.machinePreviewRef={url:'resource://bound-head',version:'2'};const before=JSON.stringify(f.view.scene.domain);
 try{
  syncSimulation(f.context,f.view);
  for(let i=0;i<20&&!f.view.fiveSimulation.group?.getObjectByName('machine-cutting-head');++i)await new Promise(setImmediate);
  const group=f.view.fiveSimulation.group,mesh=group.getObjectByName('machine-cutting-head');assert.ok(mesh);assert.deepEqual(f.calls,['resource://bound-head']);
  assert.equal(group.getObjectByName('imported-cutting-head'),undefined);assert.equal(group.getObjectByName('machine-cutting-head-boxes'),undefined);
  assert.deepEqual(Array.from(mesh.geometry.attributes.position.array),f.positions,'the aggregated machine preview already owns attachment poses and scales');
  const calibration=new THREE.Quaternion().setFromUnitVectors(new THREE.Vector3(1,0,0),new THREE.Vector3(0,0,-1));
  nearVector(mesh.quaternion.toArray(),calibration.toArray());nearVector(mesh.position.toArray(),new THREE.Vector3(2,3,4).applyQuaternion(calibration).negate().toArray());
  assert.equal(JSON.stringify(f.view.scene.domain),before,'display loading must preserve engineering inputs');
 }finally{disposeSimulation(f.view);}
}));

test('a bound head without a preview displays calibrated boxes without loading the other selected model',()=>browser(async()=>{
 const f=fixture();f.head.collisionModel.boxes.push({enabled:false,center:[100,100,100],halfExtents:[50,50,50],axes:[[1,0,0],[0,1,0],[0,0,1]]});
 try{
  syncSimulation(f.context,f.view);await Promise.resolve();const group=f.view.fiveSimulation.group,boxes=group.getObjectByName('machine-cutting-head-boxes');
  assert.ok(boxes);assert.equal(boxes.children.length,1);assert.deepEqual(f.calls,[]);assert.equal(group.getObjectByName('imported-cutting-head'),undefined);
  const box=boxes.children[0];assert.equal(box.geometry.type,'BoxGeometry');assert.deepEqual([box.geometry.parameters.width,box.geometry.parameters.height,box.geometry.parameters.depth],[2,4,6]);
  assert.deepEqual(box.position.toArray(),[7,8,9],'box coordinates already use the calibrated TCP frame');
  nearVector(new THREE.Vector3(1,0,0).applyQuaternion(box.quaternion).toArray(),[0,1,0]);nearVector(new THREE.Vector3(0,1,0).applyQuaternion(box.quaternion).toArray(),[-1,0,0]);
  assert.deepEqual(group.position.toArray(),[10,20,32]);
 }finally{disposeSimulation(f.view);}
}));

test('bound head controls show the actual tool and reject catalog and legacy calibration edits',()=>browser(async()=>{
 const f=fixture();f.view.fiveSimulation={cursor:0,headOpen:true,showHead:true,frames:[]};const ops={renderProject(){}};
 const markup=renderHeadSettings(f.view);
 assert.match(markup,/<span>Bound cutting head<\/span>/);assert.match(markup,/红点：喷嘴端点 · 箭头：光束方向/);
 assert.match(markup,/<select[^>]+data-five-simulation-field="head-choice"[^>]+disabled/);
 assert.match(markup,/<option value="import"[^>]*>导入切割头…/);
 assert.doesNotMatch(markup,/data-five-command="five.simulation.head-apply"/);
 assert.doesNotMatch(markup,/data-five-simulation-field="tcp0"/);
 simulationField(f.context,f.view,'builtin-head','other',ops);simulationField(f.context,f.view,'tcp0','99',ops);
 for(const action of ['head-import','head-builtin','head-apply'])await simulationCommand(f.context,f.view,action,ops);
 assert.equal(f.view.fiveSimulation.builtinHeadIdDraft,undefined);assert.equal(f.view.fiveSimulation.draft,undefined);assert.equal(f.view.fiveSimulation.request,undefined);
}));
