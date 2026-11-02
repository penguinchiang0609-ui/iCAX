export const ORDER_STRATEGIES=Object.freeze([
  ['MarkHoleTrim','打标-内孔-修边'],
  ['MarkAndHoleTrim','打标&内孔-修边'],
]);
const setup=view=>view.scene?.domain?.setups?.find(s=>s.id===view.scene.domain.activeSetupId);
export function orderStrategy(view){
  const current=setup(view);
  return view.fiveOrderStrategies?.get(current?.id)??current?.generatedGeometry?.ordering?.strategy??ORDER_STRATEGIES[0][0];
}
export function selectOrderStrategy(view,strategy){
  const current=setup(view);
  if(!current||!ORDER_STRATEGIES.some(([id])=>id===strategy)||view.pending||view.fiveOrder?.request||view.fiveOrder?.running)return false;
  (view.fiveOrderStrategies??=new Map()).set(current.id,strategy);return true;
}
