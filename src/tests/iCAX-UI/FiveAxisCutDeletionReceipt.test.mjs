import assert from 'node:assert/strict';
import {refreshMachiningState} from '../../apps/five-axis-cam/webpage/domainActions.mjs';
const expected={epoch:'session',mutationSequence:'1'};
const domain=()=>({expected:structuredClone(expected),activeSetupId:'setup',workpieces:[],placements:[],fixtures:[],fixtureUnits:[],setups:[{id:'setup',generatedGeometry:{status:'Missing'}}],intentToolpaths:[{id:'one',setupId:'setup'},{id:'two',setupId:'setup'},{id:'other',setupId:'other-setup'}]});
const receipt=()=>({statePatch:{expected:{...expected,mutationSequence:'2'},inputRevision:'2',setup:{id:'setup',definitionOrder:['two'],generatedGeometry:{status:'Missing'}},intentToolpaths:[{id:'two',name:'kept'}],removedIntentIds:['one'],fixtureValidity:{setup:{},units:{}},transitionStates:{},operationOrder:[]}});
let reads=0,renders=0;const context={isCurrentProject:()=>true,sceneProxy:{async invoke(){reads++;throw Error('full-state fallback')}}},ops={renderProject(){renders++}};
const view={scene:{domain:domain()}};const kept=view.scene.domain.intentToolpaths[1];
assert.equal(await refreshMachiningState(context,view,ops,{expected},receipt()),true);
assert.equal(reads,0);assert.equal(renders,1);assert.deepEqual(view.scene.domain.intentToolpaths.map(c=>c.id),['two','other']);assert.equal(view.scene.domain.intentToolpaths[0],kept);assert.equal(kept.name,'kept');
// A receipt cannot remove another setup's row or apply to a newer snapshot.
for(const mutate of [r=>r.statePatch.removedIntentIds.push('other'),r=>r.statePatch.intentToolpaths.push({id:'new'}),r=>r.statePatch.removedIntentIds.push('missing')]){
 const next={scene:{domain:domain()}},r=receipt();mutate(r);await assert.rejects(refreshMachiningState(context,next,ops,{expected},r),/full-state fallback/);assert.equal(next.scene.domain.intentToolpaths.length,3);
}
const stale={scene:{domain:domain()}};stale.scene.domain.expected.mutationSequence='3';await assert.rejects(refreshMachiningState(context,stale,ops,{expected},receipt()),/full-state fallback/);
await assert.rejects(refreshMachiningState(context,{scene:{domain:domain()}},ops,{expected,action:'Move'},receipt()),/full-state fallback/);
console.log('PASS delete receipt updates only acknowledged rows; empty geometry needs no scene read; stale and incomplete receipts rejected');
