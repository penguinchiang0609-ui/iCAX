import test from 'node:test';
import assert from 'node:assert/strict';
import {polynomialValue,machineSegmentJoints,machinePlaybackJoints,validateMachinePolynomial} from '../../apps/five-axis-cam/webpage/machineTrajectory.mjs';
import {advanceMachinePlayback} from '../../apps/five-axis-cam/webpage/machinePlaybackTiming.mjs';
const first={joints:[0],durationSeconds:0},last={joints:[10],durationSeconds:2,motion:{interpolation:'QuinticNormalized',coefficients:[[0,0,0,100,-150,60]]}};
test('physical playback follows the normalized quintic rather than the endpoint chord',()=>{
 assert.equal(machinePlaybackJoints([first,last],.25)[0],1.03515625);
 assert.notEqual(machinePlaybackJoints([first,last],.25)[0],2.5);
 assert.equal(machineSegmentJoints(first,last,.5,1)[0],9.375);
 assert.equal(machineSegmentJoints(first,last,0,1)[0],0);
 assert.equal(machineSegmentJoints(first,last,1,1)[0],0);
 assert.equal(machineSegmentJoints(first,last,0,2)[0],0);
 assert.equal(machineSegmentJoints(first,last,1,2)[0],0);
 validateMachinePolynomial(first,last);
});
test('time derivatives use seconds and constant terms do not contaminate derivatives',()=>{
 const c=[17,2,3,4,5,6];assert.equal(polynomialValue(c,0,0,2),17);assert.equal(polynomialValue(c,0,1,2),1);assert.equal(polynomialValue(c,0,2,2),1.5);assert.equal(polynomialValue(c,0,3,2),3);
});
test('zero-time semantic events retain process order and never create a moving segment',()=>{
 const event={...first,laser:true},state={cursor:0,report:{frames:[first,event,last]}};
 advanceMachinePlayback(state,.5);assert.equal(state.cursor,1.25);assert.equal(machinePlaybackJoints(state.report.frames,state.cursor)[0],1.03515625);
 assert.throws(()=>machineSegmentJoints(first,{joints:[1],durationSeconds:0},0),/零时长/);
});
test('missing, corrupt or mismatched formal motion is rejected',()=>{
 assert.throws(()=>machineSegmentJoints(first,{joints:[10],durationSeconds:2},.5),/缺少连续/);
 assert.throws(()=>validateMachinePolynomial(first,{...last,joints:[11]}),/端点不一致/);
 assert.throws(()=>polynomialValue([0,1],.5),/无效/);
});
