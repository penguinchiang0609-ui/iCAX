import test from 'node:test';
import assert from 'node:assert/strict';
import {cancelRejectedMachineMoves} from '../../apps/five-axis-cam/webpage/machineParameterEdits.mjs';
import {cancelRejectedMachinePoseDrafts} from '../../apps/five-axis-cam/webpage/machineIK.mjs';

test('opening a collision box can retire an unapplied rejected motion without discarding other parameter drafts',()=>{
 const blocked={error:'collision rejected',edit:{method:'CamSimulation.MoveMachine'}},post={error:'invalid header',edit:{method:'CamSimulation.ConfigureMachinePost'}},pending={edit:{method:'Machine.SetJointLimits'}};
 const view={fiveMachine:{error:blocked.error,jointDraft:{axis:'500',other:'10'},postDirty:true,post:{header:'draft'}},fiveMachineParameterEdits:{entries:new Map([['joint:axis',blocked],['post',post],['limits:other',pending]])}};
 assert.equal(cancelRejectedMachineMoves(view),true);assert.deepEqual([...view.fiveMachineParameterEdits.entries.keys()],['post','limits:other']);assert.deepEqual(view.fiveMachine.jointDraft,{other:'10'});assert.equal(view.fiveMachine.postDirty,true);assert.equal(view.fiveMachine.post.header,'draft');assert.equal(view.fiveMachine.error,'');assert.equal(cancelRejectedMachineMoves(view),false);
});

test('collision correction retires failed IK drafts on previously selected parts and preserves valid pending input',()=>{
 const rejected={machineId:'m',tip:'old',dirty:true,error:'blocked',values:['100']},valid={machineId:'m',tip:'current',dirty:true,error:'',values:['5']},view={error:'blocked',fiveMachineIK:{machineId:'m',pose:valid,hud:{tip:'current'},drafts:new Map([['old',rejected],['current',valid]])}};
 assert.equal(cancelRejectedMachinePoseDrafts(view),true);assert.deepEqual([...view.fiveMachineIK.drafts.keys()],['current']);assert.equal(view.fiveMachineIK.pose,valid);assert.equal(view.fiveMachineIK.hud.tip,'current');assert.equal(view.error,'');assert.equal(cancelRejectedMachinePoseDrafts(view),false);
});
