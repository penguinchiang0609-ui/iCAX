import {buildCutTree} from './cutTreeModel.mjs';

// A connection exists in the machining order before its path is generated.
// This list projection supplies identity/status only, never synthetic geometry.
export function rapidListModel(view){
 const setup=view.scene?.domain?.setups?.find(s=>s.id===view.scene.domain.activeSetupId),generated=setup?.generatedGeometry;
 if(!Array.isArray(view.scene?.intentToolpaths)||view.scene.intentToolpaths.length===0&&generated?.status==='Current')return {operations:generated?.operations||[],transitions:generated?.transitions||[]};
 const tree=buildCutTree(view),cuts=tree.cuts.filter(c=>c.setupId===setup?.id&&c.enabled!==false&&tree.nodes.get(`c:${c.id}`)?.enabled!==false);
 const operations=cuts.map((c,i)=>({intentId:c.id,placementId:c.placementId,order:String(i+1)}));
 const saved=new Map((generated?.transitions||[]).map(t=>[`${t.from}/${t.to}`,t])),settings=setup?.rapidSettings||{};
 const transitions=operations.slice(1).map((to,i)=>{
  const from=operations[i],existing=saved.get(`${from.intentId}/${to.intentId}`);
  return {...(existing||{from:from.intentId,to:to.intentId,fromPlacementId:from.placementId,toPlacementId:to.placementId,
   definitionOnly:true,geometryStatus:'Missing',endpointStatus:'Missing',validation:'Unchecked',
   policy:{strategy:settings.strategy||'Quick',marginMm:settings.marginMm??.2,liftHeightMm:settings.liftHeightMm??10}}),fromOrder:from.order,toOrder:to.order};
 });
 return {operations,transitions};
}
