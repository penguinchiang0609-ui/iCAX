import {geometryEditing} from './workspaceModel.mjs';
export function activeWorkpiece(view){
  const d=view.scene?.domain,p=d?.placements?.find(p=>p.id===view.scene.activePlacementId)||d?.placements?.find(p=>p.id===d.activePlacementId)||d?.placements?.find(p=>p.setupId===d.activeSetupId);
  return d?.workpieces?.find(w=>w.id===p?.workpieceId);
}
export const workpieceLocked=view=>geometryEditing(view)?!!activeWorkpiece(view)?.locked:!!view.scene?.domain?.setups?.find(s=>s.id===view.scene.domain.activeSetupId)?.locked;
export const workpieceEnabled=view=>activeWorkpiece(view)?.enabled!==false;
export const enabledWorkpiecesOnly=view=>['toolpath','simulation'].includes(view.fiveAxisArea);
export const visibleWorkpiecePlacements=view=>{
  const d=view.scene?.domain,placements=d?.placements||[];
  if(!enabledWorkpiecesOnly(view))return placements;
  const enabled=new Set((d?.workpieces||[]).filter(w=>w.enabled!==false).map(w=>w.id));
  return placements.filter(p=>enabled.has(p.workpieceId));
};

