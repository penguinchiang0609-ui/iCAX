import test from 'node:test';
import assert from 'node:assert/strict';
import {repairInOrder,scopeContains} from '../../apps/five-axis-cam/webpage/collisionRepairSequence.mjs';

const issue=(id,status,reason='',role='Cut')=>({index:id,frameIndex:id,status,reason,location:{intentId:role==='Cut'?'cut':'next',role,pointId:id,from:'cut',to:'next'}});
test('batch repair finishes limit issues before collisions and rechecks only each changed path',async()=>{
 let report={validity:'Current',keyposeChecks:[issue('1','Collision'),issue('2','Unchecked','CAM_HEAD_LIMIT_NO_SOLUTION'),issue('3','Unchecked','CAM_HEAD_LIMIT_NO_SOLUTION','Rapid')],interpolationChecks:[]};
 const calls=[],origin={intentId:'cut',anchor:{segmentId:'segment',parameter:.5},direction:[0,0,1],positionOffset:[0,0,0]};
 const result=await repairInOrder({pathKind:'Cutting'},{readReport:()=>report,active:()=>true,cancelled:()=>false,
  check:async scope=>(calls.push(['check',scope]),true),repair:async(row,phase,origins)=>{
   calls.push(['repair',row.frameIndex,phase,origins]);report.keyposeChecks=report.keyposeChecks.filter(x=>x.frameIndex!==row.frameIndex);
   return {changed:true,toleranceOrigins:phase==='Limits'?[origin]:[]};
  }});
 assert.deepEqual(calls,[['check',{pathKind:'Cutting'}],['repair','2','Limits',[]],['check',{intentId:'cut'}],['repair','1','Collision',[origin]],['check',{intentId:'cut'}]]);
 assert.equal(result.changed,2);assert.equal(report.keyposeChecks[0].location.role,'Rapid');
});
test('single rapid repair never includes cutting paths or a different connection',()=>{
 const scope={from:'a',to:'b'};
 assert(scopeContains(scope,{role:'Rapid',from:'a',to:'b'}));
 assert(!scopeContains(scope,{role:'Cut',intentId:'b'}));assert(!scopeContains(scope,{role:'Rapid',from:'c',to:'b'}));
});
test('unresolved limits are tried once and cancellation stops before another issue',async()=>{
 const report={validity:'Current',keyposeChecks:[issue('1','Unchecked','CAM_HEAD_LIMIT_NO_SOLUTION'),issue('2','Collision')],interpolationChecks:[]},calls=[];let cancel=false;
 const result=await repairInOrder({intentId:'cut'},{readReport:()=>report,active:()=>true,cancelled:()=>cancel,check:async()=>true,
  repair:async(row,phase)=>{calls.push(phase);if(phase==='Collision')cancel=true;return {changed:false};}});
 assert.deepEqual(calls,['Limits','Collision']);assert.equal(result.changed,0);assert(result.cancelled);assert.equal(result.unresolved.length,1);
});
test('failed verification ends the sequence without continuing on an unconfirmed report',async()=>{
 let checks=0,repairs=0;
 const report={validity:'Current',keyposeChecks:[issue('1','Unchecked','CAM_HEAD_LIMIT_NO_SOLUTION'),issue('2','Collision')],interpolationChecks:[]};
 const result=await repairInOrder({intentId:'cut'},{readReport:()=>report,active:()=>true,cancelled:()=>false,check:async()=>++checks===1,repair:async()=>{repairs++;return {changed:true};}});
 assert.equal(repairs,1);assert(result.incomplete);assert.equal(result.changed,1);
});
