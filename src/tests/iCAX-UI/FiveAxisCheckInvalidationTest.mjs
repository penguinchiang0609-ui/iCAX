import test from 'node:test';
import assert from 'node:assert/strict';
import {applyPlacementReceipt} from '../../apps/five-axis-cam/webpage/placementReceipt.mjs';
import {refreshMachiningState} from '../../apps/five-axis-cam/webpage/domainActions.mjs';
import {workspaceAlertItems} from '../../apps/five-axis-cam/webpage/alerts.mjs';

function fixture(proof){
  const p={id:'p',setupId:'s',workpieceId:'w'},s={id:'s',simulationCheck:proof,fixtureCheck:{validity:'Missing'},fixtureBeamCheck:{validity:'Unavailable'},generatedGeometry:{status:'Missing',operations:[],transitions:[]}};
  const expected={epoch:'e',mutationSequence:'1'},view={scene:{domain:{expected,activeSetupId:'s',setups:[s],placements:[p],workpieces:[{id:'w'}],intentToolpaths:[],fixtures:[],fixtureUnits:[]}}};
  const request={expected,setupId:'s',placementId:'p'},result={statePatch:{expected:{epoch:'e',mutationSequence:'2'},inputRevision:'2',placement:{id:'p'},setup:{id:'s'}}};
  return {view,s,request,result};
}
test('placement preserves absent, missing and unavailable inspections without expired-result alerts',()=>{
  for(const proof of [undefined,{}, {validity:'Missing',status:'Unchecked'},{validity:'Unavailable',status:'Unchecked'}]){
    const f=fixture(structuredClone(proof)),before=structuredClone(f.s.simulationCheck);
    assert.equal(applyPlacementReceipt(f.view,f.request,f.result),true);assert.deepEqual(f.s.simulationCheck,before);
    assert.equal(f.s.fixtureCheck.validity,'Missing');assert.equal(f.s.fixtureBeamCheck.validity,'Unavailable');assert.deepEqual(workspaceAlertItems(f.view),[]);
  }
});
test('a real prior inspection is invalidated and still produces its expired-result warning',()=>{
  for(const status of ['Clear','Collision','Unchecked']){
    const f=fixture({validity:'Current',status,collisionCount:status==='Collision'?'1':'0',reportResourceRef:{url:'resource://previous-report'},scope:'BuiltinSchematicWorkpieceAndFixtures'});
    applyPlacementReceipt(f.view,f.request,f.result);assert.equal(f.s.simulationCheck.validity,'Stale');assert.equal(f.s.simulationCheck.status,'Unchecked');assert.equal(f.s.simulationCheck.reportResourceRef.url,'resource://previous-report');
    assert.deepEqual(workspaceAlertItems(f.view).map(a=>a.id),['collision-stale']);
  }
});
test('deferred pose updates preserve never-checked state but revoke an existing Clear result immediately',async()=>{
  for(const validity of ['Missing','Current']){
    const f=fixture({validity,status:validity==='Current'?'Clear':'Unchecked'});
    f.view.scene.domain.intentToolpaths=[{id:'cut',setupId:'s'}];
    const result={statePatch:{...f.result.statePatch,deferredPose:true,invalidatedIntentIds:['cut']}};
    await refreshMachiningState({sceneProxy:{invoke(){throw Error('Unexpected native read');}}},f.view,{renderProject(){}},f.request,result);
    assert.equal(f.s.simulationCheck.validity,validity==='Missing'?'Missing':'Stale');assert.equal(f.s.simulationCheck.status,'Unchecked');
    assert.equal(workspaceAlertItems(f.view).some(a=>a.id==='collision-stale'),validity==='Current');
  }
});
