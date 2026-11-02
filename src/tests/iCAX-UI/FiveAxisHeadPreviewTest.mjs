import test from 'node:test';
import assert from 'node:assert/strict';
import {encodeNestingGeometry} from '../../apps/tube-designer/webpage/nestingPreview.mjs';
import {loadCuttingHeadPreview,poseCuttingHeadPreview,previewJointAxisLabel} from '../../apps/five-axis-cam/webpage/cuttingHeadPreview.mjs';

const resource=()=>new Response(encodeNestingGeometry({positions:[0,0,0,1,0,0,0,1,0],indices:[0,1,2]}));
const release=result=>result?.group.traverse(o=>{o.geometry?.dispose();o.material?.dispose();});
const near=(actual,expected)=>actual.forEach((v,i)=>assert.ok(Math.abs(v-expected[i])<1e-10));

test('joint preview labels use the declared axis, including arbitrary imported joint names',()=>{
 assert.equal(previewJointAxisLabel({kind:'Builtin'},{joint:'A',axis:[1,0,0]}),'X');
 assert.equal(previewJointAxisLabel({kind:'Builtin'},{joint:'B',axis:[0,1,0]}),'Y');
 assert.equal(previewJointAxisLabel({kind:'Builtin'},{joint:'C',axis:[0,0,1]}),'Z');
 assert.equal(previewJointAxisLabel({kind:'Imported'},{joint:'B',axis:[1,0,0]}),'局部 X');
 assert.equal(previewJointAxisLabel({kind:'Imported'},{joint:'swivel',axis:[0,-1,0]}),'局部 −Y');
});

test('AB preview has a fixed stator, B about Y carrying A about X, and never changes saved angles',async()=>{
 const id=[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1],a=[...id];a[11]=-75;
 const head={assembly:{kind:'AB',tool:2,tcp:[0,0,-220],beam:[0,0,-1],nodes:[
  {name:'mount',type:'fixed',parent:-1,origin:id,axis:[0,0,1],previewRef:{url:'resource://mount'}},
  {name:'B',joint:'B',type:'revolute',parent:0,origin:id,axis:[0,1,0],lower:-Math.PI/2,upper:Math.PI/2,previewRef:{url:'resource://B'}},
  {name:'A',joint:'A',type:'revolute',parent:1,origin:a,axis:[1,0,0],lower:-Math.PI/2,upper:Math.PI/2,previewRef:{url:'resource://A'}}
 ]}},before=structuredClone(head),result=await loadCuttingHeadPreview({get:async()=>resource()},head);
 try{
  const zero=poseCuttingHeadPreview(result.group,[0,0,0]),x=poseCuttingHeadPreview(result.group,[0,0,Math.PI/6]);
  near(x.world[0].elements,zero.world[0].elements);near(x.world[1].elements,zero.world[1].elements);near(x.beam.toArray(),[0,.5,-Math.sqrt(3)/2]);
  const y=poseCuttingHeadPreview(result.group,[0,Math.PI/6,0]);near(y.world[0].elements,zero.world[0].elements);near(y.beam.toArray(),[-.5,0,-Math.sqrt(3)/2]);
  assert.notDeepEqual(y.world[1].elements,zero.world[1].elements);assert.notDeepEqual(y.world[2].elements,zero.world[2].elements);
  assert.deepEqual(head,before);assert.throws(()=>poseCuttingHeadPreview(result.group,[0,Math.PI,0]),/超出范围/);
 }finally{release(result);}
});

test('the preview uses saved head joint positions and part origins without solving or mutating a machining pose',async()=>{
 const head={assembly:{tool:1,tcp:[1,0,0],beam:[0,0,-1],nodes:[
  {name:'fork',type:'fixed',parent:-1,axis:[0,0,1],position:0,origin:[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1],previewRef:{url:'resource://fork'}},
  {name:'nozzle',type:'revolute',parent:0,axis:[0,0,1],position:Math.PI/2,origin:[1,0,0,10,0,1,0,0,0,0,1,0,0,0,0,1],previewRef:{url:'resource://nozzle'}}
 ]}},before=structuredClone(head),calls=[];
 const result=await loadCuttingHeadPreview({async get(url){calls.push(url);return resource();}},head);
 try{assert.deepEqual(calls,['resource://fork','resource://nozzle']);assert.equal(result.group.children.length,2);near(result.tcp.toArray(),[10,1,0]);near(result.beam.toArray(),[0,0,-1]);assert.deepEqual(head,before);assert.equal(result.group.children[1].matrixAutoUpdate,false);}finally{release(result);}
});

test('a machine-bound preview loads its effective machine model instead of another selected display head',async()=>{
 const head={collisionSource:'MachineInstance',machinePreviewRef:{url:'resource://machine-head'},previewRef:{url:'resource://wrong-head'},tcpLocalPosition:[1,2,3],beamLocalDirection:[0,0,-1],calibrated:true};
 const calls=[],result=await loadCuttingHeadPreview({async get(url){calls.push(url);return resource();}},head);
 try{assert.deepEqual(calls,['resource://machine-head']);assert.equal(result.group.name,'machine-cutting-head');assert.deepEqual(result.tcp.toArray(),[1,2,3]);}finally{release(result);}
});

test('an imported model remains visible before calibration, with no invented TCP calibration',async()=>{
 const head={kind:'Imported',previewRef:{url:'resource://import'},calibrated:false},before=JSON.stringify(head);
 const result=await loadCuttingHeadPreview({get:async()=>resource()},head);
 try{assert.ok(result.group.isMesh);assert.equal(result.calibrated,false);assert.equal(JSON.stringify(head),before);}finally{release(result);}
});

test('unmarked imported assemblies retain empty fixed coordinate nodes and remain uncalibrated',async()=>{
 const id=[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1],head={kind:'Imported',calibrated:false,assembly:{tool:1,tcp:[0,0,0],beam:[0,0,-1],nodes:[
  {name:'frame',type:'fixed',parent:-1,axis:[0,0,1],origin:id},
  {name:'A',joint:'A',type:'revolute',parent:0,axis:[1,0,0],origin:id,lower:-1,upper:1,previewRef:{url:'resource://body'}}
 ]}};
 const result=await loadCuttingHeadPreview({get:async()=>resource()},head);
 try{assert.equal(result.calibrated,false);assert.equal(result.group.children[0].isGroup,true);assert.equal(result.group.children.length,2);poseCuttingHeadPreview(result.group,[0,.4]);assert.equal(head.calibrated,false);}finally{release(result);}
});

test('late resource replies do not allocate a model after the preview is invalidated',async()=>{
 let current=true,reply;const pending=loadCuttingHeadPreview({get:()=>new Promise(r=>reply=r)},{previewRef:{url:'resource://late'}},()=>current);current=false;reply(resource());assert.equal(await pending,null);
});

test('missing head geometry reports an error instead of showing an unrelated schematic',async()=>{
 await assert.rejects(loadCuttingHeadPreview({},{}),/没有外形模型/);
 await assert.rejects(loadCuttingHeadPreview({},{kind:'Builtin',builtinDefinitionId:'missing-definition'}),/没有外形模型/);
});

test('the native default schematic uses the same three meshes as trajectory display and is explicitly identified',async()=>{
 const result=await loadCuttingHeadPreview({},{kind:'Builtin'});
 try{assert.equal(result.schematic,true);assert.equal(result.group.children.length,3);assert.deepEqual(result.tcp.toArray(),[0,0,0]);}finally{release(result);}
});
