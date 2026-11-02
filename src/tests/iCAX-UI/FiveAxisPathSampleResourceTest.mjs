import test from 'node:test';
import assert from 'node:assert/strict';
import {hydratePathSamples,parsePathSamples} from '../../apps/five-axis-cam/webpage/pathSampleResource.mjs';
import {createPathSamplesResource} from './fixtures/pathSamplesResource.mjs';

const points=[[1234567.123456789,-.000000123456789,3.141592653589793],[11.123456789012345,25.98765432109876,-7.100000000000003],[90.00000000000001,2.0000000000000004,18.999999999999996]];
const directions=[[0,0,1],[0,.6,.8],[1,0,0]];
const part=()=>({role:'Rapid',points:structuredClone(points),directions:structuredClone(directions)});
const transition=(from,to,geometryStatus='Failed',extra={})=>({from,to,geometryStatus,validation:'Unchecked',editingOnly:true,pointCount:String(points.length),frame:'Setup',referencePoint:'ToolTip',reason:'TRAVEL_CLEARANCE_BUDGET',...extra});
function fixture(transitions,entries=transitions.map(t=>({id:`rapid:${t.from}/${t.to}`,parts:[part()]}))){
 const bytes=createPathSamplesResource(entries),requests=[],sampleGeometryRef={url:'resource://icax/test/path-samples',version:'7'};
 const payload={intentToolpaths:[],setups:[{id:'setup',generatedGeometry:{status:'Current',sampleGeometryRef,operations:[],transitions:structuredClone(transitions)}}]};
 const sceneProxy={resources:{async get(url,options){requests.push({url,options});return new Response(bytes,{status:200,headers:{'ICAX-Resource-Version':sampleGeometryRef.version}});}}};
 return {payload,sceneProxy,bytes,requests,get transitions(){return payload.setups[0].generatedGeometry.transitions;}};
}

test('metadata-only failed editing candidate hydrates original ICPS doubles and directions without gaining clearance',async()=>{
 const metadata=transition('29','30'),f=fixture([metadata]);assert.equal(f.transitions[0].points,undefined);assert.equal(f.transitions[0].directions,undefined);
 const parsed=parsePathSamples(f.bytes.buffer);assert.deepEqual(parsed.get('rapid:29/30'),[part()]);
 const result=await hydratePathSamples(f.payload,f.sceneProxy);assert.equal(result,f.payload);
 const hydrated=f.transitions[0];assert.deepEqual(hydrated.points,points);assert.deepEqual(hydrated.directions,directions);
 assert.deepEqual(Object.fromEntries(Object.keys(metadata).map(key=>[key,hydrated[key]])),metadata);
 assert.equal(hydrated.geometryStatus,'Failed');assert.equal(hydrated.validation,'Unchecked');assert.equal(hydrated.editingOnly,true);
 assert.notEqual(hydrated.points[0][0],Math.fround(points[0][0]),'resource transport must not reduce original sample precision to float32');
 assert.deepEqual(f.requests,[{url:'resource://icax/test/path-samples',options:{headers:{'ICAX-Resource-Version':'7',Accept:'application/vnd.icax.flatbuffer'}}}]);
});

test('failed collision candidate hydrates for editing while retaining its failed segment and collision result',async()=>{
 const metadata=transition('30','31','Failed',{validation:'Collision',failedSegment:'1',reason:'RAPID_MANUAL_PATH_BLOCKED'}),f=fixture([metadata]);
 await hydratePathSamples(f.payload,f.sceneProxy);assert.deepEqual(f.transitions[0],{...metadata,points,directions});
});

test('failed entries without explicit editing eligibility and candidates shorter than two points remain metadata-only',async()=>{
 const ts=[transition('a','b','Failed',{editingOnly:false}),transition('b','c','Failed',{editingOnly:undefined}),transition('c','d','Failed',{pointCount:'1'}),transition('d','e','Stale',{editingOnly:false}),transition('e','f','Pending')];
 const f=fixture(ts,[]),before=[...f.transitions];await hydratePathSamples(f.payload,f.sceneProxy);
 assert.deepEqual(f.transitions,ts);f.transitions.forEach((t,i)=>{assert.equal(t,before[i]);assert.equal(t.points,undefined);assert.equal(t.directions,undefined);});
});

test('stale automatic rapid geometry remains editable without a manual path or a clearance certificate',async()=>{
 const metadata=transition('a','b','Stale'),f=fixture([metadata]);await hydratePathSamples(f.payload,f.sceneProxy);
 assert.deepEqual(f.transitions[0],{...metadata,points,directions});assert.equal(f.transitions[0].validation,'Unchecked');
});

test('successful generated and ordinary draft metadata retain their previous hydration behavior and statuses',async()=>{
 const ts=[transition('a','b','Generated',{validation:'Clear',editingOnly:undefined,reason:undefined}),transition('b','c','Draft',{editingOnly:undefined,reason:undefined})],f=fixture(ts);
 await hydratePathSamples(f.payload,f.sceneProxy);
 for(let i=0;i<ts.length;i++)assert.deepEqual(f.transitions[i],{...ts[i],points,directions});
 assert.equal(f.transitions[0].geometryStatus,'Generated');assert.equal(f.transitions[0].validation,'Clear');assert.equal(f.transitions[1].geometryStatus,'Draft');assert.equal(f.transitions[1].validation,'Unchecked');
});

test('failed candidates must match the current metadata point count before receiving resource geometry',async()=>{
 for(const status of ['Failed','Generated','Draft']){
  const f=fixture([transition('a','b',status,{pointCount:'4'})]);
  await assert.rejects(hydratePathSamples(f.payload,f.sceneProxy),/空移采样与当前路径不一致/);
  assert.equal(f.transitions[0].points,undefined);assert.equal(f.transitions[0].geometryStatus,status);assert.equal(f.transitions[0].validation,'Unchecked');
 }
});
