import test from 'node:test';
import assert from 'node:assert/strict';
import {advanceMachinePlayback} from '../../apps/five-axis-cam/webpage/machinePlaybackTiming.mjs';
import {machineInstructionIndex} from '../../apps/five-axis-cam/webpage/machineProgramView.mjs';
function fixture(count=100,duration=.001){
 return {cursor:0,report:{frames:Array.from({length:count+1},(_,i)=>({durationSeconds:i?duration:0,joints:[i],laser:i>=50&&i<75}))}};
}
test('one hundred certified 1 ms motions complete in 0.1 seconds',()=>{
 const state=fixture();assert.equal(advanceMachinePlayback(state,.1),100);assert.equal(machineInstructionIndex(state),100);
});
test('short frames accumulate exactly across multiple playback ticks',()=>{
 const state=fixture();for(let i=0;i<10;i++)advanceMachinePlayback(state,.01);assert.equal(state.cursor,100);
});
test('partial short segment keeps the joint fraction, active instruction and process boundary aligned',()=>{
 const state=fixture();advanceMachinePlayback(state,.0495);assert.ok(Math.abs(state.cursor-49.5)<1e-10);assert.equal(machineInstructionIndex(state),50);assert.equal(state.report.frames[machineInstructionIndex(state)-1].laser,false);
 advanceMachinePlayback(state,.001);assert.ok(Math.abs(state.cursor-50.5)<1e-10);assert.equal(machineInstructionIndex(state),51);assert.equal(state.report.frames[machineInstructionIndex(state)-1].laser,true);
});
test('native sub-millisecond times retain their certified duration',()=>{
 const state=fixture(100,.00001);assert.equal(advanceMachinePlayback(state,.001),100);
});
test('invalid native segment time stops at the last valid frame instead of inventing timing',()=>{
 for(const duration of [-1,NaN,Infinity]){
  const state=fixture();state.report.frames[3].durationSeconds=duration;
  assert.throws(()=>advanceMachinePlayback(state,.1),/第 3 段运动时间无效/);assert.equal(state.cursor,2);
 }
});
test('a zero-time event with axis movement stops at the last valid frame',()=>{
 const state=fixture();state.report.frames[3].durationSeconds=0;
 assert.throws(()=>advanceMachinePlayback(state,.1),/零时长事件包含轴移动/);assert.equal(state.cursor,2);
});
test('an alarm frame limits time consumption to the certified stopping position',()=>{
 const state=fixture();assert.equal(advanceMachinePlayback(state,.1,37),37);assert.equal(machineInstructionIndex(state),37);
});
