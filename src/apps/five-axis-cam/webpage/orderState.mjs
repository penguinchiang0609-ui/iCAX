import * as THREE from '../../../iCAX-UI/SDK/ThirdParty/three/three.module.js';
import {invalidateCheckResult} from './checkValidity.mjs';

export function applyOrderReceipt(view,request,result){
  const domain=view.scene.domain,setup=domain.setups.find(s=>s.id===request.setupId),g=setup.generatedGeometry;
  const byId=new Map((g?.operations||[]).map(op=>[op.intentId,op]));
  const operations=result.definitionOrder.filter(id=>byId.has(id)).map((id,i)=>({...byId.get(id),order:String(i+1)}));
  const oldConnections=new Map((g?.transitions||[]).map(t=>[`${t.from}/${t.to}`,t]));
  const point=(op,start)=>{
    const direct=op[start?'setupStart':'setupEnd'];if(direct)return direct;
    const placement=domain.placements.find(p=>p.id===op.placementId);
    return new THREE.Vector3(...op[start?'start':'end']).applyMatrix4(new THREE.Matrix4().set(...placement.transform)).toArray();
  };
  const transitions=operations.slice(1).map((to,i)=>{
    const from=operations[i],pair=`${from.intentId}/${to.intentId}`,existing=oldConnections.get(pair);
    if(existing)return existing;
    const policy=structuredClone(result.rapidPolicies[pair]||result.rapidDefaultPolicy);
    return {from:from.intentId,to:to.intentId,fromPlacementId:from.placementId,toPlacementId:to.placementId,
      points:[point(from,false),point(to,true)],frame:'Setup',laser:false,policy,strategy:policy.strategy,inputKey:'',
      endpointStatus:(from.endStatus||from.status||g.status)==='Current'&&(to.startStatus||to.status||g.status)==='Current'?'Current':'Stale',
      geometryStatus:'Missing',validation:'Unchecked',reason:'CAM_RAPID_NOT_PLANNED',productionQualified:false,machineValidation:'Unchecked'};
  });
  setup.definitionOrder=result.definitionOrder;
  setup.generatedGeometry={...g,ordering:result.ordering,operations,transitions,
    rapidGeneratedCount:String(transitions.filter(t=>t.geometryStatus==='Generated').length),rapidClearCount:String(transitions.filter(t=>t.validation==='Clear').length)};
  if(result.changed){setup.machineMotionPlanRef={};invalidateCheckResult(setup.simulationCheck);}
  domain.expected=result.expected;
}
