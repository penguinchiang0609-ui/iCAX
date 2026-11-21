// The native receipt distinguishes relative workpiece placement from moving
// the complete assembly, which preserves local geometry and static checks.
import {invalidateCheckResult} from './checkValidity.mjs';
const identity=[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1];
export function applyPlacementReceipt(view,request,result){
 const d=view.scene?.domain,patch=result?.statePatch;
 const p=d?.placements?.find(p=>p.id===request.placementId),s=d?.setups?.find(s=>s.id===request.setupId);
 if(!patch||!p||!s||patch.placement?.id!==p.id||patch.setup?.id!==s.id
   ||JSON.stringify(d.expected)!==JSON.stringify(request.expected))return false;
 Object.assign(p,patch.placement);Object.assign(s,patch.setup);
 Object.assign(d,{expected:patch.expected,inputRevision:patch.inputRevision});
 if(result.motionScope==='Assembly'){
  const plan=s.registrationPlan;
  if(plan?.machineStatus==='Qualified'&&JSON.stringify(plan.assemblyTransform??identity)!==JSON.stringify(s.assemblyTransform??identity))plan.machineStatus='Stale';
 }else{
  if(s.registrationPlan?.status==='Prepared')s.registrationPlan.status='Stale';
  const geometry=s.generatedGeometry;
  if(geometry){
   geometry.status=geometry.operations?.length?'Stale':'Missing';
   for(const op of geometry.operations||[])op.status='Stale';
   for(const t of geometry.transitions||[])Object.assign(t,{endpointStatus:'Stale',geometryStatus:'Stale',validation:'Unchecked',reason:'CAM_RAPID_STALE'});
   geometry.rapidGeneratedCount='0';geometry.rapidClearCount='0';
  }
  invalidateCheckResult(s.fixtureCheck);
  for(const design of [s.fixtureDesign,...(d.fixtureUnits||[]).filter(u=>u.setupId===s.id).map(u=>u.design)])
   for(const assembly of [design?.candidate,design?.assembly].filter(Boolean)){
    assembly.validity='Stale';
    for(const proof of [assembly.cutClearance,assembly.openingPreclearance])invalidateCheckResult(proof);
   }
 }
 invalidateCheckResult(s.simulationCheck);
 // The saved resource can stay unchanged while its world-space certificate
 // expires. Retire hydrated playback and program caches immediately, before
 // the machine viewport next refreshes its native report against new expected.
 const machine=view.fiveMachine;
 if(machine){
  for(const report of new Set([machine.report,machine.planReadReport,machine.programReport]))if(report)Object.assign(report,{validity:'Stale',playbackAllowed:false});
  if(machine.animation)globalThis.cancelAnimationFrame?.(machine.animation);
  Object.assign(machine,{playing:false,animation:0,last:0,cursor:0,alarm:false,report:null,snapshot:null,snapshotKey:null,planReadKey:null,planReadReport:null,planLoadedKey:null,planValidity:'Stale',program:null,programReport:null,programOpen:false});
 }
 return true;
}
