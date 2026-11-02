export const ORDER_DEFAULTS=Object.freeze({heightPenaltyWeight:3});
export function orderOptions(view){
  const setup=view.scene?.domain?.setups?.find(s=>s.id===view.scene.domain.activeSetupId);
  const draft=view.fiveOrderDrafts?.get(setup?.id)||setup?.generatedGeometry?.ordering?.options;
  return {heightPenaltyWeight:draft?.heightPenaltyWeight??ORDER_DEFAULTS.heightPenaltyWeight};
}
export function orderInput(view){
  const value=orderOptions(view).heightPenaltyWeight,heightPenaltyWeight=Number(value);
  if(!String(value).trim()||!Number.isFinite(heightPenaltyWeight))throw Error('高度差惩罚系数须为有效数值。');
  // Send only the supported user parameter; native defaults select smart,
  // distance-based sorting of the active setup, without old hidden settings.
  return {strategy:'MarkHoleTrim',options:{heightPenaltyWeight}};
}
export function orderFieldChanged(view,name,value){
  if(name!=='heightPenaltyWeight'||view.pending||view.fiveOrder?.request||view.fiveOrder?.running||!view.scene?.domain?.activeSetupId)return false;
  (view.fiveOrderDrafts??=new Map()).set(view.scene.domain.activeSetupId,{heightPenaltyWeight:value});view.error='';return true;
}
