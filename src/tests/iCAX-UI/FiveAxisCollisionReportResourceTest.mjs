import test from 'node:test';
import assert from 'node:assert/strict';
import {hydrateCollisionReports} from '../../apps/five-axis-cam/webpage/collisionReportResource.mjs';
import {encodeMachineJSONResource} from './fixtures/machineJSONResource.mjs';
const report=(count=3)=>({schema:'cam.simulation-collision',schemaVersion:'1',validity:'Current',status:'Collision',collisionCount:'1',uncheckedCount:'0',frames:Array.from({length:count},(_,i)=>({position:[i+.12345678901234568,1.0000000000000002,0],rotation:[0,0,0,1]})),segments:Array.from({length:count-1},(_,i)=>({index:String(i),status:i?'Clear':'Collision',certifiedClearanceMm:i?.30000000000000004:0,witness:i?undefined:{headBoxIndex:'2',headPoint:[.12345678901234568,0,0],obstaclePoint:[0,0,0]}})),keypoints:[{index:'0',frameIndex:'0',status:'Collision'}]});
function fixture(body=report()){
 const {frames,segments,keypoints,...summary}=body,reference={url:'resource://collision/report',version:'7'};
 const metadata={...summary,frameCount:String(frames.length),segmentCount:String(segments.length),reportResourceRef:reference};
 let reads=0;const resources={async get(url,options){reads++;assert.equal(url,reference.url);assert.equal(options.headers['ICAX-Resource-Version'],'7');return new Response(encodeMachineJSONResource(body),{headers:{'ICAX-Resource-Version':'7'}});}};
 return {metadata,resources,get reads(){return reads;},payload:()=>({setups:[{id:'setup',simulationCheck:structuredClone(metadata)}]})};
}
test('a 19437-segment report retains every frame, keypoint and collision witness through the fixed-version resource',async()=>{
 const body=report(19438),f=fixture(body),loaded=await hydrateCollisionReports(f.payload(),{resources:f.resources});
 assert.deepEqual(loaded.setups[0].simulationCheck,{...JSON.parse(JSON.stringify(body)),reportResourceRef:f.metadata.reportResourceRef});
 assert.ok(JSON.stringify(f.metadata).length<1024);await hydrateCollisionReports(f.payload(),{resources:f.resources});assert.equal(f.reads,1);
});
test('mismatched trajectory counts, index order and non-finite pose data reject the complete report',async()=>{
 for(const mutate of [m=>m.frameCount='4',m=>m.segmentCount='1',m=>m.status='Clear']){
  const f=fixture(),payload=f.payload();mutate(payload.setups[0].simulationCheck);await assert.rejects(hydrateCollisionReports(payload,{resources:f.resources}),/不一致/);
 }
 for(const mutate of [b=>b.segments[0].index='1',b=>b.frames[0].rotation=[0,0,1],b=>b.frames[0].position=[0,NaN,0]]){
  const body=report();mutate(body);const f=fixture(body);await assert.rejects(hydrateCollisionReports(f.payload(),{resources:f.resources}),/无效/);
 }
});
test('a version mismatch is rejected and retryable, while missing and stale reports need no resource fetch',async()=>{
 const f=fixture(),get=f.resources.get.bind(f.resources);let wrong=true;
 f.resources.get=async(...args)=>wrong?new Response(encodeMachineJSONResource(report()),{headers:{'ICAX-Resource-Version':'8'}}):get(...args);
 await assert.rejects(hydrateCollisionReports(f.payload(),{resources:f.resources}),/版本不一致/);wrong=false;
 await hydrateCollisionReports(f.payload(),{resources:f.resources});assert.equal(f.reads,1);
 const absent={setups:[{simulationCheck:{validity:'Missing'}},{simulationCheck:{validity:'Stale'}}]};assert.equal(await hydrateCollisionReports(absent,{}),absent);
});
