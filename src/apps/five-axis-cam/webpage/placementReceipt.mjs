// A rigid placement changes no mesh or authored local toolpath. Apply the
// native transaction receipt; executable results wait for explicit generation.
import {invalidateCheckResult} from './checkValidity.mjs';
export function applyPlacementReceipt(view,request,result){
 const d=view.scene?.domain,patch=result?.statePatch;
 const p=d?.placements?.find(p=>p.id===request.placementId),s=d?.setups?.find(s=>s.id===request.setupId);
 if(!patch||!p||!s||patch.placement?.id!==p.id||patch.setup?.id!==s.id
   ||JSON.stringify(d.expected)!==JSON.stringify(request.expected))return false;
 Object.assign(p,patch.placement);Object.assign(s,patch.setup);
 Object.assign(d,{expected:patch.expected,inputRevision:patch.inputRevision});
 if(s.registrationPlan?.status==='Prepared')s.registrationPlan.status='Stale';
 const geometry=s.generatedGeometry;
 if(geometry){
  geometry.status=geometry.operations?.length?'Stale':'Missing';
  for(const op of geometry.operations||[])op.status='Stale';
  for(const t of geometry.transitions||[])Object.assign(t,{endpointStatus:'Stale',geometryStatus:'Stale',validation:'Unchecked',reason:'CAM_RAPID_STALE'});
  geometry.rapidGeneratedCount='0';geometry.rapidClearCount='0';
 }
 for(const proof of [s.fixtureCheck,s.simulationCheck])invalidateCheckResult(proof);
 for(const design of [s.fixtureDesign,...(d.fixtureUnits||[]).filter(u=>u.setupId===s.id).map(u=>u.design)])
  for(const assembly of [design?.candidate,design?.assembly].filter(Boolean)){
   assembly.validity='Stale';
   for(const proof of [assembly.cutClearance,assembly.openingPreclearance])invalidateCheckResult(proof);
  }
 return true;
}
