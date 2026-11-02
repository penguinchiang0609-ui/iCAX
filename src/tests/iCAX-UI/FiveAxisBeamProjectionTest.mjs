import test from 'node:test';
import assert from 'node:assert/strict';
import {refreshMachiningState} from '../../apps/five-axis-cam/webpage/domainActions.mjs';
import {fixtureLightInspectionTarget} from '../../apps/five-axis-cam/webpage/fixtureLightValidation.mjs';

const identity=[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1];
function fixture(){
 const beam={validity:'Current',status:'Clear',reportRevision:{revision:'proof'}};
 const assembly={id:'a',validity:'Current',geometryValidity:'Current',beamValidity:'Current',cutClearance:{validity:'Current',status:'Clear'}};
 const setup={id:'s',rapidSettings:{followHeightMm:2},generatedGeometry:{status:'Current',operations:[{intentId:'c',parts:[{role:'Cut',points:[[0,0,0],[10,0,0]],directions:[[0,0,1],[0,0,1]]}]}]},fixtureBeamCheck:beam,fixtureDesign:{assembly}};
 const cut={id:'c',setupId:'s',placementId:'p'};
 const domain={expected:{epoch:'e',mutationSequence:'1'},activeSetupId:'s',setups:[setup],intentToolpaths:[cut],fixtureUnits:[],fixtures:[],workpieces:[],placements:[{id:'p',setupId:'s',transform:identity}]};
 const view={fiveAxisArea:'toolpath',scene:{domain,intentToolpaths:[cut]}};
 const patch={expected:{epoch:'e',mutationSequence:'2'},inputRevision:'2',setup:{id:'s',generatedGeometry:{status:'Stale'},fixtureBeamCheck:{...beam,validity:'Stale',status:'Unchecked'}},intentToolpaths:[{...cut}],fixtureValidity:{setup:{assembly:{id:'a',validity:'Current',geometryValidity:'Current',cutClearance:{validity:'Stale',status:'Unchecked'}}},units:{}}};
 return {view,domain,setup,patch};
}
test('an acknowledged pose edit projects the native stale beam certificate without another scene read',async()=>{
 const f=fixture();let renders=0;
 const context={isCurrentProject:()=>true,sceneProxy:{invoke(){throw Error('Unexpected scene query');}}};
 await refreshMachiningState(context,f.view,{renderProject(){renders++;}},{expected:{...f.domain.expected}},{statePatch:f.patch});
 assert.equal(f.setup.fixtureBeamCheck.validity,'Stale');assert.equal(f.setup.fixtureBeamCheck.status,'Unchecked');
 assert.equal(f.setup.fixtureDesign.assembly.geometryValidity,'Current');assert.equal(f.setup.fixtureDesign.assembly.beamValidity,'Stale');assert.equal(renders,1);
});
test('an incomplete compact response cannot leave an old Clear beam result usable while its full read is pending',async()=>{
 const f=fixture();delete f.patch.setup.fixtureBeamCheck;let resolve;
 const response=new Promise(done=>{resolve=done;});
 const context={isCurrentProject:()=>true,sceneProxy:{invoke:()=>response}};
 const pending=refreshMachiningState(context,f.view,{renderProject(){}},{expected:{...f.domain.expected}},{statePatch:f.patch});
 assert.equal(f.setup.fixtureBeamCheck.validity,'Stale');assert.equal(f.setup.fixtureBeamCheck.status,'Unchecked');
 const state=structuredClone(f.domain);state.expected=f.patch.expected;state.setups[0].fixtureBeamCheck={...f.patch.setup.fixtureBeamCheck,validity:'Stale',status:'Unchecked'};
 resolve(state);await pending;assert.equal(f.view.scene.domain.setups[0].fixtureBeamCheck.validity,'Stale');
});
test('a failed full read leaves optical delivery stale rather than reviving a cached Clear result',async()=>{
 const f=fixture();delete f.patch.setup.fixtureBeamCheck;
 const context={isCurrentProject:()=>true,sceneProxy:{invoke:async()=>{throw Error('Read failed');}}};
 await assert.rejects(refreshMachiningState(context,f.view,{renderProject(){}},{expected:{...f.domain.expected}},{statePatch:f.patch}),/Read failed/);
 assert.equal(f.setup.fixtureBeamCheck.validity,'Stale');assert.equal(f.setup.fixtureBeamCheck.status,'Unchecked');
});
test('optical edit targets use the surface target in Workpiece coordinates and retain endpoint semantics',()=>{
 const f=fixture();f.domain.placements[0].transform=[1,0,0,100,0,1,0,200,0,0,1,300,0,0,0,1];
 const issue={intentId:'c',role:'Cut',beamWitness:{frame:'Setup',beamOrigin:[110,220,332],beamEnd:[110,220,-100]}};
 f.view.fiveFixtureLight={setupId:'s',report:{validity:'Current',issues:[issue]},loadedKey:JSON.stringify(['e','s',f.setup.fixtureBeamCheck.reportRevision,'Current'])};
 assert.deepEqual(fixtureLightInspectionTarget(f.view,0),{intentId:'c',nearPoint:[10,20,30],maximumDistanceMm:1});
 issue.role='LeadIn';assert.deepEqual(fixtureLightInspectionTarget(f.view,0),{intentId:'c',pointId:'start'});
 issue.role='LeadOut';assert.deepEqual(fixtureLightInspectionTarget(f.view,0),{intentId:'c',pointId:'end'});
 issue.role='Cut';issue.beamWitness.frame='Machine';assert.equal(fixtureLightInspectionTarget(f.view,0),null);
 f.setup.fixtureBeamCheck.validity='Stale';assert.equal(fixtureLightInspectionTarget(f.view,0),null);
});
