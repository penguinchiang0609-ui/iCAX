import test from 'node:test';
import assert from 'node:assert/strict';
import {hydrateMachinePlan,hydrateMachineProgram,parseMachineJSONResource} from '../../apps/five-axis-cam/webpage/machinePlanResource.mjs';
import {encodeMachineJSONResource,machineResourceTransport} from './fixtures/machineJSONResource.mjs';
const identity=[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1];
function report(count=2){return {schema:'cam.machine-motion-plan',schemaVersion:'2',interpolation:'QuinticNormalized',status:'Ready',validity:'Current',playbackAllowed:true,modelKey:'fixed-machine',resource:{url:'resource://persisted-plan',version:'3'},tool:1,support:0,nodes:[{id:'table',parent:-1,type:'fixed',axis:[0,0,1],origin:identity},{id:'head',parent:0,type:'prismatic',axis:[0,0,1],origin:identity}],sampleCount:String(count),durationSeconds:(count-1)*.0001,frames:Array.from({length:count},(_,i)=>({joints:[0,i+.12345678901234568],durationSeconds:i?.0001:0,...(i?{motion:{interpolation:'QuinticNormalized',coefficients:[[0,0,0,0,0,0],[i-1+.12345678901234568,1,0,0,0,0]]}}:{}),laser:i>0,role:'Cut',position:[i+.12345678901234568,.9876543210987654,1.0000000000000002],rotation:[0,0,0,1],sourceStation:String(i),sourceFraction:.12345678901234568,seedSource:'Continuous previous joint solution; refined midpoint endpoint; native verified',branchKey:'Validated fixed signed tool axis and fixed-position adaptive source interval'}))};}
test('complete 60000-frame resource exceeds typed SDO size while retaining every double and semantic identity',async()=>{
 const body=report(60000),transport=machineResourceTransport(),metadata=transport.wire(body,'planResourceRef');assert.ok(encodeMachineJSONResource(body).byteLength>16*1024*1024);assert.equal(Object.hasOwn(metadata,'frames'),false);
 const loaded=await hydrateMachinePlan(metadata,{resources:transport.resources});assert.equal(loaded.frames.length,60000);assert.equal(loaded.frames[59999].joints[1],body.frames[59999].joints[1]);assert.equal(loaded.frames[59321].position[2],1.0000000000000002);assert.equal(Object.hasOwn(loaded,'planResourceRef'),false);assert.deepEqual(loaded,body);
 const changedTransportRef=transport.wire(body,'planResourceRef'),reloaded=await hydrateMachinePlan(changedTransportRef,{resources:transport.resources});assert.deepEqual(reloaded,loaded);
});
test('fixed versions and FlatBuffer envelope are verified and a failed version read is retryable',async()=>{
 const body=report(),metadata={...body,frames:undefined,planResourceRef:{url:'resource://one',version:'7'}},bytes=encodeMachineJSONResource(body);let calls=0;
 const resources={async get(url,options){assert.equal(options.headers['ICAX-Resource-Version'],'7');assert.equal(options.headers.Accept,'application/vnd.icax.flatbuffer');return new Response(bytes,{headers:{'ICAX-Resource-Version':++calls===1?'8':'7'}});}};
 await assert.rejects(hydrateMachinePlan(metadata,{resources}),/版本不一致/);assert.equal((await hydrateMachinePlan(metadata,{resources})).frames.length,2);await hydrateMachinePlan(metadata,{resources});assert.equal(calls,2);
 const bad=bytes.slice();bad[4]=0;assert.throws(()=>parseMachineJSONResource(bad.buffer),/格式无效/);assert.throws(()=>parseMachineJSONResource(bytes.buffer.slice(0,12)),/不完整/);
});
test('wrong model, frame count or non-positive certified time cannot enter playback',async()=>{
 for(const mutate of [m=>m.modelKey='other',m=>m.sampleCount='3',m=>m.frames[1].durationSeconds=0,m=>m.frames[1].joints=[1]]){
  const body=report(),transport=machineResourceTransport(),metadata=transport.wire(body,'planResourceRef');const changed=structuredClone(body);mutate(changed);
  const resources={async get(){return new Response(encodeMachineJSONResource(changed),{headers:{'ICAX-Resource-Version':'1'}});}};
  await assert.rejects(hydrateMachinePlan(metadata,{resources}),/不一致|无效|零时长/);
 }
});
test('departure or cancellation during resource loading discards the complete late response',async()=>{
 const body=report(),metadata={...body,frames:undefined,planResourceRef:{url:'resource://late',version:'1'}};let active=true,release;
 const resources={get:()=>new Promise(resolve=>release=resolve)},pending=hydrateMachinePlan(metadata,{resources},{active:()=>active});active=false;release(new Response(encodeMachineJSONResource(body),{headers:{'ICAX-Resource-Version':'1'}}));assert.equal(await pending,null);
});
test('program documents use the fixed-version resource channel and retain exact native instructions',async()=>{
 const source=report(),body={modelKey:source.modelKey,format:'JointJSON',extension:'json',instructionCount:1,machineReady:false,content:'native exact output',motion:{format:'ICAX-JOINT-MOTION-2',interpolation:'QuinticNormalized',machine:{modelKey:source.modelKey,nodes:source.nodes},instructions:[{index:0,joints:[0,.12345678901234568],axes:{X:.12345678901234568},process:false,durationSeconds:0},{index:1,joints:[0,1.1234567890123457],axes:{X:1.1234567890123457},process:true,durationSeconds:.0001,motion:source.frames[1].motion}]}};
 const transport=machineResourceTransport(),metadata=transport.wire(body,'programResourceRef');assert.equal(Object.hasOwn(metadata,'motion'),false);assert.equal(Object.hasOwn(metadata,'content'),false);
 assert.deepEqual(await hydrateMachineProgram(metadata,{resources:transport.resources}),body);
});
