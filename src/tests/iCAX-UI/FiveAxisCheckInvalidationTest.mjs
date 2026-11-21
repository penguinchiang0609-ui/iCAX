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
test('an assembly receipt preserves complete local toolpaths, prepared locating data and static fixture checks while revoking machine qualification',()=>{
 const f=fixture({validity:'Current',status:'Clear',reportResourceRef:{url:'resource://motion-report'}}),identity=[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1],moved=[...identity];moved[3]=35;
 const geometry={status:'Current',revision:'geometry-4',sampleGeometryRef:{url:'resource://paths',version:'4'},operations:[{intentId:'cut',status:'Current',parts:[{role:'Cut',points:[[1,2,3],[5,6,7]],directions:[[0,0,1],[0,0,1]]}]}],transitions:[{from:'cut',to:'other',endpointStatus:'Current',geometryStatus:'Generated',validation:'Clear',points:[[5,6,8],[9,10,8]],directions:[[0,0,1],[0,0,1]],frame:'Setup',lengthMm:Math.sqrt(32)}],rapidGeneratedCount:'1',rapidClearCount:'1'};
 const registration={status:'Prepared',machineStatus:'Qualified',placementId:'p',placementTransform:identity,assemblyTransform:identity,points:[{modelPoint:[1,2,3],query:{featureId:'point-1'}}],machineRegistration:{transform:identity,maximumResidualMm:0}},staticCheck={scope:'StaticAssembly',validity:'Current',status:'Clear',inputKey:'local-geometry-and-placement-key',pairs:[{first:'p',second:'fixture',distanceMm:2}]},support={validity:'Current',supportCheck:{status:'Supported'},cutClearance:{validity:'Current',status:'Clear'}};
 Object.assign(f.s,{generatedGeometry:geometry,registrationPlan:registration,fixtureCheck:staticCheck,fixtureDesign:{assembly:support}});f.view.scene.domain.fixtureUnits=[{id:'u',setupId:'s',design:{assembly:structuredClone(support)}}];
 const before={geometry:structuredClone(geometry),registration:structuredClone(registration),staticCheck:structuredClone(staticCheck),support:structuredClone(support),unit:structuredClone(f.view.scene.domain.fixtureUnits[0])};
 f.result.motionScope='Assembly';f.result.statePatch.setup={id:'s',assemblyTransform:moved,inputRevision:'unchanged-setup-revision',planValidity:'Stale'};
 assert.equal(applyPlacementReceipt(f.view,f.request,f.result),true);
 assert.equal(f.s.generatedGeometry,geometry);assert.deepEqual(geometry,before.geometry);assert.equal(f.s.registrationPlan,registration);assert.deepEqual(registration,{...before.registration,machineStatus:'Stale'});assert.equal(f.s.fixtureCheck,staticCheck);assert.deepEqual(staticCheck,before.staticCheck);assert.deepEqual(support,before.support);assert.deepEqual(f.view.scene.domain.fixtureUnits[0],before.unit);
 assert.equal(f.s.simulationCheck.validity,'Stale');assert.equal(f.s.simulationCheck.status,'Unchecked');assert.equal(f.s.simulationCheck.reportResourceRef.url,'resource://motion-report');assert.equal(f.s.planValidity,'Stale');assert.deepEqual(f.s.assemblyTransform,moved);
});
test('assembly receipts preserve unmeasured locating plans and matching machine qualifications, including an implicit identity frame',()=>{
 const identity=[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1];
 for(const machineStatus of ['Unmeasured','Qualified','Stale']){
  const f=fixture({validity:'Missing',status:'Unchecked'});f.s.registrationPlan={status:'Prepared',machineStatus,points:[{modelPoint:[1,2,3]}]};f.result.motionScope='Assembly';f.result.statePatch.setup={id:'s',assemblyTransform:identity};
  const before=structuredClone(f.s.registrationPlan);assert.equal(applyPlacementReceipt(f.view,f.request,f.result),true);assert.deepEqual(f.s.registrationPlan,before);assert.deepEqual(f.s.simulationCheck,{validity:'Missing',status:'Unchecked'});
 }
});
test('relative workpiece receipts continue expiring generated geometry, prepared points, static checks and adopted support',()=>{
 const f=fixture({validity:'Current',status:'Clear'});f.result.motionScope='Workpiece';f.s.registrationPlan={status:'Prepared',points:[{modelPoint:[1,2,3]}]};f.s.fixtureCheck={scope:'StaticAssembly',validity:'Current',status:'Clear'};f.s.fixtureDesign={assembly:{validity:'Current'}};
 f.s.generatedGeometry={status:'Current',operations:[{intentId:'cut',status:'Current',parts:[{points:[[1,2,3],[4,5,6]]}]}],transitions:[{endpointStatus:'Current',geometryStatus:'Generated',validation:'Clear',points:[[4,5,6],[7,8,9]]}],rapidGeneratedCount:'1',rapidClearCount:'1'};
 const geometry=f.s.generatedGeometry,localPaths=structuredClone(geometry.operations[0].parts);assert.equal(applyPlacementReceipt(f.view,f.request,f.result),true);assert.equal(f.s.generatedGeometry,geometry);assert.equal(geometry.status,'Stale');assert.equal(geometry.operations[0].status,'Stale');assert.deepEqual(geometry.operations[0].parts,localPaths);assert.equal(geometry.transitions[0].geometryStatus,'Stale');assert.equal(geometry.transitions[0].validation,'Unchecked');assert.equal(geometry.rapidGeneratedCount,'0');assert.equal(geometry.rapidClearCount,'0');assert.equal(f.s.registrationPlan.status,'Stale');assert.equal(f.s.fixtureCheck.validity,'Stale');assert.equal(f.s.fixtureDesign.assembly.validity,'Stale');assert.equal(f.s.simulationCheck.validity,'Stale');
});
test('assembly receipts immediately retire hydrated machine playback and instruction caches without removing the saved resource',()=>{
 const f=fixture({validity:'Current',status:'Clear'}),saved={url:'resource://machine-plan',revision:'5'},report={validity:'Current',playbackAllowed:true,frames:[{joints:[0,1,2],time:0}]},readReport={validity:'Current',playbackAllowed:true,frames:[{joints:[3,4,5],time:0}]},program={instructions:[{line:1,motion:'G1 X1'}]};
 f.s.machineMotionPlanRef=saved;f.view.fiveMachine={playing:true,animation:42,last:123,cursor:8,alarm:true,report,snapshot:report,snapshotKey:'old-snapshot',planReadKey:'old-read',planReadReport:readReport,planLoadedKey:'old-loaded',planValidity:'Current',program,programReport:report,programOpen:true};
 f.result.motionScope='Assembly';f.result.statePatch.setup={id:'s',assemblyTransform:[1,0,0,35,0,1,0,0,0,0,1,0,0,0,0,1],planValidity:'Stale'};
 const beforeReportFrames=structuredClone(report.frames),beforeReadFrames=structuredClone(readReport.frames),beforeProgram=structuredClone(program),cancelled=[],previous=globalThis.cancelAnimationFrame;globalThis.cancelAnimationFrame=id=>cancelled.push(id);
 try{assert.equal(applyPlacementReceipt(f.view,f.request,f.result),true);}finally{if(previous===undefined)delete globalThis.cancelAnimationFrame;else globalThis.cancelAnimationFrame=previous;}
 assert.deepEqual(cancelled,[42]);assert.equal(f.s.machineMotionPlanRef,saved);assert.deepEqual(saved,{url:'resource://machine-plan',revision:'5'});assert.equal(report.validity,'Stale');assert.equal(report.playbackAllowed,false);assert.equal(readReport.validity,'Stale');assert.equal(readReport.playbackAllowed,false);assert.deepEqual(report.frames,beforeReportFrames);assert.deepEqual(readReport.frames,beforeReadFrames);assert.deepEqual(program,beforeProgram);
 const state=f.view.fiveMachine;assert.equal(state.playing,false);assert.equal(state.animation,0);assert.equal(state.planValidity,'Stale');assert.equal(state.report,null);assert.equal(state.snapshot,null);assert.equal(state.snapshotKey,null);assert.equal(state.planReadKey,null);assert.equal(state.planReadReport,null);assert.equal(state.planLoadedKey,null);assert.equal(state.program,null);assert.equal(state.programReport,null);assert.equal(state.programOpen,false);
});
